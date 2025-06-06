import esphome.config_validation as cv
from esphome.components.esp32 import add_idf_component, add_idf_sdkconfig_option
import esphome.codegen as cg
from esphome import automation
from esphome.core import CORE, coroutine_with_priority

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
    CONF_TRIGGER_ID
)
# CONF_TRIGGER_ID
CONF_ON_PUBLISH = "on_publish"
CONF_TOPIC = "topic"
MIN_IDF_VERSION = (5, 1, 0)

mqtt_broker_ns = cg.esphome_ns.namespace("mqtt_broker")
MQTTBroker = mqtt_broker_ns.class_("MQTTBroker", cg.Component)


OnPublishTrigger = mqtt_broker_ns.class_(
    "OnPublishTrigger", automation.Trigger.template(cg.std_string), cg.Component
)

CONFIG_SCHEMA = cv.All(
    cv.Schema({
        cv.GenerateID(): cv.declare_id(MQTTBroker),
        cv.Optional(CONF_ON_PUBLISH): automation.validate_automation(
                {
                    cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(OnPublishTrigger),
                    cv.Optional(CONF_TOPIC): cv.templatable(cv.string_strict),
                },
                cv.has_at_least_one_key(CONF_TOPIC),
            ),
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
    #add esp idf default parameter, otherwise if eg. in combination with webserver socket connection limits.
    add_idf_sdkconfig_option("CONFIG_LWIP_MAX_ACTIVE_TCP", 16)

    var = cg.new_Pvariable(config[CONF_ID])

    # initialize topic trigger
    for conf in config.get(CONF_ON_PUBLISH, []):
        trigger = cg.new_Pvariable(conf[CONF_TRIGGER_ID], var)
        await cg.register_component(trigger, conf)
        # if (above := conf.get(CONF_TOPIC)) is not None:
        #     template_ = await cg.templatable(above, [(str, "x")], str)
        #     cg.add(trigger.set_min(template_))
        await automation.build_automation(trigger, [(cg.std_string, "x")], conf)


    await cg.register_component(var, config)
