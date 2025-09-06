#include "AsyncBroker.h"
#include "esphome/core/application.h"
#include "lwip_callback.h"

namespace esphome {
namespace asyncbroker {

MqttClientHandler::MqttClientHandler(HandlerManager& hm, uint16_t id) : handler_manager_(hm), _handlerId(id) {

    rxQueue = xQueueCreate(QUEUE_LENGTH, ITEM_SIZE);
    buf.reserve(256);
    // if(!_mqttBroker) {
    //     ESP_LOGE(TAG, "mqttBroker is null in MqttClient constructor");
    //     delete this;
    //     return;
    // }
    ESP_LOGV(TAG, "new MqttClientHandler created");
}

MqttClientHandler::~MqttClientHandler() {
    detach();
    if (rxQueue) {
        MqttEvent* item = nullptr;
        while (xQueueReceive(rxQueue, &item, 0) == pdTRUE) {
            delete item;
        }
        vQueueDelete(rxQueue);
        rxQueue = nullptr;
    }

    // Also clear the response queue if it holds pointers
    // for (auto* obj : _responseQueue) {
    //     delete obj;
    // }
    _responseQueue.clear_with_destruct();
}

void MqttClientHandler::attach(tcp_pcb* client_pcb) {
    if (!client_pcb) return;
    //clear queues again to be safe
    xQueueReset(rxQueue);
    _responseQueue.clear();
    
    ESP_LOGI(TAG, "New Connection, on Handler: %d", _handlerId);
    _pcb = client_pcb;

    // set pointer to this instance for callbacks
    tcp_arg(_pcb, this);

    // Set the callbacks
    tcp_recv(_pcb, &lwip_recv_cb_static);
    tcp_err(_pcb, &lwip_err_cb_static);
    tcp_poll(_pcb, &lwip_poll_cb_static, POLL_INTERVAL_S); // around two ticks per second
    tcp_sent(_pcb, &lwip_sent_cb_static);

    handler_manager_._addHandler(this);

    tcp_nagle_disable(_pcb); // no-delay
}

void MqttClientHandler::detach(bool abort, bool from_err) {
    if (!_pcb) {
        //already detached, as no asyncClient
        return;
    }
    ESP_LOGI(TAG, "Disconnect, on Handler: %d", _handlerId);
    // if (_mqttClient && _mqttClient->handler != this){
    //     //should not happen
    //     ESP_LOGW(TAG, "detach called but mqttClient->handler does not point to this");
    //     return;
    // }

    tcp_arg(_pcb, NULL);
    tcp_recv(_pcb, nullptr);
    tcp_err(_pcb, nullptr);
    tcp_poll(_pcb, nullptr, 0); // poll every 4 ticks
    tcp_sent(_pcb, nullptr);

    
    
    handler_manager_._handleDisconnect(this);
    buf.clear();
    xQueueReset(rxQueue);

    _responseQueue.clear();

    if (_mqttClient){
        // std::lock_guard<std::recursive_mutex> lock(_mqttClient->_client_lock);
        // taskENTER_CRITICAL(&(_mqttClient->_lock_clients));
        if (_mqttClient->handler == this) {
            _mqttClient->handler = nullptr;
            ESP_LOGV(TAG, "Client disconnected: %s", this->clientId.c_str());
        }
        else{
            ESP_LOGV(TAG, "Client disconnected (reconnected on other Handler): %s", this->clientId.c_str());
        }
        _mqttClient = nullptr;
        // taskENTER_CRITICAL(&(_mqttClient->_lock_clients));
    }
    else {
        ESP_LOGV(TAG, "Client disconnected: (no clientId)");
    }

    if(!from_err && (abort || tcp_close(_pcb) != ERR_OK)) {
        tcp_abort(_pcb); // hard close
    }
    // mark as "free" as last action. So 
    _pcb = nullptr;
}

void MqttClientHandler::set_max_inflight_bytes(size_t value) {
    if (value >= RESPONSE_MIN_INFLIGH && value <= RESPONSE_MAX_INFLIGH) {
        _max_inflight = value;
        ESP_LOGD(TAG, "Updated max_inflight to: %d, pcb: %p", _max_inflight, _pcb);
    }
}

void MqttClientHandler::attach_MqttClient(MqttClient* mqtt_client){
    if (!mqtt_client) return;
    if (_mqttClient) {
        ESP_LOGW(TAG, "Handler already has a MqttClient attached");
        return;
    }
    _mqttClient = mqtt_client;
    mqtt_client->handler = this;
    ESP_LOGV(TAG, "Client attach to Handler: %s", mqtt_client->clientId.c_str());
}
// bool MqttClientHandler::_queueMessage(const char* message, size_t len) {
//     MqttResponse_SharedData_t msg(message, len);

//     if (!_responseQueue.push(std::move(msg))) {
//         ESP_LOGE(TAG, "Event message queue overflow: discard message");
//         return false;
//     }

//     // try sending immediately if possible
//     if (_asyncClient && _inflight < _max_inflight) {
//         _runQueue();
//     }
//     return true;
// }

// bool MqttClientHandler::_queueMessage(MqttResponse_SharedData_t&& msg) {
//     if (!_responseQueue.push(std::move(msg))) {
//         ESP_LOGE(TAG, "Event message queue overflow: discard message");
//         return false;
//     }

//     if (_asyncClient && _inflight < _max_inflight) {
//         _runQueue();
//     }
//     return true;
// }


// void MqttClientHandler::_runQueue() {
//     if (!_asyncClient) return;

//     size_t total_bytes_written = 0;
//     size_t sz = _responseQueue.size();
//     MqttResponse_SharedData_t msg;

//     for (size_t idx = 0; idx < sz; ++idx) {
//         if (!_responseQueue.peek(idx, msg)) continue;

//         if (!msg.sent()) {
//             size_t bytes_written = msg.write(_asyncClient);
//             total_bytes_written += bytes_written;
//             _inflight += bytes_written;

//             if (bytes_written == 0 || _inflight > _max_inflight) break;

//             if (msg.finished()) {
//                 // remove only fully sent messages from front
//                 MqttResponse_SharedData_t dummy;
//                 _responseQueue.pop(dummy);
//                 --idx; // adjust index since we removed front
//                 --sz;
//             }
//         }
//     }

//     if (total_bytes_written) {
//         _asyncClient->send();
//     }
// }

void MqttClientHandler::close(MqttClient* caller){
    if (_pcb && caller == _mqttClient) {
        ESP_LOGI(TAG, "Closing connection");
        detach();
    }
}

err_t MqttClientHandler::lwip_recv_cb(struct tcp_pcb* tpcb, struct pbuf* p, err_t err) {
    if (!p) {
        // connection closed
        ESP_LOGD(TAG, "Connection was closed");
        detach();
        return ERR_OK;
    }

    if (err != ERR_OK) {
        pbuf_free(p);
        return err;
    }


    // Flood detection
    uint32_t now = millis();  // or esp_timer_get_time()/1000 for µs→ms
    _last_activity_time = now;
    if (now - _rx_last_tick > 1000) {
        // reset every second
        _rx_last_tick = now;
        _rx_packet_count = 0;
    }
    _rx_packet_count++;

    if (_rx_packet_count > MAX_PACKETS_PER_SECOND) {
        ESP_LOGW(TAG, "Client flooding: %u packets/sec > limit %u, closing",
                 _rx_packet_count, MAX_PACKETS_PER_SECOND);
        pbuf_free(p);
        detach(true);
        return ERR_ABRT;
    }



    for (struct pbuf* q = p; q != nullptr; q = q->next) {
        uint8_t* ptr = static_cast<uint8_t*>(q->payload);
        // ESP_LOG_BUFFER_HEXDUMP("mini_broker", ptr, q->len, ESP_LOG_INFO);
        _onData(ptr, q->len);
    }

    tcp_recved(tpcb, p->tot_len); // inform LWIP we received data
    pbuf_free(p);

    return ERR_OK;
}

void MqttClientHandler::lwip_err_cb(err_t err) {
    // error happened
    ESP_LOGW(TAG, "LWIP TCP error %d", err);
    detach(true, true);
}

err_t MqttClientHandler::lwip_poll_cb(struct tcp_pcb* tpcb) {
    if (!_pcb) return ERR_OK;

    uint32_t now_ms = millis();
    
    if(!_mqttClient) {
        if (now_ms - _last_activity_time > TIMOUT_INTERVAL_MS) {
            ESP_LOGW(TAG, "Stub Not Conneted to Client for 30s");
            detach(true);
            return ERR_ABRT;
        }
    }
    else{
        // Convert Keep Alive from seconds to milliseconds
        uint32_t keepalive_ms = static_cast<uint32_t>(_mqttClient->keepalive) * 1000;

        // Check if client has been silent for more than 1.5× keepalive
        if (now_ms - _last_activity_time > keepalive_ms + (keepalive_ms >> 1)) {
            ESP_LOGW(TAG, "Client %s timed out (no MQTT Control packets received)", _mqttClient->clientId.c_str());
            // ESP_LOGW(TAG, "Client timed out (no MQTT Control packets received)");
            detach(true);
            return ERR_ABRT;
        }
    }

    // try to send any queued messages
    // _runQueueLWIP();
    // ESP_LOGD(TAG, "LWIP Poll %p", tpcb);
    if (_responseQueue.size()) {
        _runQueue();
    }
    return ERR_OK;
}

err_t MqttClientHandler::lwip_sent_cb(struct tcp_pcb* tpcb, u16_t len) {
    // update inflight bytes
    // ESP_LOGD(TAG, "got ack: %p", tpcb);
    _last_activity_time = millis();
    _onAck(len);
    return ERR_OK;
}

} //namespace asyncbroker
} //namespace esphome