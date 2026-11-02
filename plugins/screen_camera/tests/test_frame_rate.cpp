// clang++ -std=c++17 -Wall -Wextra -Werror -I components tests/test_frame_rate.cpp -o /tmp/test_frame_rate && /tmp/test_frame_rate
// (from plugins/screen_camera). How fast the camera's sensor runs (components/screen_camera/frame_rate.h): full at once
// while anyone needs it, slow for sensing only after nobody has needed full speed for HOLD_MS, across the clock's wrap.
#include <cstdio>

#include "screen_camera/frame_rate.h"

using namespace esphome::screen_camera::frame_rate;

static int failures = 0;
static void expect(int got, int want, const char *what) {
  if (got != want) {
    printf("FAIL %s: %d, not %d\n", what, got, want);
    ++failures;
  }
}

int main() {
  uint32_t full_at = 0;
  // Sensing alone from the start: slow at once, nobody ever wanted more.
  expect(choose(SENSE, 1000, full_at), SENSE, "sensing alone from boot");
  // Nobody asks (the video keeps the camera running unwatched): as slow as for sensing.
  expect(choose(NONE, 2000, full_at), SENSE, "nobody asks");
  // Someone watches: full at once, and while they watch.
  expect(choose(FULL, 3000, full_at), FULL, "a viewer comes");
  expect(choose(FULL, 9000, full_at), FULL, "the viewer stays");
  // The viewer goes, sensing stays: full for HOLD_MS more, then slow.
  expect(choose(SENSE, 9001, full_at), FULL, "just after the viewer went");
  expect(choose(SENSE, 9000 + HOLD_MS - 1, full_at), FULL, "a moment before the hold ends");
  expect(choose(SENSE, 9000 + HOLD_MS + 1, full_at), SENSE, "after the hold");
  // The viewer goes and nothing senses: slow too, after the same hold.
  uint32_t alone = 0;
  expect(choose(FULL, 40000, alone), FULL, "a viewer, nothing sensing");
  expect(choose(NONE, 40000 + HOLD_MS - 1, alone), FULL, "the viewer gone, inside the hold");
  expect(choose(NONE, 40000 + HOLD_MS + 1, alone), SENSE, "the viewer gone, after the hold");
  // A snapshot in between holds it full again for HOLD_MS.
  expect(choose(FULL, 30000, full_at), FULL, "a snapshot");
  expect(choose(SENSE, 31000, full_at), FULL, "a second after the snapshot");
  expect(choose(SENSE, 30000 + HOLD_MS + 1, full_at), SENSE, "the snapshot's hold over");
  // Across the millisecond clock's wrap.
  uint32_t wrapped = 0;
  expect(choose(FULL, 0xFFFFF000u, wrapped), FULL, "a viewer just before the wrap");
  expect(choose(SENSE, 0x00000100u, wrapped), FULL, "just after the wrap, inside the hold");
  expect(choose(SENSE, 0xFFFFF000u + HOLD_MS + 1, wrapped), SENSE, "after the wrap and the hold");
  if (failures) return 1;
  printf("test_frame_rate: ok\n");
  return 0;
}
