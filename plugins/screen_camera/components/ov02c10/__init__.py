"""The OmniVision OV02C10 of the Guition ESP32-P4 screens, for esp_video_camera.

Espressif's esp_cam_sensor has no driver for the OV02C10 (PID 0x5602, SCCB 0x36), nor an image tuning for esp_video's
ISP pipeline. This component brings both, beside esp_video_camera and without changing it:

- ov02c10_sensor.c registers the driver in esp_cam_sensor's auto-detect list, a linker section that esp_video_init()
  walks. Nothing calls the driver by name, so a static archive would drop its object: the generated code calls
  ov02c10_sensor_link_anchor(), an empty function in the same object, which keeps it. The driver rejects a sensor of
  another id, so the other sensors at 0x36 are still found.
- ipa/ holds the tuning, one for rev3 silicon and one for older chips. It goes into the build as an empty ESP-IDF
  component whose project_include.cmake hands the right file to esp_ipa; ESP-IDF builds every component in the
  project's components folder.

The day esp_cam_sensor has an OV02C10 of its own, this folder goes.
"""
from pathlib import Path

import esphome.codegen as cg
from esphome.components.esp32 import add_extra_build_file
import esphome.config_validation as cv

DEPENDENCIES = ["esp32"]

CONFIG_SCHEMA = cv.Schema({})

IPA_FILES = ("CMakeLists.txt", "project_include.cmake", "ov02c10_p4_eco4.json", "ov02c10_p4_eco5.json")


async def to_code(config):
    cg.add_global(cg.RawStatement('extern "C" void ov02c10_sensor_link_anchor(void);'))
    cg.add(cg.RawExpression("ov02c10_sensor_link_anchor()"))
    ipa = Path(__file__).parent / "ipa"
    for name in IPA_FILES:
        add_extra_build_file(f"components/ov02c10_ipa/{name}", ipa / name)
