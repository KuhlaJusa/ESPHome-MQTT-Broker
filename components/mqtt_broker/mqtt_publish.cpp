#include "minibroker.h"
#include "esphome/core/application.h"

namespace esphome {
namespace mini_broker {

void MiniBroker::handlePublish(ClientStub* s, uint8_t flags, uint8_t* data, size_t len) {
    if (len < 2) return;

    Client* client = s->realClient;

    // Extract topic
    uint16_t topic_len = (data[0] << 8) | data[1];
    if (2 + topic_len > len) return;

    const uint8_t* topic_ptr = data + 2;
    const uint8_t* payload_ptr = data + 2 + topic_len;
    size_t payload_len = len - (2 + topic_len);

    // Broadcast to matching subscribers
    TopicEntry* te = get_topic(topic_ptr, topic_len);
    if (!te) return;

    // --- Build the publish packet once ---
    std::vector<uint8_t> packet;
    build_publish_message(packet, topic_ptr, topic_len, payload_ptr, payload_len);

    for (auto c : te->subscribers) {
        if (c->stub && c->stub->pcb) {
            // Send publish to client stub
            sendPublish_pcb(c->stub->pcb, packet);
            ESP_LOGV(TAG, "Published to Client: %s, Topic: %.*s",
                     c->clientId.c_str(), topic_len, topic_ptr);
        }
    }

    // Calling inside handlePublish
    if (publish_cb_) {
        ESP_LOGVV(TAG, "calling cb function");
        publish_cb_(topic_ptr, topic_len, payload_ptr, payload_len);
    }
}

void MiniBroker::build_publish_message(std::vector<uint8_t>& out,
                                       const uint8_t* topic_ptr, size_t topic_len,
                                       const uint8_t* payload, size_t payload_len) {
    out.clear();
    out.reserve(1 + 4 + 2 + topic_len + payload_len);

    // Fixed header: PUBLISH, QoS0, DUP=0, Retain=0
    out.push_back(0x30);

    // Remaining length
    uint32_t rl = 2 + static_cast<uint32_t>(topic_len) + static_cast<uint32_t>(payload_len);
    encodeRemainingLength(out, rl);

    // Topic length
    out.push_back(static_cast<uint8_t>(topic_len >> 8));
    out.push_back(static_cast<uint8_t>(topic_len & 0xFF));;

    // Topic string
    out.insert(out.end(), topic_ptr, topic_ptr + topic_len);

    // Payload
    if (payload && payload_len > 0) {
        out.insert(out.end(), payload, payload + payload_len);
    }
}

void MiniBroker::sendPublish_pcb(tcp_pcb* pcb, const std::vector<uint8_t>& packet) {
    // Send packet
    err_t err = tcp_write(pcb, packet.data(), packet.size(), TCP_WRITE_FLAG_COPY);
    if (err != ERR_OK) {
        ESP_LOGE(TAG, "tcp_write: PUBLISH failed: %d", err);
        return;
    }

    err = tcp_output(pcb);
    if (err != ERR_OK) {
        ESP_LOGE(TAG, "tcp_output: PUBLISH failed: %d", err);
    }
}

} // namespace mqtt_broker
} // namespace esphome