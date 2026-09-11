"""Hub del ventilador Cecotec: SX1280 mas generador de trama.

Ruta A1 de el README seccion 2: componente externo propio, con el driver y
el generador en C++ y las entidades como plataformas. Nada de lambdas en el
YAML, que se volvio insostenible en una version previa.
"""

import esphome.codegen as cg
from esphome.components import spi
import esphome.config_validation as cv
from esphome import pins
from esphome.const import CONF_FREQUENCY, CONF_ID

CODEOWNERS = ["@Kaysera"]
DEPENDENCIES = ["spi"]
MULTI_CONF = True

CONF_BUSY_PIN = "busy_pin"
CONF_RESET_PIN = "reset_pin"
CONF_DRY_RUN = "dry_run"
CONF_POWER = "power"
CONF_BLOCKS = "blocks"
CONF_PACKETS_PER_BLOCK = "packets_per_block"
CONF_INTERVAL = "interval"
CONF_GROUP = "group"
CONF_BLOCK_GAP = "block_gap"
CONF_COUNTER_START = "counter_start"
CONF_TEST_PRESS_INTERVAL = "test_press_interval"
CONF_USE_BANK = "use_bank"

cecotec_ns = cg.esphome_ns.namespace("cecotec_ventilador")
CecotecHub = cecotec_ns.class_("CecotecHub", cg.Component, spi.SPIDevice)

CONF_CECOTEC_ID = "cecotec_ventilador_id"

CONFIG_SCHEMA = (
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(CecotecHub),
            # el modo de prueba: las entidades existen y logean la trama que
            # emitirian, sin tocar el bus ni sacar RF. Por defecto activo, para
            # que un despiste no ponga a emitir una placa sin validar.
            cv.Optional(CONF_DRY_RUN, default=True): cv.boolean,
            cv.Optional(CONF_BUSY_PIN): pins.gpio_input_pin_schema,
            cv.Optional(CONF_RESET_PIN): pins.gpio_output_pin_schema,
            cv.Optional(CONF_FREQUENCY, default="2402MHz"): cv.All(
                cv.frequency, cv.int_range(min=2400000000, max=2500000000)
            ),
            cv.Optional(CONF_POWER, default=13): cv.int_range(min=-18, max=13),
            # La estructura real de una pulsacion del mando, medida en
            # el mando real.
            cv.Optional(CONF_BLOCKS, default=6): cv.int_range(min=1, max=32),
            cv.Optional(CONF_PACKETS_PER_BLOCK, default=80): cv.int_range(min=1, max=500),
            cv.Optional(CONF_INTERVAL, default="1161us"): cv.positive_time_period_microseconds,
            cv.Optional(CONF_GROUP, default=3): cv.int_range(min=1, max=32),
            cv.Optional(CONF_BLOCK_GAP, default="9000us"): cv.positive_time_period_microseconds,
            # Marca de procedencia de una iteracion de medida, y de paso evita
            # que dos arranques seguidos empiecen en el mismo valor.
            # `true` (por defecto): emitir las pulsaciones REALES capturadas,
            # rotando entre las 3 de cada boton. Es lo unico que el receptor
            # acepta hoy (el README, seccion "El resultado negativo que cambio el diseño"). `false`: generar por campos.
            cv.Optional(CONF_USE_BANK, default=True): cv.boolean,
            cv.Optional(CONF_COUNTER_START, default=0): cv.int_range(min=0, max=255),
            # 0 = desactivada. Solo para medir sin red; no va en el producto.
            cv.Optional(
                CONF_TEST_PRESS_INTERVAL, default="0s"
            ): cv.positive_time_period_milliseconds,
        }
    )
    .extend(cv.COMPONENT_SCHEMA)
    .extend(spi.spi_device_schema(cs_pin_required=True, default_data_rate=2000000))
)


def cecotec_child_schema():
    """Los tres tipos de entidad comparten la referencia al hub."""
    return cv.Schema({cv.GenerateID(CONF_CECOTEC_ID): cv.use_id(CecotecHub)})


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await spi.register_spi_device(var, config)

    cg.add(var.set_dry_run(config[CONF_DRY_RUN]))
    cg.add(var.set_frequency(int(config[CONF_FREQUENCY])))
    cg.add(var.set_power(config[CONF_POWER]))
    cg.add(var.set_use_bank(config[CONF_USE_BANK]))
    cg.add(var.set_counter_start(config[CONF_COUNTER_START]))
    cg.add(var.set_test_press_interval(
        int(config[CONF_TEST_PRESS_INTERVAL].total_milliseconds)))

    if CONF_BUSY_PIN in config:
        cg.add(var.set_busy_pin(await cg.gpio_pin_expression(config[CONF_BUSY_PIN])))
    if CONF_RESET_PIN in config:
        cg.add(var.set_reset_pin(await cg.gpio_pin_expression(config[CONF_RESET_PIN])))

    press = cg.StructInitializer(
        cecotec_ns.struct("PressParams"),
        ("blocks", config[CONF_BLOCKS]),
        ("packets_per_block", config[CONF_PACKETS_PER_BLOCK]),
        ("interval_us", int(config[CONF_INTERVAL].total_microseconds)),
        ("group", config[CONF_GROUP]),
        ("block_gap_us", int(config[CONF_BLOCK_GAP].total_microseconds)),
    )
    cg.add(var.set_press(press))
