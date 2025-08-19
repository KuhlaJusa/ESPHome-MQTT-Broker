#include "esphome/core/log.h"
#include "esphome/core/hal.h"
#include "esphome/core/application.h"
#include "mqtt_broker.h"
// #include "mosq_broker.h"

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "minibroker.h"

namespace esphome {
namespace mqtt_broker {

static const char *TAG = "mqtt_broker.component";
MQTTBroker *MQTTBroker::global_instance_ = nullptr;


void MQTTBroker::mqtt_callback(const uint8_t* topic_ptr, size_t topic_len,
                      const uint8_t* payload_ptr, size_t payload_len) {

    if (!global_instance_ || !global_instance_->message_queue_) return;

    ESP_LOGI(TAG, "Received topic: %.*s, payload: %.*s",
             (int)topic_len, topic_ptr, (int)payload_len, payload_ptr);

    bool any_match = false;

    for (auto* trig : global_instance_->triggers_) {
        const std::string &t = trig->get_topic();

        // First check length, then memcmp (faster than constructing std::string)
        if (t.size() == topic_len && 
            std::memcmp(t.data(), topic_ptr, topic_len) == 0) {
            any_match = true;
            break;
        }
    }

  if (!any_match) {
      ESP_LOGVV(TAG, "No Topic Match for Message (Automation)");
      return;  // Drop message early
  }

  // Allocate new message (copy into std::string only once here)
  MQTTMessage* msg = new MQTTMessage{
      {},                        // copy client ID
      std::string(reinterpret_cast<const char*>(topic_ptr), topic_len),  // construct from pointer+len
      std::string(reinterpret_cast<const char*>(payload_ptr), payload_len), // same for payload
      0,
      millis()
  };

  // Enqueue message
  BaseType_t result = xQueueSend(global_instance_->message_queue_, &msg, 0);
  if (result != pdPASS) {
      // Queue full or send failed, clean up
      delete msg;
      ESP_LOGW(TAG, "Failed to enqueue MQTT Message (Automation)");
  }
}

void MQTTBroker::setup() {
  global_instance_ = this;
  broker_.set_port(port_);
  if (enable_callback_)
    broker_.set_publish_cb(mqtt_callback);
    message_queue_ = xQueueCreate(10, sizeof(MQTTMessage*));

  ESP_LOGI(TAG, "Starting MQTT broker on port %d", port_);
  broker_.begin();
}

void MQTTBroker::loop() {
  broker_.loop_iteration();
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
}

void MQTTBroker::dump_config(){
  ESP_LOGCONFIG(TAG, "MQTT Broker:");
  ESP_LOGCONFIG(TAG, "  Port: %u", this->port_);
  ESP_LOGCONFIG(TAG, "  Max Queue Elements: %u", this->max_queue_elements_);
  ESP_LOGCONFIG(TAG, "  Max Message Age (ms): %u", this->max_message_age_ms_);
  ESP_LOGCONFIG(TAG, "  Debug Logging: %s", this->debug_ ? "ENABLED" : "DISABLED");
  ESP_LOGCONFIG(TAG, "  Callbacks Enabled: %s", this->enable_callback_ ? "YES" : "NO");
  ESP_LOGCONFIG(TAG, "  Number of Triggers: %u", static_cast<unsigned int>(this->triggers_.size()));
  #if defined(USE_ESP32_VARIANT_ESP32S3) || defined(USE_ESP32_VARIANT_ESP32)
  ESP_LOGCONFIG(TAG, "  Running on Core 1");
  #endif
}

float MQTTBroker::get_setup_priority() const { return setup_priority::AFTER_CONNECTION - 1.0f; }

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
