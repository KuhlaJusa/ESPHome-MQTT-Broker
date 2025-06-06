#pragma once

#include "esphome/core/component.h"
#include "esphome/core/automation.h"
#include "esphome/core/helpers.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "esphome/core/log.h"



namespace esphome {
namespace mqtt_broker {

class MQTTBroker : public Component {
  public:
    void setup() override;
    void loop() override;
    void dump_config() override;
    float get_setup_priority() const override;
    static void handle_message(char *client, char *topic, char *payload, int len, int qos, int retain);
    static void start_broker(void* pvParameter);
    void add_on_publish_callback(std::function<void(std::string, std::string)> &&callback){
      this->callback_.add(std::move(callback));
    }

  protected:
    TaskHandle_t mqtt_task_handle = nullptr;
    CallbackManager<void(std::string, std::string)> callback_;
    static MQTTBroker *global_instance_;
};


class OnPublishTrigger : public Trigger<std::string>, public Component {
 public:
  explicit OnPublishTrigger(MQTTBroker* parent) : parent_(parent) {}

  void setup() override{
    this->parent_->add_on_publish_callback([this](const std::string &topic, const std::string &payload) { this->on_publish_(topic, payload); });
  }
  float get_setup_priority() const override { return setup_priority::HARDWARE; }

protected:
  void on_publish_(const std::string &topic, const std::string &payload) {
    // if (std::isnan(topic))
    //   return;
    ESP_LOGI("recive topic", "handle_message topic:%s", topic.c_str());
    ESP_LOGI("recive topic", "handle_message topic:%s", payload.c_str());
  //   float local_min = this->min_.value(state);
  //   float local_max = this->max_.value(state);

  //   bool in_range;
  //   if (std::isnan(local_min) && std::isnan(local_max)) {
  //     in_range = this->previous_in_range_;
  //   } else if (std::isnan(local_min)) {
  //     in_range = state <= local_max;
  //   } else if (std::isnan(local_max)) {
  //     in_range = state >= local_min;
  //   } else {
  //     in_range = local_min <= state && state <= local_max;
  //   }

  //   if (in_range != this->previous_in_range_ && in_range) {
      // this->trigger(state);
  //   }

  //   this->previous_in_range_ = in_range;
  //   this->rtc_.save(&in_range);
  }

  MQTTBroker *parent_;
};

}  // namespace mqtt_broker
}  // namespace esphome