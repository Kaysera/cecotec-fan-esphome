"""Entidad `fan`: on/off y velocidades 1-6, que es exactamente el mando."""

import esphome.codegen as cg
from esphome.components import fan
import esphome.config_validation as cv
from esphome.const import CONF_ID

from .. import CONF_CECOTEC_ID, CecotecHub, cecotec_child_schema, cecotec_ns

DEPENDENCIES = ["cecotec_ventilador"]

CecotecFan = cecotec_ns.class_("CecotecFan", fan.Fan, cg.Component)

CONFIG_SCHEMA = (
    fan.fan_schema(CecotecFan)
    .extend(cecotec_child_schema())
    .extend(cv.COMPONENT_SCHEMA)
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await fan.register_fan(var, config)
    cg.add(var.set_parent(await cg.get_variable(config[CONF_CECOTEC_ID])))
