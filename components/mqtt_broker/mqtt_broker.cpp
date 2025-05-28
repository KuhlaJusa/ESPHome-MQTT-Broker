#include "esphome/core/log.h"
#include "mqtt_broker.h"

namespace esphome {
namespace mqtt_broker {

static const char *TAG = "mqtt_broker.component";

void ExampleComponent::setup() {
  // Code here should perform all component initialization,
  //  whether hardware, memory, or otherwise
}

void ExampleComponent::loop() {
  // Tasks here will be performed at every call of the main application loop.
  // Note: code here MUST NOT BLOCK (see below)
}

void ExampleComponent::dump_config(){
  ESP_LOGCONFIG(TAG, "MQTT Broker");
}

}  // namespace mqtt_broker
}  // namespace esphome