import esphome.codegen as cg
import esphome.config_validation as cv

from esphome import pins
from esphome.components import climate, switch
from esphome.const import (
    CONF_ID,
    CONF_NAME,
    CONF_MIN_TEMPERATURE,
    CONF_MAX_TEMPERATURE,
)

CONF_POWER_BTN = "power_button"
CONF_UP_BTN = "up_button"
CONF_DOWN_BTN = "down_button"
CONF_STB_COMM = "display_stb"
CONF_CLK_COMM = "display_clk"
CONF_DIO_COMM = "display_dio"

rinai15l_ns = cg.esphome_ns.namespace("rinai15l")

RINAI15LClimate = rinai15l_ns.class_(
    "RINAI15LClimate",
    climate.Climate,
    cg.Component,
)

CONFIG_SCHEMA = climate.CLIMATE_SCHEMA.extend(
    {
        cv.GenerateID(): cv.declare_id(RINAI15LClimate),        

        cv.Required(CONF_POWER_BTN): pins.gpio_output_pin_schema,
        cv.Required(CONF_UP_BTN): pins.gpio_output_pin_schema,
        cv.Required(CONF_DOWN_BTN): pins.gpio_output_pin_schema,

        cv.Required(CONF_STB_COMM): pins.gpio_input_pin_schema,
        cv.Required(CONF_CLK_COMM): pins.gpio_input_pin_schema,
        cv.Required(CONF_DIO_COMM): pins.gpio_input_pin_schema,

        cv.Optional(CONF_MIN_TEMPERATURE, default=35): cv.float_,
        cv.Optional(CONF_MAX_TEMPERATURE, default=60): cv.float_,
    }
)

async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])

    await cg.register_component(var, config)
    await climate.register_climate(var, config)

    # ---------------------------
    # Buttons
    # ---------------------------
    power_pin = await cg.gpio_pin_expression(config[CONF_POWER_BTN])
    cg.add(var.set_power_pin(power_pin))

    up_pin = await cg.gpio_pin_expression(config[CONF_UP_BTN])
    cg.add(var.set_up_pin(up_pin))

    down_pin = await cg.gpio_pin_expression(config[CONF_DOWN_BTN])
    cg.add(var.set_down_pin(down_pin))

    # ---------------------------
    # Display
    # ---------------------------
    stb_pin = await cg.gpio_pin_expression(config[CONF_STB_COMM])
    cg.add(var.set_display_stb_pin(stb_pin))

    clk_pin = await cg.gpio_pin_expression(config[CONF_CLK_COMM])
    cg.add(var.set_display_clk_pin(clk_pin))

    dio_pin = await cg.gpio_pin_expression(config[CONF_DIO_COMM])
    cg.add(var.set_display_dio_pin(dio_pin))

    # ---------------------------
    # Temperature
    # ---------------------------
    cg.add(var.set_min_temperature(config[CONF_MIN_TEMPERATURE]))
    cg.add(var.set_max_temperature(config[CONF_MAX_TEMPERATURE]))
