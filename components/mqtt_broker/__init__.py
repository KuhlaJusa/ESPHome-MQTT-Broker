import esphome.config_validation as cv
from esphome.components.esp32 import add_idf_component, add_idf_sdkconfig_option
import esphome.codegen as cg
from esphome import automation
from esphome.core import CORE, coroutine_with_priority

from esphome.const import (
    CONF_ID,
    KEY_CORE,
    KEY_FRAMEWORK_VERSION,
    CONF_ENABLE_IPV6,
    CONF_TRIGGER_ID,
    CONF_ON_MESSAGE,
    CONF_TOPIC,
    # CONF_QOS,
    CONF_PAYLOAD,
    CONF_PORT,
    CONF_DEBUG

)

MIN_IDF_VERSION = (5, 1, 0)
CONF_ON_MESSAGE_MAX_AGE = "on_message_max_age"
CONF_ON_MAX_MESSAGES_IN_QUEUE = "max_queue_elements"

mqtt_broker_ns = cg.esphome_ns.namespace("mqtt_broker")
MQTTBroker = mqtt_broker_ns.class_("MQTTBroker", cg.Component)


MQTTMessageTrigger = mqtt_broker_ns.class_(
    "MQTTMessageTrigger", automation.Trigger.template(cg.std_string), cg.Component
)

CONFIG_SCHEMA = cv.All(
    cv.Schema({
        cv.GenerateID(): cv.declare_id(MQTTBroker),
        cv.Optional(CONF_PORT, default=1883): cv.port,
        cv.Optional(CONF_DEBUG, default=False): cv.boolean,
        cv.Optional(CONF_ON_MAX_MESSAGES_IN_QUEUE, default=10): cv.int_range(0, 65535),
        cv.Optional(CONF_ON_MESSAGE_MAX_AGE, default="1000ms"): cv.Any(
            cv.positive_time_period_milliseconds,
            "never",
        ),
        cv.Optional(CONF_ON_MESSAGE): automation.validate_automation(
            {
                cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(MQTTMessageTrigger),
                cv.Required(CONF_TOPIC): cv.subscribe_topic,
                # cv.Optional(CONF_QOS, default=0): cv.mqtt_qos,
                cv.Optional(CONF_PAYLOAD): cv.string_strict,
            },
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


@coroutine_with_priority(40.0)
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
    await cg.register_component(var, config)

    cg.add(var.set_port(config[CONF_PORT]))
    cg.add(var.set_max_queue_elements(config[CONF_ON_MAX_MESSAGES_IN_QUEUE]))
    interval = config[CONF_ON_MESSAGE_MAX_AGE]
    if interval == "never":
        interval = 2**32 - 1
    cg.add(var.set_max_message_age(interval))

    cg.add(var.set_debug(config[CONF_DEBUG]))
    if config[CONF_DEBUG] or config.get(CONF_ON_MESSAGE) is not None:
        cg.add(var.enable_mqtt_callback(True))

    # initialize topic trigger
    for conf in config.get(CONF_ON_MESSAGE, []):
        trig = cg.new_Pvariable(conf[CONF_TRIGGER_ID], var)
        # cg.add(trig.set_qos(conf[CONF_QOS]))
        cg.add(trig.set_topic(conf[CONF_TOPIC]))
        if CONF_PAYLOAD in conf:
            cg.add(trig.set_payload(conf[CONF_PAYLOAD]))
        await cg.register_component(trig, conf)
        await automation.build_automation(trig, [(cg.std_string, "x")], conf)

