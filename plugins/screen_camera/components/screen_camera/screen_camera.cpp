#include "screen_camera.h"
#include "picture_turn.h"
#include "frame_reader.h"

#include <cmath>
#include <cstdlib>
#include <cstring>

#include "esphome/core/hal.h"
#include "esphome/core/log.h"

namespace esphome::screen_camera {

static const char *const TAG = "screen_camera";
// Motion wakes the screen at most this often while it goes on (docs/PLUGINS.md, "Waking the screen").
static constexpr uint32_t WAKE_EVERY_MS = 2000;
// Sensing reads a frame at most this often; the camera delivers a few a second (plugin.yaml's max_framerate).
static constexpr uint32_t SAMPLE_MS = 400;
// A cell counts as changed when it moved more than this (0..255, after the frame's own shift in brightness is taken
// off), plus a little more as gain, and with it noise, rises.
static constexpr int MOTION_CELL_THRESHOLD = 14;
// Motion: at least this many of the 576 cells changed between two samples.
static constexpr int MOTION_CELLS = 6;
// Exposure x gain moving more than this between two samples is auto exposure at work, not motion.
static constexpr float EXPOSURE_STEP = 0.10f;
// 40% of the cells or more changing the same way at once (all brighter, or all darker) is a light switched on or off,
// not someone moving: a lamp lights the room unevenly, a person changes one part of it, some cells brighter and some
// darker.
static constexpr int LIGHT_CHANGE_CELLS = 32 * 18 * 2 / 5;
// After a light change auto exposure, white balance and the gamma take a few seconds to settle, and every frame of it
// looks different: no motion is judged for this long.
static constexpr uint32_t SETTLE_MS = 3000;
// Motion stays on this long after the last changed sample (plugin.yaml adds its own hold for Home Assistant).
static constexpr uint32_t MOTION_ON_MS = 2500;
// The light level goes to Home Assistant at most this often, unless it changed a lot.
static constexpr uint32_t LUX_PUBLISH_MS = 10000;
// The light level from the frame's mean brightness and the sensor's exposure time and gain:
// lux = LUX_SCALE * linear mean / (exposure in seconds * gain), with the ISP's gamma (about 0.55) taken off the mean
// first. LUX_SCALE is the exposure equation for an f/2 lens and a sensor of about ISO 100 at gain 1: a rough figure,
// right in proportion, which is what an automation that dims by it needs.
static constexpr float LUX_SCALE = 48.5f;
static constexpr float INVERSE_GAMMA = 1.8f;

void ScreenCamera::setup() {
  set_up_ = true;
  // Sensing reads either layout, so it takes the frames as the camera hands them out: the video's YUV420 when the part
  // Video added itself first, RGB565 otherwise.
  // Light level and motion read any frame: a Full field of view's halved or not (frame_hub.h).
  added_ = camera_ != nullptr && frames_.add(this, esp_video_camera::RawVideoPixelFormat::RGB565_LE, true, true);
  // The own tile reads either layout too, and takes the picture at its true shape, only while a tile of it shows.
  own_added_ = camera_ != nullptr && frames_.add(&own_tile_, esp_video_camera::RawVideoPixelFormat::RGB565_LE, true);
  if (own_added_) {
    frames_.wants(&own_tile_, false);
    tessera::offer_own_frames();
  }
  apply_sensing_();
}

void ScreenCamera::OwnTile::consume_raw_video_frame(const esp_video_camera::RawVideoFrame &frame) {
  if (frame.data == nullptr || !tessera::own_tile_wants()) return;
  tessera::OwnFrame own;
  own.data = frame.data;
  own.width = static_cast<uint16_t>(frame.width);
  own.height = static_cast<uint16_t>(frame.height);
  own.format = frame.pixel_format == esp_video_camera::RawVideoPixelFormat::YUV420_OUYY_EVYY ? tessera::FrameFormat::YUV420
                                                                                              : tessera::FrameFormat::RGB565;
  tessera::own_frame(own);
}

void ScreenCamera::set_sensing(bool on) {
  sensing_wanted_ = on;
  apply_sensing_();
}

// Starts or stops the camera's frames for sensing. While they run the camera stays on, at plugin.yaml's frame rate, and
// its auto exposure stays settled, so a picture Home Assistant asks for comes at once; nothing is encoded or sent.
void ScreenCamera::apply_sensing_() {
  if (!set_up_ || camera_ == nullptr || camera_->is_failed()) return;
  const bool on = sensing_wanted_;
  if (on == sensing_on_) return;
  if (on) {
    grid_valid_ = false;
    light_level_ = -1.0f;
    last_exposure_product_ = 0;
    settle_until_ms_ = millis() + SETTLE_MS;
    // With the part Video the camera runs from boot already, and the hub only starts handing frames on here.
    if (!added_ || !frames_.set_on(this, true)) {
      ESP_LOGW(TAG, "The camera does not hand out its frames; no light level or motion");
      return;
    }
    // A sample every 400 ms: the sensor may run slow for it while nobody watches (frame_hub.h).
    frames_.ask(this, frame_rate::SENSE);
    ESP_LOGI(TAG, "Sensing light and motion");
  } else {
    frames_.ask(this, frame_rate::NONE);
    frames_.set_on(this, false);
    ESP_LOGI(TAG, "Sensing stopped");
  }
  sensing_on_ = on;
  show_dot_();
#ifdef USE_SENSOR
  // Off: the light level reads unknown in Home Assistant, not the last value it had.
  if (!on && illuminance_ != nullptr) {
    illuminance_->publish_state(NAN);
    published_lux_x10_ = UINT32_MAX;
    lux_x10_.store(UINT32_MAX);
  }
#endif
  publish_sensing_();
}

void ScreenCamera::set_video_watched(bool watched) {
  video_watched_ = watched;
  show_dot_();
}

// Someone hears what the microphone hears (the video's sound): the screen keeps its live tiles at a quarter while it
// lasts (plugin API 0.23, the governor), so they do not starve the video's uplink; no dot changes.
void ScreenCamera::set_sound_heard(bool heard) { tessera::capturing(this, tessera::Capture::SOUND, heard); }

// The camera runs on the screen itself, for the video while someone watches (the core's dot solid) or for what never
// leaves it, sensing alone (the dot a ring, plugin API 0.20). Home Assistant's own pictures the core sees by itself.
void ScreenCamera::show_dot_() {
  // A tile of the camera on the glass shows the camera as much as a viewer of the video does.
  const bool watched = video_watched_ || own_shown_, sensing = sensing_on_ && !watched;
  if (watched != dot_watched_) {
    dot_watched_ = watched;
    tessera::capturing(this, tessera::Capture::CAMERA, watched);
  }
  if (sensing != dot_sensing_) {
    dot_sensing_ = sensing;
    tessera::capturing(this, tessera::Capture::SENSING, sensing);
  }
}

void ScreenCamera::consume_raw_video_frame(const esp_video_camera::RawVideoFrame &frame) {
  const uint32_t now = millis();
  // A busy screen (plugin API 0.21) samples half or a quarter as often: a light level and motion a second later.
  const uint8_t squeeze = tessera::squeeze(tessera::Work::SENSING);
  const uint32_t every = squeeze == 0 ? SAMPLE_MS : squeeze == 1 ? SAMPLE_MS * 2 : SAMPLE_MS * 4;
  if (last_sample_ms_ != 0 && now - last_sample_ms_ < every) return;
  const bool yuv = frame.pixel_format == esp_video_camera::RawVideoPixelFormat::YUV420_OUYY_EVYY;
  if ((!yuv && frame.pixel_format != esp_video_camera::RawVideoPixelFormat::RGB565_LE) || frame.width < GRID_W * 4 ||
      frame.height < GRID_H * 4 || frame.data == nullptr) {
    return;
  }
  last_sample_ms_ = now;
  // The picture turned: the cells changed places, which is no motion.
  const uint32_t turns = frames_.turns();
  if (turns != seen_turns_) {
    seen_turns_ = turns;
    grid_valid_ = false;
    settle_until_ms_ = now + SETTLE_MS;
  }
  const FrameReader reader(frame.data, yuv ? Layout::YUV420_OUYY_EVYY : Layout::RGB565_LE, frame.width, frame.stride_bytes);

  // The grid: each cell the mean brightness (0..255) of 4 x 4 points spread over it.
  uint8_t grid[GRID_W * GRID_H];
  uint32_t total = 0;
  const int cell_w = frame.width / GRID_W, cell_h = frame.height / GRID_H;
  for (int gy = 0; gy < GRID_H; gy++) {
    for (int gx = 0; gx < GRID_W; gx++) {
      uint32_t sum = 0;
      for (int sy = 0; sy < 4; sy++) {
        const int y = gy * cell_h + (2 * sy + 1) * cell_h / 8;
        for (int sx = 0; sx < 4; sx++) sum += reader.brightness(gx * cell_w + (2 * sx + 1) * cell_w / 8, y);
      }
      const uint8_t cell = static_cast<uint8_t>(sum / 16);
      grid[gy * GRID_W + gx] = cell;
      total += cell;
    }
  }
  const float mean = static_cast<float>(total) / (GRID_W * GRID_H);

  // The sensor's exposure, through esp_video (this is the capture task, which may use the capture descriptor).
  float exposure_us = 0, gain = 0;
  const bool exposure_known = video_()->read_sensor_exposure(&exposure_us, &gain);
  const float product = exposure_known ? exposure_us * gain : 0;

  // A light change: auto exposure moving, or most of the frame brighter or darker at once (below). Then the camera
  // settles first.
  if (exposure_known && last_exposure_product_ > 0 &&
      std::fabs(product - last_exposure_product_) > EXPOSURE_STEP * last_exposure_product_) {
    settle_until_ms_ = now + SETTLE_MS;
  }
  const bool settled = static_cast<int32_t>(now - settle_until_ms_) >= 0;

  // Motion: cells that changed by more than the whole frame did.
  if (grid_valid_ && settled) {
    const int threshold = MOTION_CELL_THRESHOLD + static_cast<int>(exposure_known ? gain : 1.0f);
    int shift = 0, brighter = 0, darker = 0;
    for (int i = 0; i < GRID_W * GRID_H; i++) {
      const int d = grid[i] - grid_[i];
      shift += d;
      if (d > threshold) brighter++;
      if (d < -threshold) darker++;
    }
    shift /= GRID_W * GRID_H;
    int changed = 0;
    for (int i = 0; i < GRID_W * GRID_H; i++) {
      if (std::abs(grid[i] - grid_[i] - shift) > threshold) changed++;
    }
    if (brighter >= LIGHT_CHANGE_CELLS || darker >= LIGHT_CHANGE_CELLS) {
      settle_until_ms_ = now + SETTLE_MS;
    } else if (changed >= MOTION_CELLS) {
      last_motion_ms_.store(now == 0 ? 1 : now);
    }
  }
  memcpy(grid_, grid, sizeof(grid_));
  grid_valid_ = true;
  last_exposure_product_ = product;

  // Light level.
  if (exposure_known && exposure_us > 0 && gain > 0) {
    const float linear = std::pow(mean / 255.0f, INVERSE_GAMMA);
    const float lux = LUX_SCALE * linear / (exposure_us * 1e-6f * gain);
    light_level_ = light_level_ < 0 ? lux : 0.7f * light_level_ + 0.3f * lux;
    lux_x10_.store(static_cast<uint32_t>(light_level_ * 10.0f + 0.5f));
  }
  enable_loop_soon_any_context();
}

void ScreenCamera::loop() {
  disable_loop();
  publish_sensing_();
}

void ScreenCamera::publish_sensing_() {
  const uint32_t now = millis();
#ifdef USE_BINARY_SENSOR
  if (motion_ != nullptr) {
    const uint32_t last = last_motion_ms_.load();
    const bool moving = sensing_on_ && last != 0 && now - last < MOTION_ON_MS;
    if (moving != motion_shown_ || !motion_->has_state()) {
      motion_shown_ = moving;
      motion_->publish_state(moving);
    }
  }
#endif
  // Motion wakes the screen (the setting), for each new sample with motion, at most every WAKE_EVERY_MS.
  const uint32_t motion_at = last_motion_ms_.load();
  if (wake_on_motion_ && sensing_on_ && motion_at != 0 && motion_at != woken_for_ && now - motion_at < MOTION_ON_MS &&
      (last_wake_ms_ == 0 || now - last_wake_ms_ >= WAKE_EVERY_MS)) {
    woken_for_ = motion_at;
    last_wake_ms_ = now;
    tessera::wake(this);
  }
#ifdef USE_SENSOR
  if (illuminance_ != nullptr && sensing_on_) {
    const uint32_t lux_x10 = lux_x10_.load();
    if (lux_x10 != UINT32_MAX) {
      // At most every ten seconds, at once when it changed by a quarter (a light switched on or off).
      const float last = static_cast<float>(published_lux_x10_);
      const bool big = published_lux_x10_ == UINT32_MAX || std::fabs(static_cast<float>(lux_x10) - last) > 0.25f * last + 5;
      if (big || (now - last_lux_publish_ms_ >= LUX_PUBLISH_MS && lux_x10 != published_lux_x10_)) {
        published_lux_x10_ = lux_x10;
        last_lux_publish_ms_ = now;
        illuminance_->publish_state(lux_x10 / 10.0f);
      }
    }
  }
#endif
}

void ScreenCamera::on_interval(uint32_t now_ms) {
  // A tile of the camera came onto the glass or went: its frames at full speed while it shows, none after.
  const bool own = own_added_ && tessera::own_tile_wants();
  if (own != own_shown_) {
    own_shown_ = own;
    frames_.wants(&own_tile_, own);
    frames_.ask(&own_tile_, own ? frame_rate::FULL : frame_rate::NONE);
    frames_.set_on(&own_tile_, own);
    show_dot_();
    ESP_LOGI(TAG, own ? "A tile of this camera shows: its frames go to it" : "No tile of this camera shows any more");
  }
  // The picture's turn follows the screen's rotation, which the app changes without a build.
  frames_.turn(picture_turn(tessera::camera_turn(), upside_down_));
  // The sensor's rate as the consumers and Home Assistant want it now (slower once nobody needed full speed a while).
  frames_.tick(now_ms);
  publish_sensing_();
  // The camera went back to Sharp by itself (no such mode, or no frame in it): the setting says so.
  if (camera_ != nullptr && camera_->take_field_fallback()) {
#ifdef USE_SELECT
    if (field_select_ != nullptr) field_select_->make_call().set_index(0).perform();
#endif
  }
}

uint8_t ScreenCamera::video_squeeze() const { return tessera::squeeze(tessera::Work::OWN_CAMERA); }

}  // namespace esphome::screen_camera
