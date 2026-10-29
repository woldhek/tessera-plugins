"""An empty esp_h264 for a camera that makes JPEG pictures only (Screen camera without its part "Video").

esp_video's manifest asks for esp_h264 on every ESP32-P4 build, although its H.264 sources and its CMake requirement
are gated by CONFIG_ESP_VIDEO_ENABLE_H264_VIDEO_DEVICE. ESP-IDF takes a component of the project before a managed one,
so this empty one satisfies that edge without downloading or compiling the encoder.

It is a component of its own so that a build with the video does without it (parts/video.yaml removes it): ESPHome wipes
a screen's build folder when an integration it had is gone, and the empty esp_h264 it wrote goes with it. Left behind,
it would hide the real encoder from the video and that build would fail.
"""
from pathlib import Path

import esphome.config_validation as cv
from esphome.components.esp32 import add_extra_build_file

DEPENDENCIES = ["esp32"]

CONFIG_SCHEMA = cv.Schema({})

HERE = Path(__file__).parent


async def to_code(config):
    add_extra_build_file("components/esp_h264/CMakeLists.txt", HERE / "build_stubs" / "esp_h264" / "CMakeLists.txt")
