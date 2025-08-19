#include "minibroker.h"
#include "esphome/core/application.h"

namespace esphome {
namespace mini_broker {

void MiniBroker::handleDisconnect(ClientStub* s){
    ESP_LOGV(TAG, "Got Disconnect Message, Client: %s", s->realClient->clientId.c_str());
    release_stub(s, false);
}

} // namespace mqtt_broker
} // namespace esphome