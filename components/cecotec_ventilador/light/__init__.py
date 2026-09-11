"""Plataforma `light` OPCIONAL, y desaconsejada. Lee esto antes de usarla.

El mando del ventilador **nunca informa de su estado**: no hay realimentacion
por ningun canal. Y `light_toggle` es un toggle ciego. Una entidad `light` tiene
estado (encendida/apagada, color), asi que en cuanto alguien use el mando fisico
lo que muestre Home Assistant sera mentira, y no hay forma de detectarlo.

Por eso la configuracion recomendada expone la luz como **botones**
(`light_toggle`, `light_cold`, `light_neutral`, `light_warm`): un boton no
promete un estado que no se puede conocer. Ver el README.

Esta plataforma se conserva por dos razones:

1. Quien prefiera una entidad con estado optimista puede tenerla.
2. Es la unica via para investigar si el receptor acepta **niveles de luz
   intermedios** (con `use_bank: false`, que genera la trama por campos en vez
   de emitir una captura real). Hoy eso NO funciona, ver el README: el receptor
   rechaza las tramas generadas.

Con `use_bank: true` (por defecto) la entidad elige el preset capturado mas
cercano (`light_cold` = `ff 00`, `light_neutral` = `ff ff`, `light_warm` =
`00 ff`), que es lo que `ColorMode::COLD_WARM_WHITE` entrega en
`current_values_as_cwww()`.
"""

import esphome.codegen as cg
from esphome.components import light
import esphome.config_validation as cv
from esphome.const import CONF_OUTPUT_ID

# COLD_WARM_WHITE no es un LightType del codegen (solo hay BINARY,
# BRIGHTNESS_ONLY, RGB y ADDRESSABLE); el modo de color lo declara el C++ en
# get_traits(). El componente `cwww` oficial hace exactamente lo mismo: usa
# RGB_LIGHT_SCHEMA y declara COLD_WARM_WHITE en sus traits.

from .. import CONF_CECOTEC_ID, cecotec_child_schema, cecotec_ns

DEPENDENCIES = ["cecotec_ventilador"]

CecotecLight = cecotec_ns.class_("CecotecLight", light.LightOutput, cg.Component)

CONF_COLD_WHITE_TEMPERATURE = "cold_white_temperature"
CONF_WARM_WHITE_TEMPERATURE = "warm_white_temperature"
CONF_CONSTANT_BRIGHTNESS = "constant_brightness"
CONF_ABSOLUTE_OFF = "absolute_off"

CONFIG_SCHEMA = (
    light.light_schema(CecotecLight, light.LightType.RGB)
    .extend(
        {
            cv.Optional(CONF_COLD_WHITE_TEMPERATURE, default="153 mireds"): cv.color_temperature,
            cv.Optional(CONF_WARM_WHITE_TEMPERATURE, default="500 mireds"): cv.color_temperature,
            cv.Optional(CONF_CONSTANT_BRIGHTNESS, default=False): cv.boolean,
            # una pregunta abierta: se busca un apagado absoluto (parametro `00 00` con
            # la familia de color) en vez de `light_toggle`, que es un toggle y
            # desincroniza el estado en cuanto alguien usa el mando fisico.
            # Pendiente de medir contra el ventilador.
            cv.Optional(CONF_ABSOLUTE_OFF, default=True): cv.boolean,
        }
    )
    .extend(cecotec_child_schema())
    .extend(cv.COMPONENT_SCHEMA)
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_OUTPUT_ID])
    await cg.register_component(var, config)
    await light.register_light(var, config)
    cg.add(var.set_parent(await cg.get_variable(config[CONF_CECOTEC_ID])))
    cg.add(var.set_cold_white_temperature(config[CONF_COLD_WHITE_TEMPERATURE]))
    cg.add(var.set_warm_white_temperature(config[CONF_WARM_WHITE_TEMPERATURE]))
    cg.add(var.set_constant_brightness(config[CONF_CONSTANT_BRIGHTNESS]))
    cg.add(var.set_absolute_off(config[CONF_ABSOLUTE_OFF]))
