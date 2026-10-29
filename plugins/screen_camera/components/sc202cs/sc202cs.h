#pragma once

#include "esphome/components/i2c/i2c.h"
#include "esphome/core/component.h"

namespace esphome::sc202cs {

// The SC202CS's black level target, set once after esp_video_camera has started the sensor (__init__.py says why), and
// its frame rate while it streams for the camera (esp_video_camera's camera_sensor.h), once it read the sensor's id.
class SC202CS : public Component, public i2c::I2CDevice {
 public:
  void setup() override;
  void dump_config() override;
  // camera_sensor.h's set_fps and frame_report, on the camera's task: a longer frame for fewer frames a second (0 the
  // format's own), and what the sensor holds now.
  int set_fps(int fps);
  int frame_report(char *out, size_t size);
  // After esp_video_camera (DATA), whose setup writes the sensor's start registers.
  float get_setup_priority() const override { return setup_priority::PROCESSOR; }

 protected:
  bool found_{false};
  bool set_{false};
  uint16_t own_vts_{0};  // the format's own frame length, read the first time it is changed
};

}  // namespace esphome::sc202cs
