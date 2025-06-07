#include "esphome/core/log.h"
#include "esphome/core/hal.h"

#include "mqtt_broker.h"
#include "mosq_broker_wrapper.h"

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>


namespace esphome {
namespace mqtt_broker {

static const char *TAG = "mqtt_broker.component";
MQTTBroker *MQTTBroker::global_instance_ = nullptr;

void MQTTBroker::handle_message(char *client, char *topic, char *payload, int len, int qos, int retain){
  if (!global_instance_ || !global_instance_->message_queue_) return;

  //Check if an Automation want this topic
  std::string topic_str(topic);
  bool any_match = false;
  for (auto* trig : global_instance_->triggers_) {
    if (trig->get_topic() == topic_str &&
        (!trig->has_payload() || trig->get_payload() == payload)) {
      any_match = true;
      break;
    }
  }
  if (!any_match) {
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
      ESP_LOGW(TAG, "Failed to enqueue MQTT message;");
  }
}

void MQTTBroker::start_broker(void* pvParameter) {
  int broker_return = 0;

  char ip[] = "0.0.0.0"; // Listen on all interfaces
  struct mosq_broker_config config = {
      .host = ip,         
      .port = 1883,       // Standard MQTT port
      .tls_cfg = NULL,    // No TLS in this example
      .handle_message_cb = handle_message
      };

  mosq_broker_run(&config);
  ESP_LOGD(TAG, "MQTT Broker exit");
}

void MQTTBroker::setup() {
  global_instance_ = this;

  // recommended stack size of minimum ~5kB 
  // we take 8 to be safe
  message_queue_ = xQueueCreate(10, sizeof(MQTTMessage*));
  xTaskCreate(&MQTTBroker::start_broker, "MQTTTask", 4000, this, 0, &mqtt_task_handle);
}

void MQTTBroker::loop() {
  static uint32_t last_stack_print_time = 0;
  static uint32_t now;

  now = millis();

  MQTTMessage* msg = nullptr;
  while (xQueueReceive(message_queue_, &msg, 0) == pdTRUE) {
    if (msg == nullptr) {
      ESP_LOGE(TAG, "Received null message pointer!");
      continue;  // Defensive check
    }

    if (millis() - msg->timestamp <= MAX_MESSAGE_AGE_MS) {
      callback_.call(msg->client, msg->topic, msg->payload, msg->qos);
    }
    delete msg;  // Free heap
  }

  // if (now - last_stack_print_time > 5000) {
  //   ESP_LOGD("MQTTBroker", "MQTT Task stack high watermark: %d", uxTaskGetStackHighWaterMark(mqtt_task_handle));
  //   last_stack_print_time = now;
  // }
}

void MQTTBroker::dump_config(){
  ESP_LOGCONFIG(TAG, "MQTT Broker");
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
  ESP_LOGCONFIG(TAG, "  QoS: %u", this->qos_);
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