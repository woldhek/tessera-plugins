"""The SmartSens SC202CS (sold as the SC2356) of the M5Stack Tab5 and the reTerminal D1001: its black level.

Espressif's esp_cam_sensor drives this sensor and starts it with a black level target of 0x41 (registers 0x3907 and
0x3908, in 10-bit counts): black leaves the sensor as about 16 of 255, and the ISP is meant to take that off. An ESP32-P4
before rev3 has no black level correction in its ISP, and both boards have such a chip, so the 16 stayed in the picture:
the white balance counted it as light, gamma lifted it to about 60 and the white balance gains turned it pink.

esp_video_init() writes the sensor's registers once, in esp_video_camera's setup; streaming only starts and stops it.
This component runs after that setup, checks the sensor's id over the board's I2C bus, and lowers the target to 4, so
black reaches the ISP as black. A sensor of another id is left alone. Should a tuning with the ISP's own black level
(`blc`) come for rev3 chips, this target goes back to the sensor's.
"""
import esphome.codegen as cg
from esphome.components import i2c
import esphome.config_validation as cv
from esphome.const import CONF_ID

DEPENDENCIES = ["i2c", "esp_video_camera"]

sc202cs_ns = cg.esphome_ns.namespace("sc202cs")
SC202CS = sc202cs_ns.class_("SC202CS", cg.Component, i2c.I2CDevice)

CONFIG_SCHEMA = (
    cv.Schema({cv.GenerateID(): cv.declare_id(SC202CS)})
    .extend(cv.COMPONENT_SCHEMA)
    .extend(i2c.i2c_device_schema(0x36))
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await i2c.register_i2c_device(var, config)
