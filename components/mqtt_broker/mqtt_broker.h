#pragma once

#include "esphome/core/component.h"



namespace esphome {
namespace mqtt_broker {

class MQTTBroker : public Component {
public:
  void setup() override;
  void loop() override;
  void dump_config() override;

};

}  // namespace mqtt_broker
}  // namespace esphome