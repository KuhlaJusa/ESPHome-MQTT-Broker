#include "minibroker.h"
#include "esphome/core/application.h"

namespace esphome {
namespace mini_broker {

MiniBroker::MiniBroker(uint16_t port) 
    : port_(port), listen_pcb_(nullptr) {}

MiniBroker::~MiniBroker() { stop(); }

void MiniBroker::set_port(uint16_t port){ port_ = port; }

void MiniBroker::set_publish_cb(PublishCallback cb) { publish_cb_ = cb;}

void MiniBroker::loop_iteration(){
    //check if broker runnnig
    if(!listen_pcb_) return;
}

bool MiniBroker::begin() {
    stop();

    //pending_delete_.reserve(8);
    stubs_.reserve(16);
    clients_.reserve(16);
    topics_.reserve(32);
    topics_.max_load_factor(0.7f);

    listen_pcb_ = tcp_new();
    if (!listen_pcb_) return false;

    err_t r = tcp_bind(listen_pcb_, IP_ADDR_ANY, port_);
    if (r != ERR_OK) {
        tcp_close(listen_pcb_);
        listen_pcb_ = nullptr;
        return false;
    }

    listen_pcb_ = tcp_listen(listen_pcb_);
    tcp_accept(listen_pcb_, &MiniBroker::lwip_accept_cb_static);
    tcp_arg(listen_pcb_, this);     //class instance as argument to accept callback

    ESP_LOGI(TAG, "MiniBroker started on port: %d", port_);
    return true;
}

void MiniBroker::stop() {
    // close listener
    if (listen_pcb_) {
        tcp_arg(listen_pcb_, nullptr);
        tcp_accept(listen_pcb_, nullptr);
        tcp_close(listen_pcb_);
        listen_pcb_ = nullptr;
    }

    // Close and free stubs
    for (auto &s : stubs_) {
        if (!s) continue;

        // Close the PCB
        if (!s->inactive && s->pcb) {
            release_stub(s, true); // force close
        }

        // Disconnect from client
        if (s->realClient) {
            s->realClient->stub = nullptr;
            s->realClient = nullptr;
        }

        s->buf.clear();
        delete s;
    }
    stubs_.clear();

    // Free clients
    for (auto &c : clients_) {
        if (!c) continue;

        if (c->stub) {
            c->stub->realClient = nullptr;
            c->stub = nullptr;
        }

        c->published_topics.clear();
        c->subscribed_topics.clear();
        delete c;
    }
    clients_.clear();

    // Free topics
    for (auto& pair : topics_) {
        TopicEntry* te = pair.second;
        if (!te) continue;

        // Clear subscribers vector (just pointers, no deletion)
        te->subscribers.clear();

        // Delete the TopicEntry itself
        delete te;
    }
    topics_.clear();

    ESP_LOGI(TAG, "MiniBroker stopped");
}

// checks for the stub happens directly in the lwip recv callback, so we know in this function and in the MQTT-functions 
// that the stub is valid, it is active, and has a valid pcb.
void MiniBroker::client_read_buffer(ClientStub* s) {
    const size_t bufSize = s->buf.size();
    const uint8_t* bufData = s->buf.data();

    size_t pos = 0;

    //keep activity time
    s->last_activity_ms = millis();
    
    // process as many full MQTT packets as possible
    while (pos + 2 <= bufSize) {  // need at least 2 bytes to start
        uint8_t* bufPtr = s->buf.data() + pos;
        uint8_t byte1 = bufPtr[0];

        uint32_t remainingLength = 0;
        size_t rl_bytes = 0;

        int rc = decodeRemainingLength(bufPtr + 1, bufSize - pos - 1, remainingLength, rl_bytes);;
        if (rc < 0) break; // incomplete RL

        size_t totalHeaderLen = 2 + rl_bytes;
        size_t packetTotalLen = totalHeaderLen + remainingLength;

        if (pos + packetTotalLen > bufSize) break; // incomplete packet, wait for more data

        uint8_t packetType = byte1 >> 4;
        uint8_t flags = byte1 & 0x0F;
        uint8_t* payloadStart = bufPtr + totalHeaderLen;
        size_t payloadLen = remainingLength;

        // if a link exist and a second connect request, or 
        // no link exist and not a connect request
        // then somethings wrong
        bool clientLinkValid = (s->realClient && s->realClient->stub == s);
        if (clientLinkValid == (packetType == 1)) {
            ESP_LOGW(TAG, "Invalid Client/Stub link, or Second Connect request over same Connection. Closing Connection..");
            release_stub(s, false);
            return;
        }
        else{
            switch (packetType) {
                case 1: handleConnect(s, payloadStart, payloadLen); break;
                case 3: handlePublish(s, flags, payloadStart, payloadLen); break;
                case 8: 
                    if (flags == 2)
                        handleSubscribe(s, payloadStart, payloadLen); 
                    else   //if flags not exactly two, Server MUST close connection
                        release_stub(s, false);
                    break;
                case 10: 
                    if (flags == 2)
                        handleUnsubscribe(s, payloadStart, payloadLen); 
                    else   //if flags not exactly two, Server MUST close connection
                        release_stub(s, false);
                    break;
                case 12: handlePingreq(s); break;
                case 14: handleDisconnect(s); break;
                default: break;
            }
        }
        pos += packetTotalLen;
    }
    // Remove processed bytes from vector after processing
    s->buf.erase(s->buf.begin(), s->buf.begin() + pos);

    return;
}

}
}