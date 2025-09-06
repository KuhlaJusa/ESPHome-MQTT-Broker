#include "AsyncBroker.h"
#include "esphome/core/application.h"
#include "lwip_callback.h"

namespace esphome {
namespace asyncbroker {

MqttClientManager::MqttClientManager(size_t pool_size) {
    clients_.reserve(pool_size);
    for (size_t i = 0; i < pool_size; ++i) {
        clients_.emplace_back(std::make_unique<MqttClient>());
    }
}

// Get existing client by ID or allocate a free one
MqttClient* MqttClientManager::get_client(const uint8_t* idData, size_t idLen) {
    MqttClient* free_slot = nullptr;

    for (auto& c : clients_) {
        // Exact match → return immediately
        if (c->clientId.size() == idLen &&
            memcmp(c->clientId.data(), idData, idLen) == 0) {
            return c.get();
        }
        // Remember first free slot
        else if (c->clientId.empty()) {
            free_slot = c.get();
            break;
        }
    }

    if (free_slot) {
        ESP_LOGW(TAG, "Allocating new client ID: %.*s", (int)idLen, idData);
        close_connection(free_slot->handler, free_slot);
        // std::lock_guard<std::recursive_mutex> lock(free_slot->_client_lock);
        // taskENTER_CRITICAL(&(free_slot->_lock_clients));
        free_slot->disconnect();
        free_slot->clientId.assign(reinterpret_cast<const char*>(idData), idLen);
        free_slot->subscribed_topics.reserve(16);
        // free_slot->published_topics.reserve(16);
        return free_slot;
    }

    ESP_LOGW("MqttClientManager", "Client pool exhausted");
    return nullptr;
}

// Optional: find client by handler
MqttClient* MqttClientManager::find_by_handler(MqttClientHandler* handler) {
    for (auto& c : clients_) {
        if (c->handler == handler) return c.get();
    }
    return nullptr;
}

// Optional: remove client (mark as unused)
void MqttClientManager::remove_client(MqttClient* client) {
    if (!client) return;
    client->disconnect();
}

void MqttClientManager::close_connection(MqttClientHandler* handler, MqttClient* client) {
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