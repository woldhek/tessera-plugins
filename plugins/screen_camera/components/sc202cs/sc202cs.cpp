#include "sc202cs.h"

#include <algorithm>
#include <cstdio>

#include "esphome/core/log.h"
#include "esphome/components/esp_video_camera/camera_sensor.h"

namespace esphome::sc202cs {

static const char *const TAG = "sc202cs";

// The registers of esp_cam_sensor's sc202cs_regs.h and its start tables.
static constexpr uint16_t REG_ID_HIGH = 0x3107;
static constexpr uint16_t REG_ID_LOW = 0x3108;
static constexpr uint16_t SC202CS_ID = 0xEB52;
static constexpr uint16_t REG_BLC_TARGET_HIGH = 0x3907;  // bits 4:0
static constexpr uint16_t REG_BLC_TARGET_LOW = 0x3908;

// In 10-bit counts: the sensor starts at 0x41, which is 16 of 255 in its 8-bit picture. 4 is 1 of 255, black.
static constexpr uint16_t BLC_TARGET = 0x0004;

// The frame length (VTS) and the group hold of sc202cs_regs.h, the group written as SmartSens drivers write theirs
// (Rockchip's sc2355.c: 0x3812 0x00 starts it, 0x30 ends and launches it at the frame's end). Every start table of
// esp_cam_sensor's runs the sensor at 30 frames a second.
static constexpr uint16_t REG_VTS_HIGH = 0x320e;
static constexpr uint16_t REG_VTS_LOW = 0x320f;
static constexpr uint16_t REG_GROUP_HOLD = 0x3812;
static constexpr uint16_t REG_EXPOSURE = 0x3e00;
static constexpr int OWN_FPS = 30;

static SC202CS *found_sensor = nullptr;
static int camera_set_fps(int fps) { return found_sensor ? found_sensor->set_fps(fps) : 0; }
static int camera_frame_report(char *out, size_t size) {
  return found_sensor ? found_sensor->frame_report(out, size) : 0;
}
// Its frame rate only: its one format already shows all it sees.
static const tessera_camera_sensor_t CAMERA_SENSOR = {"SC202CS", camera_set_fps, camera_frame_report, nullptr,
                                                      nullptr};

void SC202CS::setup() {
  uint8_t high = 0, low = 0;
  if (this->read_register16(REG_ID_HIGH, &high, 1) != i2c::ERROR_OK ||
      this->read_register16(REG_ID_LOW, &low, 1) != i2c::ERROR_OK) {
    ESP_LOGD(TAG, "No sensor answers at 0x%02X", this->address_);
    return;
  }
  if ((uint16_t(high) << 8 | low) != SC202CS_ID) {
    ESP_LOGD(TAG, "Sensor id 0x%02X%02X is not the SC202CS: black level left alone", high, low);
    return;
  }
  this->found_ = true;
  found_sensor = this;
  tessera_camera_add_sensor(&CAMERA_SENSOR);
  const uint8_t target_high = (BLC_TARGET >> 8) & 0x1F;
  const uint8_t target_low = BLC_TARGET & 0xFF;
  this->set_ = this->write_register16(REG_BLC_TARGET_HIGH, &target_high, 1) == i2c::ERROR_OK &&
               this->write_register16(REG_BLC_TARGET_LOW, &target_low, 1) == i2c::ERROR_OK;
  if (!this->set_)
    ESP_LOGW(TAG, "Could not set the black level target; the picture keeps a grey, pink cast in its shadows");
}

int SC202CS::set_fps(int fps) {
  if (!this->found_) return 0;
  if (this->own_vts_ == 0) {
    // The start table's own length: nothing else changes it before the first slower rate.
    uint8_t high = 0, low = 0;
    if (this->read_register16(REG_VTS_HIGH, &high, 1) != i2c::ERROR_OK ||
        this->read_register16(REG_VTS_LOW, &low, 1) != i2c::ERROR_OK)
      return 0;
    this->own_vts_ = uint16_t(high) << 8 | low;
    if (this->own_vts_ == 0) return 0;
  }
  uint32_t vts = this->own_vts_;
  if (fps > 0) vts = std::min<uint32_t>(std::max<uint32_t>(uint32_t(this->own_vts_) * OWN_FPS / fps, vts), 0x7fff);
  const uint8_t start = 0x00, end = 0x30, high = (vts >> 8) & 0xff, low = vts & 0xff;
  if (this->write_register16(REG_GROUP_HOLD, &start, 1) != i2c::ERROR_OK ||
      this->write_register16(REG_VTS_HIGH, &high, 1) != i2c::ERROR_OK ||
      this->write_register16(REG_VTS_LOW, &low, 1) != i2c::ERROR_OK ||
      this->write_register16(REG_GROUP_HOLD, &end, 1) != i2c::ERROR_OK) {
    ESP_LOGW(TAG, "The frame length did not take (%u lines)", (unsigned) vts);
    return 0;
  }
  return int(OWN_FPS * this->own_vts_ / vts);
}

// What the sensor holds now: the frame's length and the exposure (0x3e00..0x3e02, lines in bits 19..4, as
// esp_cam_sensor's sc202cs.c writes them).
int SC202CS::frame_report(char *out, size_t size) {
  if (!this->found_ || out == nullptr || size == 0) return 0;
  uint8_t high = 0, low = 0, exposure[3] = {0, 0, 0};
  bool ok = this->read_register16(REG_VTS_HIGH, &high, 1) == i2c::ERROR_OK &&
            this->read_register16(REG_VTS_LOW, &low, 1) == i2c::ERROR_OK;
  for (uint16_t i = 0; i < 3 && ok; i++) ok = this->read_register16(REG_EXPOSURE + i, &exposure[i], 1) == i2c::ERROR_OK;
  if (!ok) return snprintf(out, size, "nothing: it did not answer");
  const unsigned lines = (exposure[0] & 0x0f) << 12 | exposure[1] << 4 | exposure[2] >> 4;
  return snprintf(out, size, "a frame of %u lines (its format's %u), exposure %u lines", (unsigned) (high << 8 | low),
                  (unsigned) this->own_vts_, lines);
}

void SC202CS::dump_config() {
  ESP_LOGCONFIG(TAG, "SC202CS black level:");
  if (!this->found_) {
    ESP_LOGCONFIG(TAG, "  No SC202CS on this screen");
    return;
  }
  ESP_LOGCONFIG(TAG, "  Target: %u (10-bit), %s", BLC_TARGET, this->set_ ? "set" : "NOT set");
}

}  // namespace esphome::sc202cs
