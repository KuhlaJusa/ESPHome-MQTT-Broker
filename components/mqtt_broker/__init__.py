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

DEPENDENCIES = ["network"]

CONF_ON_MESSAGE_MAX_AGE = "on_message_max_age"
CONF_ON_MAX_MESSAGES_IN_QUEUE = "max_queue_elements"

mini_broker_ns = cg.esphome_ns.namespace("mqtt_broker")
MQTTBroker = mini_broker_ns.class_("MQTTBroker", cg.Component)


MQTTMessageTrigger = mini_broker_ns.class_(
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
            "infinite",
        ),
        cv.Optional(CONF_ON_MESSAGE): automation.validate_automation(
            {
                cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(MQTTMessageTrigger),
                cv.Required(CONF_TOPIC): cv.subscribe_topic,
                # cv.Optional(CONF_QOS, default=0): cv.mqtt_qos,
                cv.Optional(CONF_PAYLOAD): cv.string_strict,
            },
        ),
    })
)



@coroutine_with_priority(40.0)
async def to_code(config):

    # add_idf_sdkconfig_option("CONFIG_LWIP_MAX_ACTIVE_TCP", 16)

    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    cg.add(var.set_port(config[CONF_PORT]))
    cg.add(var.set_max_queue_elements(config[CONF_ON_MAX_MESSAGES_IN_QUEUE]))
    interval = config[CONF_ON_MESSAGE_MAX_AGE]
    if interval == "infinite":
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

