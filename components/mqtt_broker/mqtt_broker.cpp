#include "esphome/core/log.h"
#include "mqtt_broker.h"

#include "mosq_broker_wrapper.h"

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "esp_system.h"

// #ifdef __cplusplus
//   extern "C" int mosq_broker_run(mosq_broker_config*);
// #endif

namespace esphome {
namespace mqtt_broker {

static const char *TAG = "mqtt_broker.component";
void start_broker(void* pvParameter) {

  // Delay start to let Wi-Fi/lwIP stabilize
  vTaskDelay(pdMS_TO_TICKS(10000));
  char ip[] = "0.0.0.0";
  struct mosq_broker_config config = {
      .host = ip,  // Listen on all interfaces
      .port = 1883,       // Standard MQTT port
      .tls_cfg = NULL     // No TLS in this example
    };
  // Properly cast the void pointer to the correct type
  // struct mosq_broker_config *config = static_cast<struct mosq_broker_config*>(pvParameter);
  ESP_LOGD(TAG, "free memory: %d", esp_get_free_heap_size());
  mosq_broker_run(&config);
}

void MQTTBroker::setup() {
  TaskHandle_t loop_task_handle = nullptr;
  struct mosq_broker_config config = {
    .host = const_cast<char *>("0.0.0.0"),  // Listen on all interfaces
    .port = 1883,       // Standard MQTT port
    .tls_cfg = NULL     // No TLS in this example
  };
  
  // mosq_broker_run(&config);
  xTaskCreate(start_broker, "loopTask", 16000, &config, 1, &loop_task_handle);
}

void MQTTBroker::loop() {
  // Tasks here will be performed at every call of the main application loop.
  // Note: code here MUST NOT BLOCK (see below)
}

void MQTTBroker::dump_config(){
  ESP_LOGCONFIG(TAG, "MQTT Broker");
}

}  // namespace mqtt_broker
}  // namespace esphome