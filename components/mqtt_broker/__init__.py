import esphome.config_validation as cv
from esphome.components.esp32 import add_idf_component, add_idf_sdkconfig_option
import esphome.codegen as cg

from esphome.const import (
    CONF_DISABLED,
    CONF_ID,
    CONF_PORT,
    CONF_PROTOCOL,
    CONF_SERVICE,
    CONF_SERVICES,
    KEY_CORE,
    KEY_FRAMEWORK_VERSION,
    CONF_ENABLE_IPV6,
)
from esphome.core import CORE, coroutine_with_priority

MIN_IDF_VERSION = (5, 1, 0)

mqtt_broker_ns = cg.esphome_ns.namespace("mqtt_broker")
MQTTBroker = mqtt_broker_ns.class_("MQTTBroker", cg.Component)

CONFIG_SCHEMA = cv.All(
    cv.Schema({
        cv.GenerateID(): cv.declare_id(MQTTBroker),
    }),
    cv.only_with_esp_idf,
    cv.require_framework_version(esp_idf=cv.Version(*MIN_IDF_VERSION)),
    cv.requires_component("network"),
)

def has_network_ipv6():
    core_config = CORE.config
    if (network_config := core_config.get("network", None)) is not None:
        if (enable_ipv6 := network_config.get(CONF_ENABLE_IPV6, None)) is not None:
            if enable_ipv6:
                return 
        raise cv.Invalid("Required 'enable_ipv6: true', in the Network component: https://esphome.io/components/network.html")
    else:
        raise cv.Invalid("Required the Network component: https://esphome.io/components/network.html")


@coroutine_with_priority(50.0)
async def to_code(config):
    has_network_ipv6()
    if CORE.using_esp_idf and CORE.data[KEY_CORE][KEY_FRAMEWORK_VERSION] >= cv.Version(
        *MIN_IDF_VERSION
    ):
        add_idf_component(
            name="mosquitto",
            repo="https://github.com/espressif/esp-protocols.git",
            ref="mosq-v2.0.20_2",
            path="components/mosquitto",
            submodules=["components/mosquitto/mosquitto"]
        )
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
