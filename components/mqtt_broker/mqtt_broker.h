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

  void add_on_publish_callback(std::function<void(std::string, std::string, std::string, uint8_t)> &&callback){
    this->callback_.add(std::move(callback));
  }

  void add_trigger(MQTTMessageTrigger* trigger);

  static void handle_message(char *client, char *topic, char *payload, int len, int qos, int retain);
  static void start_broker(void* pvParameter);

 protected:
  static MQTTBroker *global_instance_;
  TaskHandle_t mqtt_task_handle = nullptr;
  QueueHandle_t message_queue_ = nullptr;
  static constexpr uint32_t MAX_MESSAGE_AGE_MS = 1000;
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