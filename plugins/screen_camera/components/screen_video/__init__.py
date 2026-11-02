"""Screen camera's video: the camera as H.264 with the microphone as Opus, over RTSP, for Home Assistant.

The part "Video" (parts/video.yaml) puts the camera in its raw mode (esp_video_camera device: csi) and adds this
component. It takes the camera's raw frames from Screen camera's frame hub (screen_camera/frame_hub.h), which hands the
same frames to its light level and motion: the ESP32-P4's hardware encoder (Espressif's esp_h264, which esp_video
already brings) takes every frame as the camera gives it while someone watches. A small RTSP server on the screen serves the stream to up to three viewers (Home
Assistant's go2rtc, which turns it into WebRTC without transcoding, and Home Assistant's own stream).

The password is not typed anywhere: it is worked out here from the screen's API key, and Home Assistant's tessera_video
integration works out the same from the key its ESPHome integration keeps for the screen.
"""
import hashlib

import esphome.codegen as cg
import esphome.config_validation as cv
import esphome.final_validate as fv
from esphome.components import microphone, socket
from esphome.components.esp32 import add_idf_component
from esphome.const import CONF_ID, CONF_MICROPHONE, CONF_PORT
from esphome.core import CORE

DEPENDENCIES = ["esp32", "screen_camera"]

CONF_CAMERA = "camera"
CONF_SCREEN_CAMERA = "screen_camera"
# Home Assistant's tessera_video integration makes the same password: keep the two equal.
PASSWORD_SALT = "tessera-video:"
PASSWORD_LENGTH = 24

screen_video_ns = cg.esphome_ns.namespace("screen_video")
ScreenVideo = screen_video_ns.class_("ScreenVideo", cg.Component)
Camera = cg.esphome_ns.namespace("camera").class_("Camera", cg.EntityBase, cg.Component)
ESPVideoCamera = cg.esphome_ns.namespace("esp_video_camera").class_("ESPVideoCamera", Camera)
ScreenCamera = cg.esphome_ns.namespace("screen_camera").class_("ScreenCamera", cg.Component)


def _raw_camera(config):
    cameras = fv.full_config.get().get("esp_video_camera") or []
    for camera in cameras if isinstance(cameras, list) else [cameras]:
        if camera.get(CONF_ID) == config[CONF_CAMERA] and camera.get("device") != "csi":
            raise cv.Invalid("The video needs the camera in its raw mode: esp_video_camera device: csi")
    return config


def password(key):
    """The stream's password for a screen's API key (its base64 text, or nothing for a screen without one)."""
    return hashlib.sha256((PASSWORD_SALT + (key or "")).encode()).hexdigest()[:PASSWORD_LENGTH]


CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(ScreenVideo),
        cv.Required(CONF_CAMERA): cv.use_id(ESPVideoCamera),
        cv.Required(CONF_SCREEN_CAMERA): cv.use_id(ScreenCamera),
        # Home Assistant's tessera_video asks for this port.
        cv.Optional(CONF_PORT, default=8554): cv.port,
        # The sound beside the picture: 16-bit samples of one channel of the screen's microphone. It listens only while
        # someone watches; ESPHome counts its listeners, so the voice assistant can listen at the same time.
        cv.Optional(CONF_MICROPHONE): microphone.microphone_source_schema(min_bits_per_sample=16, max_bits_per_sample=16),
    }
).extend(cv.COMPONENT_SCHEMA)


# The server's connections, which ESPHome sums into the screen's socket count: its listener and one for each of
# ScreenVideo::MAX_VIEWERS (screen_video.h). Without them the screen's count ran out with four live tiles beside three
# viewers.
MAX_VIEWERS = 3
CONFIG_SCHEMA = cv.All(
    CONFIG_SCHEMA,
    socket.consume_sockets(MAX_VIEWERS, "screen_video"),
    socket.consume_sockets(1, "screen_video", socket.SocketType.TCP_LISTEN),
)

FINAL_VALIDATE_SCHEMA = _raw_camera


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    cg.add(var.set_camera(await cg.get_variable(config[CONF_CAMERA])))
    cg.add(var.set_screen_camera(await cg.get_variable(config[CONF_SCREEN_CAMERA])))
    cg.add(var.set_port(config[CONF_PORT]))
    key = ((CORE.config.get("api") or {}).get("encryption") or {}).get("key")
    cg.add(var.set_password(password(key)))
    # Espressif's H.264 encoder at the version esp_video asks for (1.3.*), so the build keeps one copy.
    add_idf_component(name="espressif/esp_h264", ref="1.3.8")
    if CONF_MICROPHONE in config:
        cg.add_define("USE_SCREEN_VIDEO_SOUND")
        # 2.6 and later build only for P4 chips of revision 3.
        add_idf_component(name="espressif/esp_audio_codec", ref="2.5.0")
        cg.add(var.set_microphone_source(await microphone.microphone_source_to_code(config[CONF_MICROPHONE])))
