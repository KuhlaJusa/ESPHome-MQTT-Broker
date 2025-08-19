#include "minibroker.h"
#include "esphome/core/application.h"

namespace esphome {
namespace mini_broker {

Client* MiniBroker::get_client(const char* idData, size_t idLen) {
    //find free stub and reset values
    for (auto& c : clients_) {
        if (c->clientId.size() == idLen &&
            memcmp(c->clientId.data(), idData, idLen) == 0) {
            return c;
        }
    }

    // Id not found, create new client
    ESP_LOGD(TAG, "Create new Client");
    Client* c = new Client();
    c->clientId.assign(idData, idLen);
    c->subscribed_topics.reserve(16);
    c->published_topics.reserve(16);
    clients_.push_back(c);
    return c;
}

ClientStub* MiniBroker::get_free_stub() {
    //find free stub and reset values
    for (auto& s : stubs_) {
        if (s->inactive) {
            s->parent = nullptr;
            s->pcb = nullptr;
            s->realClient = nullptr;
            s->buf.clear();
            s->inactive = false;
            return s;
        }
    }
    // No free one, create new
    ESP_LOGD(TAG, "Create new Stub");
    ClientStub* s = new ClientStub();
    s->buf.reserve(1024);
    stubs_.push_back(s);
    return s;
}

void MiniBroker::release_stub(ClientStub* s, bool client_err) {
    if(!s || s->inactive) return;

    s->inactive = true;

    ESP_LOGD(TAG, "Release Stub: %p, Client Error: %d", s, client_err);
    // unhook callbacks 
    if (s->pcb) {
        // If client_err (from the tcp_err callback), connection is already closed
        if (!client_err) {
            tcp_arg(s->pcb, nullptr);
            tcp_recv(s->pcb, nullptr);
            tcp_err(s->pcb, nullptr);
            tcp_sent(s->pcb, nullptr);
            tcp_poll(s->pcb, nullptr, 0);
            err_t err = tcp_close(s->pcb);
            if (err != ERR_OK) {
                // If close fails, abort
                tcp_abort(s->pcb);
            }
        }
        s->pcb = nullptr;
    }

    // if client exist, and associated client has this stub, then remove
    if(s->realClient && s == s->realClient->stub){
        s->realClient->stub = nullptr;
        ESP_LOGD(TAG, "Remove Stub from Client: %s", s->realClient->clientId.c_str());
    }
}

TopicEntry* MiniBroker::get_topic(const uint8_t* topic_ptr, size_t topic_len) {
    std::string key(reinterpret_cast<const char*>(topic_ptr), topic_len);

    auto it = topics_.find(key);
    if (it != topics_.end()) {
        return it->second;
    }

    // Topic not found → create new TopicEntry
    auto te = new TopicEntry();
    te->subscribers.reserve(16);

    // Insert the key into the map first
    auto result = topics_.emplace(std::move(key), te);
    te->topicPtr = &result.first->first;  // point to the string in the map

    ESP_LOGD(TAG, "Topic Created: %.*s", (int)topic_len, topic_ptr);
    return te;
}

TopicEntry* MiniBroker::get_topic(const std::string& topic){
    return get_topic(reinterpret_cast<const uint8_t*>(topic.data()), topic.size());
}


} // namespace mqtt_broker
} // namespace esphome