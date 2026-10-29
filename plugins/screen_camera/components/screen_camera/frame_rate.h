#pragma once
// How fast the camera's sensor runs (frame_hub.h): as fast as its format goes while anyone needs that (a person
// watching the video, Home Assistant taking pictures), and slow while the camera only senses (a light level, motion,
// read every 400 ms) or nobody needs a frame at all (the part Video keeps the camera running from boot, for its
// encoder's memory, frames or not), so its frames cost the memory bus, the image processor and the power next to
// nothing. Down only once nobody has needed full speed for HOLD_MS, so a snapshot now and then does not switch it to
// and fro; up at once.
// Pure, so a PC tests it (tests/test_frame_rate.cpp).
#include <cstdint>

namespace esphome::screen_camera::frame_rate {

constexpr uint8_t NONE = 0;    // a consumer that needs no frames now
constexpr uint8_t SENSE = 5;   // frames a second for sensing (a sample every 400 ms, with room for the exposure), and
                               // the slowest the camera runs
constexpr uint8_t FULL = 255;  // the sensor's format's own rate
constexpr uint32_t HOLD_MS = 10000;

// The rate to run at now, from the most any consumer asks (`asked`: NONE, a rate, or FULL). `full_at_ms` is when FULL
// was last asked (0 for never), kept by the caller and updated here.
inline uint8_t choose(uint8_t asked, uint32_t now_ms, uint32_t &full_at_ms) {
  if (asked == FULL) {
    full_at_ms = now_ms | 1;  // 0 means never
    return FULL;
  }
  if (full_at_ms != 0 && now_ms - full_at_ms < HOLD_MS) return FULL;
  // Nobody asks: as slow as for sensing. A camera no consumer has on is stopped anyway, and a consumer that switches
  // it on asks what it needs.
  return asked == NONE ? SENSE : asked;
}

}  // namespace esphome::screen_camera::frame_rate
