#pragma once
// How far the camera's picture turns: the core's answer for this screen (tessera::camera_turn, plugin API 0.19: the
// board's mounting and the screen turned half way round), and half way again when a person mounted the screen the
// other way round (the setting "Camera upside down"). 0 or 180, which the sensor makes by flipping and mirroring its
// readout (frame_hub.h), so the stills, the video and sensing all read the same frames. Pure, so a PC tests it
// (tests/test_picture_turn.cpp).
namespace esphome::screen_camera {

inline int picture_turn(int screen_turn, bool upside_down) {
  const bool half = ((screen_turn % 360) + 360) % 360 == 180;
  return half != upside_down ? 180 : 0;
}

}  // namespace esphome::screen_camera
