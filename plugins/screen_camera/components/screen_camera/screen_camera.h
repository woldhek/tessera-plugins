#pragma once
#include <atomic>
#include <cstdint>
#include <memory>

#include "esphome/core/component.h"
#include "esphome/core/defines.h"
#include "esphome/components/camera/camera.h"
#include "esphome/components/esp_video_camera/esp_video_camera.h"
#include "esphome/components/smart_display/plugin_api.h"
#include "frame_hub.h"
#ifdef USE_SENSOR
#include "esphome/components/sensor/sensor.h"
#endif
#ifdef USE_BINARY_SENSOR
#include "esphome/components/binary_sensor/binary_sensor.h"
#endif
#ifdef USE_SELECT
#include "esphome/components/select/select.h"
#endif

namespace esphome::screen_camera {

// The plugin: the screen's camera as a camera of its device in Home Assistant (esp_video_camera, plugin.yaml). The core
// shows its dot while Home Assistant takes pictures (it listens to ts_camera itself, plugin API 0.17), so this plugin
// draws nothing of its own for that.
// And, only while a person switches it on (the setting "Camera light level and motion", off by default), the room's
// light level and motion, worked out on the screen from the camera's frames: no picture leaves the screen for them. The
// camera hands its processed frames to the frame hub (frame_hub.h), which hands them on to this plugin on the camera's
// own task while sensing is on (RGB565, or the part Video's YUV420); the plugin reads a coarse grid of them through the
// frame reader (frame_reader.h) and gives the results to the main loop. While it runs it tells the core (tessera::capturing), so
// the dot shows then too; with the setting "Camera motion wakes the screen" (off by default) motion wakes the screen.
class ScreenCamera : public Component,
                     public tessera::Plugin,
                     public esp_video_camera::RawVideoFrameConsumer {
 public:
  void setup() override;
  void loop() override;
  float get_setup_priority() const override { return setup_priority::LATE; }
  void on_interval(uint32_t now_ms) override;

  void set_camera(esp_video_camera::ESPVideoCamera *c) {
    camera_ = c;
    frames_.set_camera(c);
  }
  // The camera's frames, for every consumer on the screen (the part Video's encoder adds itself here too).
  FrameHub &frames() { return frames_; }
  // How much a busy screen asks the video to give way (plugin API 0.21, tessera::squeeze): 0 as it is, 1 every other
  // frame, 2 one in four. Asked here, so the video's driver beside this plugin uses none of the screen's API itself
  // (tools/check.py: a driver may run a task of its own); from any task.
  uint8_t video_squeeze() const;
#ifdef USE_SENSOR
  void set_illuminance(sensor::Sensor *s) { illuminance_ = s; }
#endif
#ifdef USE_BINARY_SENSOR
  void set_motion(binary_sensor::BinarySensor *s) { motion_ = s; }
#endif
  // Sensing on or off (the setting). Before setup() it is only remembered.
  void set_sensing(bool on);
  // Motion wakes the screen or not (the setting, off by default).
  void set_wake_on_motion(bool on) { wake_on_motion_ = on; }
  // The screen is mounted the other way round (the setting "Camera upside down", off by default): the picture turns
  // half way on top of the core's answer (picture_turn.h), at the next second.
  void set_upside_down(bool on) { upside_down_ = on; }
  // The field of view (the setting, Full by default): 0 Sharp, the sensor's own mode; 1 Full, its whole field. Only a
  // sensor whose driver offers it takes Full (the OV02C10); the camera goes back to Sharp by itself otherwise, and
  // on_interval sets the setting back.
  void set_field(int field) {
    if (camera_ != nullptr) camera_->set_field(field);
  }
#ifdef USE_SELECT
  void set_field_select(select::Select *s) { field_select_ = s; }
#endif
  // On the camera's task, from the frame hub, for every frame while sensing is on. RGB565, or the video's YUV420.
  void consume_raw_video_frame(const esp_video_camera::RawVideoFrame &frame) override;
  // Someone watches the video, or nobody any more: the core's dot, as for sensing (from the loop).
  void set_video_watched(bool watched);
  // Someone hears the microphone through the video, or nobody any more (plugin API 0.23, from the loop).
  void set_sound_heard(bool heard);

  // The screen's own camera on a live tile of its own (plugin API 0.22): the hub's consumer that hands the core each
  // frame, as the camera gave it, while such a tile is on the glass. The core draws the tile from it, so the video's
  // encoder, the app and the network have no part in it.
  class OwnTile : public esp_video_camera::RawVideoFrameConsumer {
   public:
    void consume_raw_video_frame(const esp_video_camera::RawVideoFrame &frame) override;
  };

 protected:
  // The coarse grid sensing reads: 32 x 18 cells, each the mean of 4 x 4 points.
  static constexpr int GRID_W = 32;
  static constexpr int GRID_H = 18;

  void apply_sensing_();
  void publish_sensing_();
  void show_dot_();
  esp_video_camera::ESPVideoCamera *video_() const { return camera_; }

  esp_video_camera::ESPVideoCamera *camera_{};
  FrameHub frames_;
  bool added_{false};  // sensing is one of the hub's consumers
  OwnTile own_tile_;
  bool own_added_{false};  // the own tile's feed is one of them too, offered to the core
  bool own_shown_{false};  // a tile of the camera is on the glass, as the loop last saw
#ifdef USE_SENSOR
  sensor::Sensor *illuminance_{};
#endif
#ifdef USE_BINARY_SENSOR
  binary_sensor::BinarySensor *motion_{};
#endif
  bool wake_on_motion_{false};
  bool upside_down_{false};
#ifdef USE_SELECT
  select::Select *field_select_{};
#endif
  uint32_t seen_turns_{0};  // the camera's task: the hub's turns its motion grid was made after
  uint32_t woken_for_{0};      // the motion sample the screen was last woken for
  uint32_t last_wake_ms_{0};

  // Sensing. Set up on the main loop; the camera's task owns the grid and the sample clock while it runs.
  bool sensing_wanted_{false};
  bool sensing_on_{false};
  bool set_up_{false};
  bool video_watched_{false};
  bool dot_watched_{false};  // the core's dot as this plugin told it: CAMERA
  bool dot_sensing_{false};  // and SENSING
  uint8_t grid_[GRID_W * GRID_H]{};
  bool grid_valid_{false};
  uint32_t last_sample_ms_{0};
  float last_exposure_product_{0};
  uint32_t settle_until_ms_{0};   // no motion before this: the light changed and the camera is settling
  float light_level_{-1.0f};      // smoothed light level in lux, on the camera's task
  // What the camera's task found, for the main loop.
  std::atomic<uint32_t> last_motion_ms_{0};
  std::atomic<uint32_t> lux_x10_{UINT32_MAX};
  uint32_t published_lux_x10_{UINT32_MAX};
  uint32_t last_lux_publish_ms_{0};
  bool motion_shown_{false};
};

}  // namespace esphome::screen_camera
