"""Screen camera: the room's light level and motion from the screen's own camera, worked out on the screen.

The camera itself is esp_video_camera beside this folder (plugin.yaml); the core shows its dot while Home Assistant
takes pictures of it, and while this component senses (tessera::capturing). The sensors the camera finds by itself are
this plugin's table (sensors.py), and the camera's frames go to every consumer through the frame hub (frame_hub.h).
register_plugin() takes the id, version and the screen's texts from the manifest and translations/ beside this folder.
"""
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import binary_sensor, select, sensor, smart_display
from esphome.const import (
    CONF_ID,
    DEVICE_CLASS_ILLUMINANCE,
    DEVICE_CLASS_MOTION,
    STATE_CLASS_MEASUREMENT,
    UNIT_LUX,
)

from . import sensors

DEPENDENCIES = ["smart_display", "camera"]
AUTO_LOAD = ["sensor", "binary_sensor"]

CONF_CAMERA = "camera"
CONF_ILLUMINANCE = "illuminance"
CONF_MOTION = "motion"
CONF_FIELD_OF_VIEW = "field_of_view"

screen_camera_ns = cg.esphome_ns.namespace("screen_camera")
# ESPHome's camera base (esphome/components/camera/camera.h), which declares no class of its own in Python, and
# esp_video_camera's camera beside this folder: sensing takes its frames and its sensor's exposure, which another
# camera does not have, so a wrong id fails here rather than at run time. ESPHome matches an id's type by name.
Camera = cg.esphome_ns.namespace("camera").class_("Camera", cg.EntityBase, cg.Component)
ESPVideoCamera = cg.esphome_ns.namespace("esp_video_camera").class_("ESPVideoCamera", Camera)
ScreenCamera = screen_camera_ns.class_("ScreenCamera", cg.Component)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(ScreenCamera),
        cv.Required(CONF_CAMERA): cv.use_id(ESPVideoCamera),
        # The room's light level and motion, from the camera's frames on the screen (screen_camera.h).
        cv.Optional(CONF_ILLUMINANCE): sensor.sensor_schema(
            unit_of_measurement=UNIT_LUX,
            accuracy_decimals=1,
            device_class=DEVICE_CLASS_ILLUMINANCE,
            state_class=STATE_CLASS_MEASUREMENT,
        ),
        cv.Optional(CONF_MOTION): binary_sensor.binary_sensor_schema(device_class=DEVICE_CLASS_MOTION),
        # The setting "Field of view", which the plugin sets back to Sharp when the camera goes back by itself.
        cv.Optional(CONF_FIELD_OF_VIEW): cv.use_id(select.Select),
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    sensors.to_code()
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await smart_display.register_plugin(var, __file__)
    cg.add(var.set_camera(await cg.get_variable(config[CONF_CAMERA])))
    if CONF_ILLUMINANCE in config:
        cg.add(var.set_illuminance(await sensor.new_sensor(config[CONF_ILLUMINANCE])))
    if CONF_MOTION in config:
        cg.add(var.set_motion(await binary_sensor.new_binary_sensor(config[CONF_MOTION])))
    if CONF_FIELD_OF_VIEW in config:
        cg.add(var.set_field_select(await cg.get_variable(config[CONF_FIELD_OF_VIEW])))
