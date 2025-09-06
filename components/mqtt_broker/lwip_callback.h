#pragma once

extern "C" {
    #include "lwip/err.h"
    #include "lwip/tcp.h"
}

namespace esphome {
namespace asyncbroker {

static err_t lwip_accept_cb_static(void* arg, struct tcp_pcb* newpcb, err_t err){
    if (!arg) return ERR_OK;
    return static_cast<AsyncBroker*>(arg)->OnConnect(newpcb, err);
}

static err_t lwip_recv_cb_static(void* arg, struct tcp_pcb* tpcb, struct pbuf* p, err_t err) {
    if (!arg) return ERR_OK;
    return static_cast<MqttClientHandler*>(arg)->lwip_recv_cb(tpcb, p, err);
}

static void lwip_err_cb_static(void* arg, err_t err) {
    if (!arg) return;
    static_cast<MqttClientHandler*>(arg)->lwip_err_cb(err);
}

static err_t lwip_poll_cb_static(void* arg, struct tcp_pcb* tpcb) {
    if (!arg) return ERR_OK;
    return static_cast<MqttClientHandler*>(arg)->lwip_poll_cb(tpcb);
}

static err_t lwip_sent_cb_static(void* arg, struct tcp_pcb* tpcb, u16_t len) {
    if (!arg) return ERR_OK;
    return static_cast<MqttClientHandler*>(arg)->lwip_sent_cb(tpcb, len);
}


} //namespace asyncbroker
} //namespace esphome