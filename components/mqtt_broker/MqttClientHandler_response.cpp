#include "AsyncBroker.h"
#include "esphome/core/application.h"
#include "lwip_callback.h"

namespace esphome {
namespace asyncbroker {

bool MqttClientHandler::_queueMessage(const uint8_t* data, size_t len) {
    if (_responseQueue.full()) {
        ESP_LOGW(TAG, "Response queue full, discarding message");
        return false;
    }

    // Construct message in place
    MqttResponseMessage msg(data, len);
    _responseQueue.push(std::move(msg));

    // Attempt to send immediately if TCP buffer has space
    if (_responseQueue.size() < (MAX_EVENTS_PER_CLIENT >> 2)) {
        tcpip_callback([](void* arg){
            auto self = static_cast<MqttClientHandler*>(arg);
            self->_runQueue();
        }, this);
    }
    return true;
}

bool MqttClientHandler::_queueMessage(MqttResponse_SharedData_t &&msg) {
    if (_responseQueue.full()) {
        ESP_LOGW(TAG, "Response queue full, discarding message");
        return false;
    }

    _responseQueue.push(MqttResponseMessage(std::move(msg)));

    if (_responseQueue.size() < (MAX_EVENTS_PER_CLIENT >> 2)) {
        tcpip_callback([](void* arg){
            auto self = static_cast<MqttClientHandler*>(arg);
            self->_runQueue();
        }, this);
    }
    return true;
}

// Accepts lvalues (copy of shared_ptr, increments refcount)
bool MqttClientHandler::_queueMessage(const MqttResponse_SharedData_t &msg) {
    if (_responseQueue.full()) {
        ESP_LOGW(TAG, "Response queue full, discarding message");
        return false;
    }

    _responseQueue.push(msg);  // copy shared_ptr, safe
    if (_responseQueue.size() < (MAX_EVENTS_PER_CLIENT >> 2)) {
        tcpip_callback([](void* arg){
            auto self = static_cast<MqttClientHandler*>(arg);
            self->_runQueue();
        }, this);
    }
    return true;
}

void MqttClientHandler::_onAck(u16_t len) {
    // Adjust inflight counter
    if (len < _inflight) {
        _inflight -= len;
    } else {
        _inflight = 0;
    }

    // Process acknowledged messages
    while (len && _responseQueue.size()) {
        MqttResponseMessage &msg = _responseQueue.peek(0);
        len = msg.ack(len);

        if (msg.finished()) {
            _responseQueue.pop(); // remove fully acked message
        }
    }

    // Flush next batch
    if (_responseQueue.size()) {
        _runQueue();
    }
}

void MqttClientHandler::_runQueue() {
    if (!_pcb || _responseQueue.empty()) return;

    ESP_LOGD(TAG, "running runQueue: %p", _pcb);
    size_t total_bytes_written = 0;
    size_t idx = 0;
    size_t qsize = _responseQueue.size();

    while (idx < qsize) {
        MqttResponseMessage &msg = _responseQueue.peek(idx); // peek at item idx

        if (!msg.sent()) {
            size_t bytes_written = msg.write(_pcb);
            total_bytes_written += bytes_written;
            _inflight += bytes_written;

            if (bytes_written == 0 || _inflight > _max_inflight) {
                break; // stop flush if TCP buffer full or inflight limit reached
            }
            // ESP_LOGD(TAG, "sending message");
        }
        else{
            // ESP_LOGD(TAG, "already send");
        }

        ++idx;
    }

    if (total_bytes_written) {
        tcp_output(_pcb);
    }
}


} //namespace asyncbroker
} //namespace esphome