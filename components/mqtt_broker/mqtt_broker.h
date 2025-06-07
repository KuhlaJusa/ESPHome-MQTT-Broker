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

  void add_trigger(MQTTMessageTrigger* trigger);
  static void handle_message(char *client, char *topic, char *payload, int len, int qos, int retain);
  static void start_broker(void* pvParameter);

  void add_on_publish_callback(std::function<void(std::string, std::string, std::string, uint8_t)> &&callback){
    this->callback_.add(std::move(callback));
  }
  void set_port(uint16_t port) { this->port_ = port; }
  // uint16_t get_port() {return this->port_;}
  void set_max_message_age(uint16_t milliseconds) { this->max_message_age_ms_ = milliseconds; }
  // uint32_t set_max_message_age() { return this->max_message_age_ms_; }
  void set_debug(bool debug) { this->debug_ = debug;}
  void enable_mqtt_callback(bool enable) { this->enable_callback_ = enable;}

 protected:
  uint16_t port_ = 1883;
  static MQTTBroker *global_instance_;
  TaskHandle_t mqtt_task_handle_ = nullptr;
  QueueHandle_t message_queue_ = nullptr;
  uint32_t max_message_age_ms_  = 1000;
  bool debug_ = false;
  bool enable_callback_ = false;
  std::vector<MQTTMessageTrigger*> triggers_;
  CallbackManager<void(std::string, std::string, std::string, uint8_t)> callback_;
};


class MQTTMessageTrigger : public Trigger<std::string>, public Component {
 public:
  explicit MQTTMessageTrigger(MQTTBroker* parent);

  void set_topic(const std::string &topic);
  void set_payload(const std::string &payload);
  void set_qos(uint8_t qos);

  void setup() override;
  void dump_config() override;
  float get_setup_priority() const override;

  const std::string& get_topic() const;
  const std::string& get_payload() const;
  bool has_payload() const;

 protected:
  MQTTBroker *parent_;
  optional<std::string> payload_;
  std::string topic_;
  uint8_t qos_{0};

  void on_message_(const std::string &client, const std::string &topic, 
                   const std::string &payload, uint8_t qos);
};

}  // namespace mqtt_broker
}  // namespace esphome