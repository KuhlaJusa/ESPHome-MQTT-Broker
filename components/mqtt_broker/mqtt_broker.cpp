#include "esphome/core/log.h"
#include "esphome/core/hal.h"

#include "mqtt_broker.h"
#include "mosq_broker.h"

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>


namespace esphome {
namespace mqtt_broker {

static const char *TAG = "mqtt_broker.component";
MQTTBroker *MQTTBroker::global_instance_ = nullptr;

void MQTTBroker::handle_message(char *client, char *topic, char *payload, int len, int qos, int retain){
  if (!global_instance_ || !global_instance_->message_queue_) return;

  //if debug set, skip automation
  if (global_instance_->debug_) {
    ESP_LOGD(TAG, "Message from \"%s\" on topic \"%s\" (%d bytes): %.*s", client, topic, len, len, payload);
    return;
  }

  // Check if an Automation want this topic, check for payload from the main task, 
  // to keep this task as non blocking as possible
  std::string topic_str(topic);
  bool any_match = false;
  for (auto* trig : global_instance_->triggers_) {
    if (trig->get_topic() == topic_str) {
      any_match = true;
      break;
    }
  }
  if (!any_match) {
    ESP_LOGV(TAG, "No Topic Match for Message (Automation)");
    return;  // Drop message early
  }

  MQTTMessage* msg = new MQTTMessage{
    std::string(client),
    std::string(topic),
    std::string(payload, len),
    static_cast<uint8_t>(qos),
    millis()
  };

  BaseType_t result = xQueueSend(global_instance_->message_queue_, &msg, 0);
  if (result != pdPASS) {
      // Queue full or send failed, clean up
      delete msg;
      ESP_LOGW(TAG, "Failed to enqueue MQTT Message (Automation)");
  }
}

void MQTTBroker::start_broker(void* pvParameter) {
  if (!global_instance_){
    ESP_LOGE(TAG, "MQTT_Broker instance unknown, can not start broker.");
    return;
  } 

  int broker_return = 0;

  char ip[] = "0.0.0.0"; // Listen on all interfaces
  struct mosq_broker_config config = {
      .host = ip,         
      .port = global_instance_->port_,
      .tls_cfg = NULL,    // No TLS
      .handle_message_cb = global_instance_->enable_callback_ ? handle_message : nullptr
      };

  ESP_LOGI(TAG, "Starting MQTT Broker on port: %d", config.port);
  broker_return = mosq_broker_run(&config);     //broker only returns from this function if stopped
  ESP_LOGI(TAG, "MQTT Broker closed with code: %d", broker_return);
}

void MQTTBroker::setup() {
  global_instance_ = this;

  if (enable_callback_)
    message_queue_ = xQueueCreate(10, sizeof(MQTTMessage*));

/*
The dokumentation of the broker: https://github.com/espressif/esp-protocols/tree/master/components/mosquitto
mentions a minimum stack size of 5kB. To be on the safe side, we use 8kB (4000 * 16bit stack width)
*/
#if defined(USE_ESP32_VARIANT_ESP32S3) || defined(USE_ESP32_VARIANT_ESP32)
  //if esp32 or esp32s3 run mqtt on second core (not tested!!!!!!!)
  xTaskCreatePinnedToCore(&MQTTBroker::start_broker, "MQTT_Task", 4000, nullptr, 1, &mqtt_task_handle_, 1);

#else 
  //single core esp, tested on esp32c6
  xTaskCreate(&MQTTBroker::start_broker, "MQTT_Task", 4000, this, 0, &mqtt_task_handle_);
#endif

}

void MQTTBroker::loop() {
  /*
  next lines only needed for stack size debugging
  */
  // static uint32_t last_stack_print_time = 0;
  // static uint32_t now;
  // now = millis();

  if (enable_callback_){
    MQTTMessage* msg = nullptr;

    // per MQTTBroker::loop call only process at maximum the full queue and not new added elements.
    for (int i = 0; i < max_queue_elements_; ++i) {
      if (xQueueReceive(message_queue_, &msg, 0) != pdTRUE) {
        break;  // No more messages to process
      }

      if (msg == nullptr) {
        ESP_LOGE(TAG, "Received null message pointer!");
        continue;  // Defensive check
      }

      if (millis() - msg->timestamp <= max_message_age_ms_) {
        callback_.call(msg->client, msg->topic, msg->payload, msg->qos);
      }

      delete msg;  // Free heap, as msg not needed anymore
    }
  }


  /*
  next lines only needed for stack size debugging
  */
  // if (now - last_stack_print_time > 5000) {
  //   ESP_LOGD("MQTTBroker", "MQTT Task stack high watermark: %d", uxTaskGetStackHighWaterMark(mqtt_task_handle));
  //   last_stack_print_time = now;
  // }
}

void MQTTBroker::dump_config(){
  ESP_LOGCONFIG(TAG, "MQTT Broker:");
  ESP_LOGCONFIG(TAG, "  Port: %u", this->port_);
  ESP_LOGCONFIG(TAG, "  Max Queue Elements: %u", this->max_queue_elements_);
  ESP_LOGCONFIG(TAG, "  Max Message Age (ms): %u", this->max_message_age_ms_);
  ESP_LOGCONFIG(TAG, "  Debug Logging: %s", this->debug_ ? "ENABLED" : "DISABLED");
  ESP_LOGCONFIG(TAG, "  Callbacks Enabled: %s", this->enable_callback_ ? "YES" : "NO");
  ESP_LOGCONFIG(TAG, "  Number of Triggers: %u", static_cast<unsigned int>(this->triggers_.size()));
}

float MQTTBroker::get_setup_priority() const { return setup_priority::AFTER_CONNECTION; }

void MQTTBroker::add_trigger(MQTTMessageTrigger* trigger) {
  this->triggers_.push_back(trigger);
}


// MQTTMessageTrigger
MQTTMessageTrigger::MQTTMessageTrigger(MQTTBroker* parent) : parent_(parent) {}

void MQTTMessageTrigger::set_qos(uint8_t qos) { this->qos_ = qos; }
void MQTTMessageTrigger::set_topic(const std::string &topic) { this->topic_ = topic; }
void MQTTMessageTrigger::set_payload(const std::string &payload) { this->payload_ = payload; }

const std::string& MQTTMessageTrigger::get_topic() const { return topic_; }
const std::string& MQTTMessageTrigger::get_payload() const {
  // It's caller's responsibility to ensure payload_ has a value before calling this!
  return *payload_;
}
bool MQTTMessageTrigger::has_payload() const { return payload_.has_value(); }

void MQTTMessageTrigger::setup() {
  parent_->add_trigger(this);
  parent_->add_on_publish_callback(
    [this]
    (const std::string &client, const std::string &topic, const std::string &payload, uint8_t qos) 
    {this->on_message_(client, topic, payload, qos);});
}

void MQTTMessageTrigger::dump_config() {
  ESP_LOGCONFIG(TAG, "MQTT Message Trigger:");
  ESP_LOGCONFIG(TAG, "  Topic: '%s'", this->topic_.c_str());
  // ESP_LOGCONFIG(TAG, "  QoS: %u", this->qos_);
  if (this->payload_.has_value()) {
    ESP_LOGCONFIG(TAG, "  Payload: '%s'", this->payload_->c_str());
  } else {
    ESP_LOGCONFIG(TAG, "  Payload: <any>");
  }
}

float MQTTMessageTrigger::get_setup_priority() const { return setup_priority::AFTER_CONNECTION - 1; }

void MQTTMessageTrigger::on_message_(const std::string &client, const std::string &topic, const std::string &payload, uint8_t qos){
  if (this->payload_.has_value() && payload != *this->payload_) {
    return;
  }
  if (topic != this->topic_) {
    return;
  }

  this->trigger(payload);
}

}  // namespace mqtt_broker
}  // namespace esphome