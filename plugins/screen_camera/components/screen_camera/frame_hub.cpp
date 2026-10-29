#include "frame_hub.h"

#include "esp_heap_caps.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"

namespace esphome::screen_camera {

static const char *const TAG = "frame_hub";

bool FrameHub::add(Consumer *consumer, Format format, bool either, bool any_size) {
  if (consumer == nullptr || camera_ == nullptr) return false;
  if (!registered_) {
    if (!camera_->register_raw_frame_consumer(this, format)) return false;
    registered_ = true;
    format_ = format;
  } else if (format != format_ && !either) {
    return false;
  }
  const int n = count_.load(std::memory_order_relaxed);
  if (find_(consumer) != nullptr) return true;
  if (n >= MAX_CONSUMERS) return false;
  entries_[n].consumer.store(consumer, std::memory_order_relaxed);
  entries_[n].any_size = any_size;
  count_.store(n + 1, std::memory_order_release);
  return true;
}

void FrameHub::wants(Consumer *consumer, bool wants) {
  if (Entry *entry = find_(consumer)) entry->wants.store(wants, std::memory_order_release);
}

FrameHub::Entry *FrameHub::find_(Consumer *consumer) {
  const int n = count_.load(std::memory_order_acquire);
  for (int i = 0; i < n; ++i)
    if (entries_[i].consumer.load(std::memory_order_relaxed) == consumer) return &entries_[i];
  return nullptr;
}

bool FrameHub::any_on_() const {
  const int n = count_.load(std::memory_order_acquire);
  for (int i = 0; i < n; ++i)
    if (entries_[i].on.load(std::memory_order_acquire)) return true;
  return false;
}

bool FrameHub::set_on(Consumer *consumer, bool on) {
  Entry *entry = find_(consumer);
  if (entry == nullptr) return false;
  if (on) {
    const bool running = any_on_();
    entry->on.store(true, std::memory_order_release);
    // The camera runs already for another consumer: nothing to ask of it.
    if (running || camera_->start_raw_frame_consumer(this)) return true;
    entry->on.store(false, std::memory_order_release);
    return false;
  }
  entry->on.store(false, std::memory_order_release);
  if (!any_on_()) camera_->stop_raw_frame_consumer(this);
  return true;
}

void FrameHub::ask(Consumer *consumer, uint8_t fps) {
  Entry *entry = find_(consumer);
  if (entry == nullptr || entry->asks == fps) return;
  entry->asks = fps;
  pick_rate_(millis());
}

void FrameHub::tick(uint32_t now_ms) {
  pick_rate_(now_ms);
  if (count_from_ms_ == 0 || static_cast<int32_t>(now_ms - count_from_ms_) < 0) return;
  if (!counting_) {
    counting_ = true;
    count_from_ms_ = now_ms | 1;
    counted_ = frames_.load(std::memory_order_relaxed);
    halved_counted_ = halved_.load(std::memory_order_relaxed);
  } else if (now_ms - count_from_ms_ >= 10000) {
    // Only while it runs: a camera no consumer has on makes none, whatever its rate.
    const uint32_t frames = frames_.load(std::memory_order_relaxed) - counted_;
    const uint32_t halved = halved_.load(std::memory_order_relaxed) - halved_counted_;
    if (any_on_())
      ESP_LOGI(TAG, "The camera made %.1f frames a second over 10 s, %.1f of them halved on the PPA",
               frames * 1000.0f / (now_ms - count_from_ms_), halved * 1000.0f / (now_ms - count_from_ms_));
    count_from_ms_ = 0;
  }
}

void FrameHub::pick_rate_(uint32_t now_ms) {
  if (camera_ == nullptr) return;
  // The most any consumer that is on asks, and full speed while Home Assistant takes pictures or a stream.
  uint8_t asked = camera_->requested() ? frame_rate::FULL : frame_rate::NONE;
  const int n = count_.load(std::memory_order_acquire);
  for (int i = 0; i < n; ++i)
    if (entries_[i].on.load(std::memory_order_acquire) && entries_[i].asks > asked) asked = entries_[i].asks;
  const uint8_t rate = frame_rate::choose(asked, now_ms, full_at_ms_);
  if (rate == rate_) return;
  if (rate == frame_rate::FULL)
    ESP_LOGI(TAG, "The camera at its own frame rate: someone watches");
  else
    ESP_LOGI(TAG, "The camera at %u frames a second: nobody watches", (unsigned) rate);
  rate_ = rate;
  camera_->set_frame_rate(rate == frame_rate::FULL ? 0 : rate);
  count_from_ms_ = (now_ms + 1000) | 1;
  counting_ = false;
}

void FrameHub::turn(int degrees) {
  if (camera_ == nullptr || degrees == turn_) return;
  turn_ = degrees;
  camera_->set_runtime_vflip(degrees == 180);
  camera_->set_runtime_hflip(degrees == 180);
  turns_.fetch_add(1, std::memory_order_release);
}

bool FrameHub::halve_(const esp_video_camera::RawVideoFrame &frame, esp_video_camera::RawVideoFrame &half) {
  // The middle of the frame's rows in whole macroblocks of 16, halved: 1056 of 1080 into 528, so the video's encoder
  // takes the frame without a cut of its own (one pass on the PPA for the video, not two).
  const bool yuv = frame.pixel_format == Format::YUV420_OUYY_EVYY;
  const uint16_t width = frame.width;
  const uint16_t height = (frame.height / 2) & ~15;
  const size_t bytes = yuv ? static_cast<size_t>(width) * height * 3 / 2 : static_cast<size_t>(width) * height * 2;
  if (half_ == nullptr || half_bytes_ < bytes) {
    if (half_ != nullptr) heap_caps_free(half_);
    half_ = static_cast<uint8_t *>(
        heap_caps_aligned_calloc(64, 1, bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT | MALLOC_CAP_DMA));
    half_bytes_ = half_ != nullptr ? bytes : 0;
  }
  if (ppa_ == nullptr && half_ != nullptr) {
    ppa_client_config_t client{};
    client.oper_type = PPA_OPERATION_SRM;
    client.max_pending_trans_num = 1;
    client.data_burst_length = PPA_DATA_BURST_LENGTH_64;
    if (ppa_register_client(&client, &ppa_) != ESP_OK) ppa_ = nullptr;
  }
  if (half_ == nullptr || ppa_ == nullptr) {
    static bool said = false;
    if (!said) ESP_LOGE(TAG, "No memory or PPA to halve a Full field of view's rows");
    said = true;
    return false;
  }
  ppa_srm_oper_config_t op{};
  op.in.buffer = frame.data;
  op.in.pic_w = frame.width;
  op.in.pic_h = frame.height;
  op.in.block_w = width;
  op.in.block_h = height * 2;
  op.in.block_offset_y = ((frame.height - height * 2) / 2) & ~1;
  op.in.srm_cm = yuv ? PPA_SRM_COLOR_MODE_YUV420 : PPA_SRM_COLOR_MODE_RGB565;
  op.in.yuv_range = PPA_COLOR_RANGE_LIMIT;
  op.in.yuv_std = PPA_COLOR_CONV_STD_RGB_YUV_BT601;
  op.out.buffer = half_;
  op.out.buffer_size = bytes;
  op.out.pic_w = width;
  op.out.pic_h = height;
  op.out.srm_cm = op.in.srm_cm;
  op.out.yuv_range = PPA_COLOR_RANGE_LIMIT;
  op.out.yuv_std = PPA_COLOR_CONV_STD_RGB_YUV_BT601;
  op.rotation_angle = PPA_SRM_ROTATION_ANGLE_0;
  op.scale_x = 1.0f;
  op.scale_y = 0.5f;
  op.mode = PPA_TRANS_MODE_BLOCKING;
  if (ppa_do_scale_rotate_mirror(ppa_, &op) != ESP_OK) return false;
  if (!half_told_) {
    half_told_ = true;
    ESP_LOGI(TAG, "A Full field of view: %ux%u from the sensor, its rows halved into %ux%u for every consumer",
             frame.width, frame.height, width, height);
  }
  half = frame;
  half.data = half_;
  half.size = bytes;
  half.height = height;
  return true;
}

void FrameHub::consume_raw_video_frame(const esp_video_camera::RawVideoFrame &frame) {
  frames_.fetch_add(1, std::memory_order_relaxed);
  const int n = count_.load(std::memory_order_acquire);
  // Whether a consumer of the picture takes this frame: only then is a Full field of view's frame halved.
  bool picture = false;
  for (int i = 0; i < n; ++i)
    picture |= entries_[i].on.load(std::memory_order_acquire) && !entries_[i].any_size &&
               entries_[i].wants.load(std::memory_order_acquire);
  const esp_video_camera::RawVideoFrame *out = &frame;
  esp_video_camera::RawVideoFrame half;
  const bool tall = frame.height >= frame.width && frame.width >= 16;
  if (tall && picture) {
    if (!halve_(frame, half)) return;
    halved_.fetch_add(1, std::memory_order_relaxed);
    out = &half;
  }
  for (int i = 0; i < n; ++i) {
    if (!entries_[i].on.load(std::memory_order_acquire)) continue;
    // A consumer of the picture gets the frame while it wants it, and never a tall one unhalved.
    if (!entries_[i].any_size && (!entries_[i].wants.load(std::memory_order_acquire) || (tall && out == &frame))) continue;
    Consumer *consumer = entries_[i].consumer.load(std::memory_order_relaxed);
    if (consumer != nullptr) consumer->consume_raw_video_frame(*out);
  }
}

}  // namespace esphome::screen_camera
