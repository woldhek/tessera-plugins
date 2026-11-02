#pragma once
// A camera frame as a consumer reads it (Screen camera, the frame hub's consumers): the brightness of a point, whatever
// the layout the camera hands out, so no consumer parses a layout of its own.
//
// Pure, free of ESPHome and ESP-IDF: tests/test_frame_reader.cpp drives it on a PC.
#include <cstddef>
#include <cstdint>

namespace esphome::screen_camera {

// The layouts the camera hands out (esp_video_camera::RawVideoPixelFormat): RGB565 in little-endian words, and the
// ESP32-P4 H.264 encoder's own YUV420 (O_UYY_E_VYY: a line of U Y Y ..., a line of V Y Y ..., three bytes for two
// points), which the part Video asks for.
enum class Layout : uint8_t { RGB565_LE, YUV420_OUYY_EVYY };

class FrameReader {
 public:
  // `stride_bytes` is an RGB565 frame's line; a YUV420 line is always width x 3 / 2.
  FrameReader(const uint8_t *data, Layout layout, int width, size_t stride_bytes)
      : data_(data), layout_(layout), yuv_row_(static_cast<size_t>(width) * 3 / 2), stride_(stride_bytes) {}

  // The brightness of the point (x, y), 0 to 255: its Y in the YUV420 (no colour to weigh), or BT.601 luma from the
  // RGB565's 5-6-5 bits (R and B times 8, G times 4, to 0..255).
  uint32_t brightness(int x, int y) const {
    if (layout_ == Layout::YUV420_OUYY_EVYY) {
      const uint8_t *row = data_ + static_cast<size_t>(y) * yuv_row_;
      return row[(x / 2) * 3 + 1 + (x & 1)];
    }
    const auto *row = reinterpret_cast<const uint16_t *>(data_ + static_cast<size_t>(y) * stride_);
    const uint16_t p = row[x];
    const uint32_t r = (p >> 11) & 0x1F, g = (p >> 5) & 0x3F, b = p & 0x1F;
    return (r * 8 * 77 + g * 4 * 150 + b * 8 * 29) >> 8;
  }

 private:
  const uint8_t *data_;
  Layout layout_;
  size_t yuv_row_, stride_;
};

}  // namespace esphome::screen_camera
