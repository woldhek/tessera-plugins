// clang++ -std=c++17 -Wall -Wextra -Werror -I components tests/test_frame_reader.cpp -o /tmp/test_frame_reader && /tmp/test_frame_reader
// (from plugins/screen_camera). The frame reader (components/screen_camera/frame_reader.h): a point's brightness in
// both layouts the camera hands out, the same numbers sensing read inline before (Screen camera 0.4.2), point by point
// and as the 32 x 18 grid of 4 x 4 points it makes of a frame.
#include "screen_camera/frame_reader.h"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>

using esphome::screen_camera::FrameReader;
using esphome::screen_camera::Layout;

// What sensing did before, inline (screen_camera.cpp, 0.4.2).
static uint32_t old_rgb565(const uint8_t *data, size_t stride, int x, int y) {
  const auto *row = reinterpret_cast<const uint16_t *>(data + (size_t) y * stride);
  const uint16_t p = row[x];
  const uint32_t r = (p >> 11) & 0x1F, g = (p >> 5) & 0x3F, b = p & 0x1F;
  return (r * 8 * 77 + g * 4 * 150 + b * 8 * 29) >> 8;
}
static uint32_t old_yuv(const uint8_t *data, int width, int x, int y) {
  const size_t yuv_row = static_cast<size_t>(width) * 3 / 2;
  const uint8_t *row = data + (size_t) y * yuv_row;
  return row[(x / 2) * 3 + 1 + (x & 1)];
}

static void points() {
  // RGB565: white, black, pure red, green and blue, and a stride wider than the line.
  const int w = 8, h = 2;
  const size_t stride = (w + 4) * 2;
  std::vector<uint8_t> rgb(stride * h, 0);
  auto put = [&](int x, int y, uint16_t p) { std::memcpy(&rgb[y * stride + x * 2], &p, 2); };
  put(0, 0, 0xFFFF);
  put(1, 0, 0x0000);
  put(2, 0, 0xF800);
  put(3, 0, 0x07E0);
  put(4, 0, 0x001F);
  put(5, 1, 0x7BEF);
  const FrameReader r(rgb.data(), Layout::RGB565_LE, w, stride);
  assert(r.brightness(0, 0) == 250);  // full white, in 5-6-5 bits
  assert(r.brightness(1, 0) == 0);
  assert(r.brightness(2, 0) == (31u * 8 * 77) >> 8);
  assert(r.brightness(3, 0) == (63u * 4 * 150) >> 8);
  assert(r.brightness(4, 0) == (31u * 8 * 29) >> 8);
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x) assert(r.brightness(x, y) == old_rgb565(rgb.data(), stride, x, y));

  // YUV420 O_UYY_E_VYY: a line of U Y Y..., a line of V Y Y..., three bytes for two points; Y is the brightness.
  std::vector<uint8_t> yuv(w * 3 / 2 * h);
  for (size_t i = 0; i < yuv.size(); ++i) yuv[i] = static_cast<uint8_t>(i * 7 + 3);
  const FrameReader v(yuv.data(), Layout::YUV420_OUYY_EVYY, w, w);
  assert(v.brightness(0, 0) == yuv[1] && v.brightness(1, 0) == yuv[2] && v.brightness(2, 0) == yuv[4]);
  assert(v.brightness(3, 1) == yuv[w * 3 / 2 + 5]);
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x) assert(v.brightness(x, y) == old_yuv(yuv.data(), w, x, y));
}

// The grid sensing makes, the old way and through the reader, on a frame of the size the cameras give.
static void grid() {
  const int w = 1280, h = 720, GRID_W = 32, GRID_H = 18;
  std::vector<uint8_t> rgb(static_cast<size_t>(w) * h * 2), yuv(static_cast<size_t>(w) * h * 3 / 2);
  uint32_t seed = 12345;
  for (auto &b : rgb) b = static_cast<uint8_t>((seed = seed * 1103515245 + 12345) >> 16);
  for (auto &b : yuv) b = static_cast<uint8_t>((seed = seed * 1103515245 + 12345) >> 16);
  for (int layout = 0; layout < 2; ++layout) {
    const bool is_yuv = layout == 1;
    const uint8_t *data = is_yuv ? yuv.data() : rgb.data();
    const FrameReader reader(data, is_yuv ? Layout::YUV420_OUYY_EVYY : Layout::RGB565_LE, w, w * 2);
    const int cell_w = w / GRID_W, cell_h = h / GRID_H;
    for (int gy = 0; gy < GRID_H; gy++) {
      for (int gx = 0; gx < GRID_W; gx++) {
        uint32_t before = 0, now = 0;
        for (int sy = 0; sy < 4; sy++) {
          const int y = gy * cell_h + (2 * sy + 1) * cell_h / 8;
          for (int sx = 0; sx < 4; sx++) {
            const int x = gx * cell_w + (2 * sx + 1) * cell_w / 8;
            before += is_yuv ? old_yuv(data, w, x, y) : old_rgb565(data, w * 2, x, y);
            now += reader.brightness(x, y);
          }
        }
        assert(before == now);
      }
    }
  }
}

int main() {
  points();
  grid();
  std::puts("frame_reader: ok");
  return 0;
}
