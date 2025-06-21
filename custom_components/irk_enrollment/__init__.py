import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import esp32_ble_server, text_sensor
from esphome.const import CONF_ID

AUTO_LOAD = ["esp32_ble", "esp32_ble_server", "text_sensor"]
CODEOWNERS = ["@dgrnbrg"]
CONFLICTS_WITH = ["esp32_ble_beacon"]
DEPENDENCIES = ["esp32", "esp32_ble", "esp32_ble_server", "text_sensor"]

CONF_LATEST_IRK = "latest_irk"

irk_enrollment_ns = cg.esphome_ns.namespace("irk_enrollment")
IrkEnrollmentComponent = irk_enrollment_ns.class_(
"IrkEnrollmentComponent",
cg.Component,
)

CONFIG_SCHEMA = cv.Schema(
{
cv.GenerateID(): cv.declare_id(IrkEnrollmentComponent),
cv.Optional(CONF_LATEST_IRK): text_sensor.text_sensor_schema(
icon="mdi:cellphone-key",
),
}
).extend(cv.COMPONENT_SCHEMA)

async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)


    if CONF_LATEST_IRK in config:
        latest_irk = await text_sensor.new_text_sensor(config[CONF_LATEST_IRK])
        cg.add(var.set_latest_irk(latest_irk))
