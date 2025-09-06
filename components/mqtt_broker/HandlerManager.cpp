#include "AsyncBroker.h"
#include "esphome/core/application.h"
#include "lwip_callback.h"

namespace esphome {
namespace asyncbroker {


void HandlerManager::_addHandler(MqttClientHandler *handler) {
  if (!handler) {
    return;
  }
// #ifdef ESP32
//   std::lock_guard<std::recursive_mutex> lock(_conHandler_queue_lock);
// #endif
//   taskENTER_CRITICAL(&_lock);
  _connected_handlers.emplace_back(handler);
    // ESP_LOGD(TAG, "Handler added to active list");
  _adjust_inflight_window();
}

void HandlerManager::_handleDisconnect(MqttClientHandler *handler) {
// #ifdef ESP32
//   std::lock_guard<std::recursive_mutex> lock(_conHandler_queue_lock);
// #endif
  for (auto i = _connected_handlers.begin(); i != _connected_handlers.end(); ++i) {
    if (*i == handler) {
      _connected_handlers.erase(i);
      // ESP_LOGD(TAG, "Handler removed from active list");
      break;
    }
  }
  _adjust_inflight_window();
}

void HandlerManager::_adjust_inflight_window() {
  if (_connected_handlers.size()) {
    size_t inflight = RESPONSE_MAX_INFLIGH / _connected_handlers.size();
    for (const auto &h : _connected_handlers) {
      h->set_max_inflight_bytes(inflight);
    }
    // Serial.printf("adjusted inflight to: %u\n", inflight);
  }
}

MqttClientHandler* HandlerManager::get_free_handler() {
    // Look for an inactive handler to reuse
    for (auto* h : _handlers) {
        if (!h->connected()) {   // check if it holds an asyncClient
            ESP_LOGV("AsyncBroker", "Reusing handler");
            // Reset values
            // h->detach();
            return h;
        }
    }

    // No free one, create new
    ESP_LOGD("AsyncBroker", "Create new handler");
    auto* h = new MqttClientHandler(*this, _handlers.size());
    _handlers.push_back(h);
    return h;
}


void HandlerManager::close_connection(MqttClientHandler* handler, MqttClient* client) {
    struct Args {
        MqttClientHandler* handler;
        MqttClient* client;
    };
    // If the client is attached to a handler, call close on it
    if (handler) {
        auto* args = new Args{handler, client};
        tcpip_callback([](void* arg){
            struct Args {
                MqttClientHandler* handler;
                MqttClient* client;
            };
            auto a = static_cast<Args*>(arg);
            a->handler->close(a->client);
        }, args);
    }
}


    
} //namespace asyncbroker
} //namespace esphome