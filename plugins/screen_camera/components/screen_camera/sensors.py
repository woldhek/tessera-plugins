"""The camera sensors a screen finds by itself (docs/PICTURE_ARCHITECTURE.md in the Tessera repository, section 5).

esp_video_camera starts the board's MIPI-CSI sensor through Espressif's esp_cam_sensor, which finds it on the camera's I2C
bus among the drivers the build has. Each row is a sensor esp_cam_sensor drives: its name in esp_cam_sensor's Kconfig,
whose driver and place in the auto-detect list the build turns on, and the image tuning the build gives it instead of
esp_cam_sensor's default, a file beside the plugin. Adding such a sensor is a row. A sensor esp_cam_sensor has no driver
for is a component of its own beside this one (components/ov02c10), and one that needs a register set after it starts
too (components/sc202cs, its black level).
"""
from pathlib import Path

from esphome.components.esp32 import add_extra_build_file, add_idf_sdkconfig_option
from esphome.core import CORE

HERE = Path(__file__).parent


class Sensor:
    def __init__(self, name, tuning=None):
        self.name, self.tuning = name, tuning


# Kconfig keys verified against esp_cam_sensor 2.4.
SENSORS = (
    # The SmartSens SC202CS of the M5Stack Tab5 and the reTerminal D1001. The SC2356 module is the same silicon (PID
    # 0xeb52 at SCCB 0x36) and ships a tuning with noticeably better colour than the stock default, validated on both
    # boards; it is the one in esp_video_camera's cfg/, from the pull request that component comes from.
    Sensor("SC202CS", HERE.parent / "esp_video_camera" / "cfg" / "sc202cs.json"),
    Sensor("OV5647"),
    Sensor("SC2336"),
)


def to_code():
    """The sensors into the build: each driver with its auto-detect, then each tuning that replaces the default."""
    for sensor in SENSORS:
        add_idf_sdkconfig_option(f"CONFIG_CAMERA_{sensor.name}", True)
        add_idf_sdkconfig_option(f"CONFIG_CAMERA_{sensor.name}_AUTO_DETECT_MIPI_INTERFACE_SENSOR", True)
    for sensor in SENSORS:
        if sensor.tuning is None:
            continue
        # esp_cam_sensor's project_include.cmake embeds the file the CUSTOMIZED path names. CMake reads that path from
        # sdkconfig as a string, where a backslash is an escape: a Windows path lost every one of them and ninja found no
        # file (a build server on Windows), so it goes in with forward slashes.
        built = f"esp_video_camera/{sensor.name.lower()}_ipa.json"
        add_extra_build_file(built, sensor.tuning)
        add_idf_sdkconfig_option(f"CONFIG_CAMERA_{sensor.name}_DEFAULT_IPA_JSON_CONFIGURATION_FILE", False)
        add_idf_sdkconfig_option(f"CONFIG_CAMERA_{sensor.name}_CUSTOMIZED_IPA_JSON_CONFIGURATION_FILE", True)
        add_idf_sdkconfig_option(f"CONFIG_CAMERA_{sensor.name}_CUSTOMIZED_IPA_JSON_CONFIGURATION_FILE_PATH",
                                 Path(CORE.relative_build_path(built)).as_posix())
