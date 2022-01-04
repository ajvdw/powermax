import esphome.codegen as cg
import esphome.config_validation as cv

from esphome.components import uart
from esphome.const import (
    CONF_ID,
)
from esphome import pins

CODEOWNERS = ["@ajvdw"]

DEPENDENCIES = ["uart"]

CONF_POWERMAX_ID = "powermax_id"

powermax_ns = cg.esphome_ns.namespace("esphome::powermax")
PowerMax = powermax_ns.class_("PowerMax", uart.UARTDevice, cg.Component)

POWERMAX_COMPONENT_SCHEMA = cv.COMPONENT_SCHEMA.extend(
    {
        cv.Required(CONF_POWERMAX_ID): cv.use_id(PowerMax),
    }
)

CONFIG_SCHEMA = (
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(PowerMax),
        }
    )
    .extend(cv.COMPONENT_SCHEMA)
    .extend(uart.UART_DEVICE_SCHEMA)
)

async def to_code(config):
    cg.add_global(powermax_ns.using)
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await uart.register_uart_device(var, config)
