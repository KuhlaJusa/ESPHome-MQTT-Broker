#include "minibroker.h"
#include "esphome/core/application.h"

namespace esphome {
namespace mini_broker {

void MiniBroker::handleUnsubscribe(ClientStub* s, uint8_t* data, size_t len) {
    if (len < 3) return; // at least packetId + one topic

    Client* c = s->realClient;

    uint16_t packetId = (data[0] << 8) | data[1];
    size_t pos = 2;

    size_t topicsCount = 0;

    while (pos + 2 <= len) {
        uint16_t topicLen = (data[pos] << 8) | data[pos + 1];
        pos += 2;

        if (pos + topicLen > len) break;

        const uint8_t* topic = data + pos;  // raw topic bytes
        pos += topicLen;

        unsubscribeClientFromTopic(c, topic, topicLen);
        topicsCount++;
    }
    sendUnsuback_pcb(s->pcb, packetId);
}

void MiniBroker::sendUnsuback_pcb(tcp_pcb* pcb, uint16_t packetId) {
    uint8_t pkt[4] = {
        0xB0,  // UNSUBACK packet type
        0x02,  // Remaining length = 2
        static_cast<uint8_t>(packetId >> 8),
        static_cast<uint8_t>(packetId & 0xFF)
    };

    // Send packet over TCP
    err_t err = tcp_write(pcb, pkt, sizeof(pkt), TCP_WRITE_FLAG_COPY);
    if (err != ERR_OK) {
        ESP_LOGE(TAG, "tcp_write: UNSUBACK failed: %d", err);
        return;
    }
    // Flush the TCP buffer
    err = tcp_output(pcb);
    if (err != ERR_OK) {
        ESP_LOGE(TAG, "tcp_output: UNSUBACK failed: %d", err);
    }
}

} // namespace mqtt_broker
} // namespace esphome