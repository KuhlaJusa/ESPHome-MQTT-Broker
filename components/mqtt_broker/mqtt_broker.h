#pragma once

#include "esphome/core/component.h"
#include "esphome/core/automation.h"
#include "esphome/core/helpers.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "esphome/core/log.h"



namespace esphome {
namespace mqtt_broker {

class MQTTMessageTrigger;

struct MQTTMessage {
  std::string client;
  std::string topic;
  std::string payload;
  uint8_t qos;
  uint32_t timestamp;  // in milliseconds (from millis())
};


class MQTTBroker : public Component {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override;

  // Adds a message trigger that listens for specific topic/payload combinations.
  void add_trigger(MQTTMessageTrigger* trigger);

  // Static handler function called when a message is received by the broker.
  static void handle_message(char *client, char *topic, char *payload, int len, int qos, int retain);

  // Static entry point function to start the MQTT broker as a FreeRTOS task.
  static void start_broker(void* pvParameter);

  void add_on_publish_callback(std::function<void(std::string, std::string, std::string, uint8_t)> &&callback){
    this->callback_.add(std::move(callback));
  }

  void set_port(uint16_t port) { this->port_ = port; }

  // Sets the maximum age (in milliseconds) for messages that are received from the queue before being discarded.
  void set_max_message_age(uint16_t milliseconds) { this->max_message_age_ms_ = milliseconds; }

  // if debug enabled, ignore triggers, and print every received message.
  void set_debug(bool debug) { this->debug_ = debug;}

  // enable disable broker callback on received message.
  void enable_mqtt_callback(bool enable) { this->enable_callback_ = enable;}

  // Sets the maximum number of messages the internal queue can hold.
  void set_max_queue_elements(uint16_t elements) { this->max_queue_elements_ = elements;}

 protected:

  // Port on which the MQTT broker will listen (default 1883).
  uint16_t port_ = 1883;

  // Singleton global instance pointer (used by static methods like handle_message).
  static MQTTBroker *global_instance_;

  // Handle to the FreeRTOS task running the MQTT broker loop.
  TaskHandle_t mqtt_task_handle_ = nullptr;

  // Queue used to store incoming topic filtered MQTT messages for processing.
  QueueHandle_t message_queue_ = nullptr;

  // Maximum time (in ms) a message is considered as valid. If a messages if pulled from the queue its age will be checked.
  uint32_t max_message_age_ms_  = 1000;

  // Maximum number of messages the queue can hold at one time.
  uint16_t max_queue_elements_ = 10;

  // Enables printing of each message in the broker callback function
  bool debug_ = false;

  // Determines if broker enable callback on messages.
  bool enable_callback_ = false;

  // List of MQTTMessageTrigger objects that define rules for triggering actions on messages.
  std::vector<MQTTMessageTrigger*> triggers_;

  // Internal callback manager to handle user-defined publish callbacks.
  CallbackManager<void(std::string, std::string, std::string, uint8_t)> callback_;
};


class MQTTMessageTrigger : public Trigger<std::string>, public Component {
 public:
  // Constructor that takes a pointer to the parent MQTTBroker to register with.
  explicit MQTTMessageTrigger(MQTTBroker* parent);

  void set_topic(const std::string &topic);
  void set_payload(const std::string &payload);
  void set_qos(uint8_t qos);

  // Called once during initialization to register the trigger with the broker.
  void setup() override;

  // Logs this trigger's configuration (topic, payload match, etc.).
  void dump_config() override;

  // Setup priority to ensure it runs after the broker has been initialized.
  float get_setup_priority() const override;

  const std::string& get_topic() const;

  // Gets the payload string if one has been set (unsafe if has_payload() == false).
  const std::string& get_payload() const;
  bool has_payload() const;

 protected:
 // Pointer to the parent MQTTBroker this trigger is registered with.
  MQTTBroker *parent_;

  // Optional payload match value; if set, only matching payloads trigger.
  optional<std::string> payload_;

  // MQTT topic match for triggering.
  std::string topic_;

  // Quality of Service (QoS) level for MQTT subscription (usually 0 or 1). (not used)
  uint8_t qos_{0};

  // Internal method called by the CallbackManager when a message is pulled from the queue.
  // Checks topic/payload match and fires the trigger if conditions are met.
  void on_message_(const std::string &client, const std::string &topic, 
                   const std::string &payload, uint8_t qos);
};

}  // namespace mqtt_broker
}  // namespace esphome