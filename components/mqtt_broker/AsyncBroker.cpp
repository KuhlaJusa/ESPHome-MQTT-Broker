#include "AsyncBroker.h"
#include "esphome/core/application.h"
#include "lwip_callback.h"
// #include "MqttClientHandler.h"

namespace esphome {
namespace asyncbroker {

static void start_broker_cb(void *arg) {
    if(!arg) return;
    AsyncBroker *broker = static_cast<AsyncBroker*>(arg);
    broker->start_broker();
}

static void stop_broker_cb(void *arg) {
    AsyncBroker *broker = static_cast<AsyncBroker*>(arg);
    broker->stop_broker();
}

void AsyncBroker::start_broker(){
    _listen_pcb = tcp_new();
    if (!_listen_pcb) {
        ESP_LOGE("AsyncBroker", "Failed to allocate tcp_pcb");
        return;
    }

    if (tcp_bind(_listen_pcb, IP_ADDR_ANY, _port) != ERR_OK) {
        tcp_close(_listen_pcb);
        _listen_pcb = nullptr;
        ESP_LOGE("AsyncBroker", "Failed to bind port %d", _port);
        return;
    }

    _listen_pcb = tcp_listen(_listen_pcb);
    tcp_accept(_listen_pcb, &lwip_accept_cb_static);
    tcp_arg(_listen_pcb, this);

    ESP_LOGI("AsyncBroker", "MqttBroker started on port: %d", _port);

    #if CONFIG_FREERTOS_UNICORE
        ESP_LOGW(TAG, "Running on single core system");
        const BaseType_t coreID = 0;
    #else
        ESP_LOGW(TAG, "Running on dual core system");
        const BaseType_t coreID = 1;  // or 0, depending on your load balancing
    #endif
    xTaskCreatePinnedToCore(
        [](void *arg) {
            static_cast<AsyncBroker*>(arg)->broker_task();
            vTaskDelete(nullptr);  // delete task if it ever exits
        },
        "broker_task",          // Task name
        4096,                   // Stack size in bytes
        this,                   // Task argument
        5,                      // Task priority
        &_broker_task_handle,   // Task handle
        coreID                       // Core ID (0 or 1)
    );
    handlerManager_.set_broker_task(_broker_task_handle);
    ESP_LOGI(TAG, "task_handle: %p, on core: %d", _broker_task_handle, coreID);
}

void AsyncBroker::stop_broker(){
    if (_listen_pcb) {
        tcp_arg(_listen_pcb, nullptr);
        tcp_accept(_listen_pcb, nullptr);
        tcp_close(_listen_pcb);
        _listen_pcb = nullptr;
    }
}

bool AsyncBroker::start() {
    // Tries to queue, returns immediately if queue is full
    err_t res = tcpip_try_callback(start_broker_cb, this);
    if (res != ERR_OK) {
        ESP_LOGE(TAG, "Failed to queue broker start (err=%d)", res);
        return false;
    }
    ESP_LOGE(TAG, "callback pointer: %p", publish_cb_);
    return true;
}

bool AsyncBroker::stop() {
    err_t res = tcpip_callback(stop_broker_cb, this);
    if (res != ERR_OK) {
        ESP_LOGE("AsyncBroker", "Failed to stop broker (err=%d)", res);
        return false;
    }
    return true;
}


err_t AsyncBroker::OnConnect(struct tcp_pcb* newpcb, err_t err){
    if (newpcb == nullptr || err != ERR_OK) return err;

    dump_active_pcbs();
    auto* handler = this->handlerManager_.get_free_handler();
    if(handler)
        handler->attach(newpcb);
    return err;
}


void AsyncBroker::broker_task() {
    ESP_LOGI("AsyncBroker", "Broker task running on core 1");

    auto it = handlerManager_._handlers.begin(); // round-robin iterator

    while (true) {
        // Wait until any notification
        uint32_t events = ulTaskNotifyTake(pdFALSE, portMAX_DELAY);
        ESP_LOGI(TAG, "broker_task woke up, events: %u", events);
        // ESP_LOGI(TAG, "handlers: %u", handlerManager_._handlers.size());
        size_t processed = 0;

        while (processed < events && !handlerManager_._handlers.empty()) {
            size_t handlers_checked = 0;
            bool did_process = false;

            // Make one round over all handlers
            while (handlers_checked < handlerManager_._handlers.size()) {
                if (it == handlerManager_._handlers.end()) {
                    it = handlerManager_._handlers.begin();
                }

                auto* h = *it;
                ++it; // advance iterator
                handlers_checked++;

                MqttEvent* ev = nullptr;
                // ESP_LOGI(TAG, "bevore connect", events);
                ESP_LOGI(TAG, "handler id: %d, connected: %d, queue spaces: %d", h->_handlerId, h->connected(), uxQueueSpacesAvailable(h->rxQueue));
                // if (xQueueReceive(h->rxQueue, &ev, 0) == pdTRUE &&
                //     ev) {
                if (h->connected() &&
                    xQueueReceive(h->rxQueue, &ev, 0) == pdTRUE &&
                    ev) {

                    parse_event(h, ev);
                    processed++;
                    did_process = true;
                    vTaskDelay(pdMS_TO_TICKS(20));
                    ESP_LOGW(TAG, "broker_task processed event, total processed: %u", processed);
                    break; // stop current round, resume next iteration
                }
            }

            // If no handler had events in this full round, exit
            if (!did_process) {
                break;
            }
        }

        // processed < events → some notifications were stale; that's OK
    }
}



void AsyncBroker::parse_event(MqttClientHandler* handler, MqttEvent* ev){
    std::unique_ptr<MqttEvent> ev_ptr(ev);
    if (handler->valid_link() == false) {
        ESP_LOGW(TAG, "Invalid Client/Stub link, or Second Connect request over same Connection. Closing Connection..");
        handlerManager_.close_connection(handler, handler->_mqttClient);
        return;
    }
    //handle event
    ESP_LOGI(TAG, "Got event: type=%u flags=%u len=%u",
                ev->packetType, ev->flags,
                static_cast<unsigned>(ev->payload.size()));
    // ESP_LOGD("mqtt", "Payload HEX: %s", to_hex_string(ev->payload).c_str());
    switch (ev->packetType) 
    {
        case 1: handleConnect(handler, ev); break;
        case 3: handlePublish(handler, ev); break;
        case 8: 
                handleSubscribe(handler, ev); 
            break;
        case 10: 
            if (ev->flags == 2)
                handleUnsubscribe(handler, ev); 
            else   //if flags not exactly two, Server MUST close connection
                handlerManager_.close_connection(handler, handler->_mqttClient);
            break;
        case 12: handlePingreq(handler); break;
        
        // case 14: handleDisconnect(s); break; //case 14 (disconnect) is catched earlier in lwip directly
        default: break;
    }
}

} //namespace asyncbroker
} //namespace esphome