import esphome.config_validation as cv
import esphome.codegen as cg

from esphome.const import CONF_ID


mqtt_broker_ns = cg.esphome_ns.namespace("mqtt_broker")
MQTTBroker = mqtt_broker_ns.class_("MQTTBroker", cg.Component)

CONFIG_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.declare_id(MQTTBroker),
})


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])

    await cg.register_component(var, config)
