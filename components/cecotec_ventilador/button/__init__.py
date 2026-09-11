"""Entidad `button` para los mandos que no encajan en `fan` ni en `light`.

`breeze`, `cold_warm` y `power_off` no son ni velocidad ni color, y
`light_toggle` es un toggle ciego. Los cuatro se exponen como botones, que es
honesto: no tienen estado que HA pueda conocer.
"""

import esphome.codegen as cg
from esphome.components import button
import esphome.config_validation as cv
from esphome.const import CONF_ID

from .. import CONF_CECOTEC_ID, cecotec_child_schema, cecotec_ns

DEPENDENCIES = ["cecotec_ventilador"]

CecotecButton = cecotec_ns.class_("CecotecButton", button.Button, cg.Component)

CONF_COMMAND = "command"

# Los 14 botones del mando. La tabla de campos y las capturas viven en
# protocol.h y la genera el generador offline (fuera de este repo).
COMMANDS = [
    "breeze",
    "cold_warm",
    "fan_1",
    "fan_2",
    "fan_3",
    "fan_4",
    "fan_5",
    "fan_6",
    "fan_off",
    "light_cold",
    "light_neutral",
    "light_toggle",
    "light_warm",
    "power_off",
]

CONFIG_SCHEMA = (
    button.button_schema(CecotecButton)
    .extend(
        {
            cv.Required(CONF_COMMAND): cv.one_of(*COMMANDS, lower=True),
        }
    )
    .extend(cecotec_child_schema())
    .extend(cv.COMPONENT_SCHEMA)
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await button.register_button(var, config)
    cg.add(var.set_parent(await cg.get_variable(config[CONF_CECOTEC_ID])))
    cg.add(var.set_command(config[CONF_COMMAND]))
