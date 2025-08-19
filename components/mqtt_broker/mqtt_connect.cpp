#include "minibroker.h"
#include "esphome/core/application.h"

namespace esphome {
namespace mini_broker {
    
void MiniBroker::handleConnect(ClientStub* s, uint8_t* data, size_t len) {
    // variable header at least 12 bytes for connect message (header + 2bytes id length)
    if (len < 12) {
        ESP_LOGW(TAG, "Connection Header smaller than 12");
        release_stub(s, false);
        return;
    }

    uint16_t protoNameLen = (data[0] << 8) | data[1];
    // Check length is exactly 4 (for "MQTT")
    if (protoNameLen != 4) {
        ESP_LOGW(TAG, "Invalid Protocol Name length: %u", protoNameLen);
        release_stub(s, false);
        return;
    }

    // Compare string
    if (memcmp(&data[2], "MQTT", 4) != 0) {
        ESP_LOGW(TAG, "Protocol Name not 'MQTT'");
        release_stub(s, false);
        return;
    }

    // Protocol version is the byte right after the protocol name
    uint8_t protoVersion = data[6];
    if (protoVersion != 4) { // MQTT 3.1.1 (MQTT) protocol version is 4
        ESP_LOGW(TAG, "Unsupported protocol version: %u", protoVersion);
        release_stub(s, false);
        return;
    }

    uint8_t connect_flags = data[7];
    uint16_t keepalive_time = (data[8] << 8) | data[9];

    // Payload starts here:
    size_t payloadPos = 10;
    if (payloadPos + 2 > len) {
        ESP_LOGW(TAG, "Packet too short for Client ID length");
        release_stub(s, false);
        return;
    }

    // Client ID
    uint16_t clientIdLen = (data[payloadPos] << 8) | data[payloadPos + 1];
    payloadPos += 2;

    if (payloadPos + clientIdLen > len) {
        ESP_LOGW(TAG, "Packet too short for Client ID");
        release_stub(s, false);
        return;
    }

    // Give id position and length to get_client()
    Client* c = get_client(reinterpret_cast<char*>(data + payloadPos), clientIdLen);
    // if client has a stub, release it
    if (c->stub){
        release_stub(c->stub, false);
    } 

    // Attach stub to client
    c->stub = s;
    s->realClient = c;

    c->keepalive = keepalive_time;

    bool cleanSession = (connect_flags & 2) >> 1;
    bool session_present = !c->cleanSession;
    // If clean session, clear old subscriptions
    if (cleanSession) {
        for (TopicEntry* te : c->subscribed_topics) {
            unsubscribeClientFromTopic(c, te);
            // Remove from topic's subscriber list
        }
        c->cleanSession = 1;
        c->published_topics.clear();
        c->subscribed_topics.clear();
        session_present = 0;
    }
    else{
        c->cleanSession = 0;
    }

    ESP_LOGI(TAG, "Accept Client: %s, CleanSession: %d, KeepAlive: %d", c->clientId.c_str(), cleanSession, keepalive_time);

    // accept; ignore details for minimal broker
    if (s->pcb) sendConnack_pcb(s->pcb, session_present, 0);
}

void MiniBroker::sendConnack_pcb(tcp_pcb* pcb, uint8_t sp_flag, uint8_t ack_flag) {
    uint8_t pkt[] = {0x20, 0x02, sp_flag, ack_flag};
    err_t err = tcp_write(pcb, pkt, sizeof(pkt), TCP_WRITE_FLAG_COPY);
    if (err != ERR_OK) {
        ESP_LOGE(TAG, "tcp_write: Connack failed: %d", err);
        return;
    }

    // Tell lwIP to send it, but don't wait
    err = tcp_output(pcb);
    if (err != ERR_OK) {
        ESP_LOGE(TAG, "tcp_output: Connack failed: %d", err);
    }
}

} // namespace mqtt_broker
} // namespace esphome