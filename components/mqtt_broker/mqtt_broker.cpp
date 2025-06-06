#include "esphome/core/log.h"
#include "esphome/core/hal.h"

#include "mqtt_broker.h"
// #include "broker_manager.h"

#include "mosq_broker_wrapper.h"

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

// #include "esphome/components/json/json_util.h"
// #include "esphome/components/network/util.h"
// #include "esphome/core/application.h"
// #include "esphome/core/entity_base.h"
// #include "esphome/core/helpers.h"
// #include "esphome/core/log.h"
// #include "esphome/core/util.h"


// #include <fcntl.h>
// #include <stdio.h>

// #ifdef __cplusplus
//   extern "C" int mosq_broker_run(mosq_broker_config*);
// #endif

namespace esphome {
namespace mqtt_broker {

static const char *TAG = "mqtt_broker.component";
MQTTBroker *MQTTBroker::global_instance_ = nullptr;

void MQTTBroker::handle_message(char *client, char *topic, char *payload, int len, int qos, int retain){
        // if (client && strcmp(client, "local_mqtt") == 0 ) {
        //     // This is our little local client -- do not forward
        //     return;
        // }
        ESP_LOGI(TAG, "handle_message topic:%s", topic);
        ESP_LOGI(TAG, "handle_message data:%.*s", len, payload);
        ESP_LOGI(TAG, "handle_message qos=%d, retain=%d", qos, retain);
        if (global_instance_ != nullptr) {
          global_instance_->callback_.call(std::string(topic), std::string(payload));
        }
        // if (s_local_mqtt && s_agent) {
        //     int topic_len = strlen(topic) + 1; // null term
        //     int topic_len_aligned = ALIGN(topic_len);
        //     int total_msg_len = 2 + 2 /* msg_wrap header */ + topic_len_aligned + len;
        //     if (total_msg_len > MAX_BUFFER_SIZE) {
        //         ESP_LOGE(TAG, "Fail to forward, message too long");
        //         return;
        //     }
        //     message_wrap_t *message = (message_wrap_t *)s_buffer;
        //     message->topic_len = topic_len;
        //     message->data_len = len;

        //     memcpy(s_buffer + 4, topic, topic_len);
        //     memcpy(s_buffer + 4 + topic_len_aligned, payload, len);
        //     juice_send(s_agent, s_buffer, total_msg_len);
        // }
    }

void MQTTBroker::start_broker(void* pvParameter) {
    int broker_return = 0;

    char ip[] = "0.0.0.0";
    struct mosq_broker_config config = {
        .host = ip,  // Listen on all interfaces
        .port = 1883,       // Standard MQTT port
        .tls_cfg = NULL,     // No TLS in this example
        .handle_message_cb = handle_message
        };

    mosq_broker_run(&config);
    // uxTaskGetStackHighWaterMark(NULL)
    ESP_LOGD(TAG, "MQTT Broker exit with: %d", broker_return);
}

void MQTTBroker::setup() {
  
  // recommended stack size of minimum ~5kB 
  // we take 8 to be safe
  MQTTBroker::global_instance_ = this;
  xTaskCreate(&MQTTBroker::start_broker, "loopTask", 4000, this, 0, &mqtt_task_handle);
}

void MQTTBroker::loop() {
  static uint32_t last_stack_print_time = 0;
  static uint32_t now;
  now = millis();

  if (now  > last_stack_print_time + 5000){
    ESP_LOGD(TAG, "Maximum Used Stack by MQTT Task: %d", 
      uxTaskGetStackHighWaterMark(mqtt_task_handle));
      last_stack_print_time = now;
  }
  // Tasks here will be performed at every call of the main application loop.
  // Note: code here MUST NOT BLOCK (see below)
}

void MQTTBroker::dump_config(){
  ESP_LOGCONFIG(TAG, "MQTT Broker");
}

float MQTTBroker::get_setup_priority() const { return setup_priority::WIFI - 2.0f; }

}  // namespace mqtt_broker
}  // namespace esphome