#pragma once
// What the files of the video share (screen_video.cpp, video_encoder.cpp, rtsp_server.cpp, opus_sound.cpp): the log's
// tag and the one way its big buffers are taken.
#include <cstddef>

#include "esp_heap_caps.h"

namespace esphome::screen_video {

static const char *const TAG = "screen_video";

// A buffer the PPA and the encoder reach by DMA, in PSRAM: internal RAM is what the screen runs short of.
static inline void *psram_dma(size_t bytes) {
  return heap_caps_aligned_calloc(64, 1, bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT | MALLOC_CAP_DMA);
}

}  // namespace esphome::screen_video
