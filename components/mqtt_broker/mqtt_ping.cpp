#include "minibroker.h"
#include "esphome/core/application.h"

namespace esphome {
namespace mini_broker {

void MiniBroker::handlePingreq(ClientStub* s) {
    if (s->pcb) sendPingresp_pcb(s->pcb);
    ESP_LOGV(TAG, "Send Ping Response to Client: %s", s->realClient->clientId.c_str());
}

void MiniBroker::sendPingresp_pcb(tcp_pcb* pcb) {
    uint8_t pkt[] = {0xD0, 0x00};
    
    err_t err = tcp_write(pcb, pkt, sizeof(pkt), TCP_WRITE_FLAG_COPY);
    if (err != ERR_OK) {
        ESP_LOGE(TAG, "tcp_write: Pingresp failed: %d", err);
        return;
    }

    // Tell lwIP to send it, but don't wait
    err = tcp_output(pcb);
    if (err != ERR_OK) {
        ESP_LOGE(TAG, "tcp_output: Pingresp failed: %d", err);
    }
}

} // namespace mqtt_broker
} // namespace esphome
