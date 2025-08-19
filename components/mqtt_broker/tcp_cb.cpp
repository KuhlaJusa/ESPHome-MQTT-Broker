#include "minibroker.h"
#include "esphome/core/application.h"

namespace esphome {
namespace mini_broker {

err_t MiniBroker::lwip_accept_cb_static(void* arg, struct tcp_pcb* newpcb, err_t err) {
    MiniBroker* broker = static_cast<MiniBroker*>(arg);

    // Take a Stub from the pool, assign client later
    ClientStub* stub = broker->get_free_stub();
    stub->pcb = newpcb;
    stub->parent = broker;
    stub->last_activity_ms = millis();
    // set tcp callbacks with stub pointer
    tcp_arg(newpcb, stub);
    tcp_recv(newpcb, &MiniBroker::client_recv_cb_static);
    tcp_err(newpcb, &MiniBroker::client_err_cb_static);
    tcp_sent(newpcb, &MiniBroker::client_sent_cb_static);

    tcp_poll(newpcb, &MiniBroker::tcp_poll_cb_static, 10); // ~5s poll callback

    ESP_LOGD(TAG, "New Incoming Connection");
    return ERR_OK;
}

void MiniBroker::client_err_cb_static(void* arg, err_t err) {
    ClientStub* s = static_cast<ClientStub*>(arg);
    release_stub(s, true);
}

err_t MiniBroker::client_sent_cb_static(void* arg, struct tcp_pcb* tpcb, u16_t len) {

    return ERR_OK;
}

err_t MiniBroker::client_recv_cb_static(void* arg, struct tcp_pcb* tpcb, struct pbuf* p, err_t err) {

    if (err != ERR_OK) {
        if (p) pbuf_free(p);
        return err;
    }

    ClientStub* s = static_cast<ClientStub*>(arg);
    
    if (p == nullptr) {
        // remote closed connection
        if (s->realClient) {
            ESP_LOGI(TAG, "Client Disconnected: %s", s->realClient->clientId.c_str());
        }
        else {
            ESP_LOGD(TAG, "Stub Disconnected, before Client was assigned");
        }
        release_stub(s, false);
        return ERR_OK;
    }

    //check if stub exist and active
    if (!s || s->inactive) {
        if (p) pbuf_free(p);
        return ERR_OK;
    }
    
    // copy pbuf data into client's buffer
    for (struct pbuf* q = p; q != nullptr; q = q->next) {
        const uint8_t* ptr = static_cast<const uint8_t*>(q->payload);
        // ESP_LOG_BUFFER_HEXDUMP("mini_broker", ptr, q->len, ESP_LOG_INFO);
        s->buf.insert(s->buf.end(), ptr, ptr + q->len);
    }
    
    // inform lwIP we've received the data
    tcp_recved(tpcb, p->tot_len);
    pbuf_free(p);

    s->parent->client_read_buffer(s);

    return ERR_OK;
}


err_t MiniBroker::tcp_poll_cb_static(void* arg, tcp_pcb* pcb) {
    ClientStub* s = static_cast<ClientStub*>(arg);
    if (!s || s->inactive) return ERR_OK;

    uint32_t now_ms = millis();  // or your system tick in ms

    if(!s->realClient){
        if (now_ms - s->last_activity_ms > 60000) {
            ESP_LOGW("mini_broker", "Stub Not Conneted to Client for 60s");
            release_stub(s, false);  // close connection
            return ERR_ABRT;
        }
    }
    else{
        Client* c = s->realClient;

        // Convert Keep Alive from seconds to milliseconds
        uint32_t keepalive_ms = static_cast<uint32_t>(c->keepalive) * 1000;

        // Check if client has been silent for more than 1.5× keepalive
        if (now_ms - s->last_activity_ms > keepalive_ms + keepalive_ms / 2) {
            ESP_LOGW("mini_broker", "Client %s timed out (no MQTT Control packets received)", c->clientId.c_str());
            release_stub(s, false);  // close connection
            return ERR_ABRT;
        }
    }
    return ERR_OK;
}

} // namespace mqtt_broker
} // namespace esphome