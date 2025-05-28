import esphome.config_validation as cv
from esphome.components.esp32 import add_idf_component
import esphome.codegen as cg

from esphome.const import CONF_ID
from esphome.const import (
    CONF_DISABLED,
    CONF_ID,
    CONF_PORT,
    CONF_PROTOCOL,
    CONF_SERVICE,
    CONF_SERVICES,
    KEY_CORE,
    KEY_FRAMEWORK_VERSION,
)
from esphome.core import CORE, coroutine_with_priority


mqtt_broker_ns = cg.esphome_ns.namespace("mqtt_broker")
MQTTBroker = mqtt_broker_ns.class_("MQTTBroker", cg.Component)

CONFIG_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.declare_id(MQTTBroker),
})


async def to_code(config):

    if CORE.using_esp_idf and CORE.data[KEY_CORE][KEY_FRAMEWORK_VERSION] >= cv.Version(
        5, 1, 0
    ):
        add_idf_component(
            name="mosquitto",
            repo="https://github.com/espressif/esp-protocols.git",
            ref="mosq-v2.0.20_2",
            path="components/mosquitto",
        )
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
