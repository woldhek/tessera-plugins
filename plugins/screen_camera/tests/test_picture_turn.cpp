// clang++ -std=c++17 -Wall -Wextra -Werror -I components tests/test_picture_turn.cpp -o /tmp/test_picture_turn && /tmp/test_picture_turn
// (from plugins/screen_camera). How far the camera's picture turns (components/screen_camera/picture_turn.h): the core's
// answer (tessera::camera_turn: the board's mounting and the screen's rotation, tested in Tessera's
// tests/test_camera_turn.cpp) and the setting "Camera upside down" on top.
#include <cstdio>

#include "screen_camera/picture_turn.h"

using esphome::screen_camera::picture_turn;

static int failures = 0;
static void expect(int got, int want, const char *what) {
  if (got != want) {
    std::printf("FAIL %s: %d, not %d\n", what, got, want);
    ++failures;
  }
}

int main() {
  expect(picture_turn(0, false), 0, "upright, as built");
  expect(picture_turn(180, false), 180, "the core turns it (a Tab5, or a screen turned half way)");
  expect(picture_turn(0, true), 180, "the setting turns it");
  expect(picture_turn(180, true), 0, "both: upright again");
  expect(picture_turn(-180, false), 180, "an angle below 0");
  expect(picture_turn(540, true), 0, "an angle past a full turn");
  if (failures == 0) std::printf("picture_turn: all good\n");
  return failures == 0 ? 0 : 1;
}
