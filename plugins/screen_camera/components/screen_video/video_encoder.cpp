// The video's frame path, on the camera's capture task: a frame from Screen camera's frame hub, cut by the PPA to
// whole macroblocks when the camera's size is not, H.264 from the P4's encoder, into a slot for the server.
#include "screen_video.h"
#include "screen_video_shared.h"

#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "esphome/core/log.h"

namespace esphome::screen_video {

// ---- The camera's task: a frame, H.264 ----
void ScreenVideo::consume_raw_video_frame(const esp_video_camera::RawVideoFrame &frame) {
  if (frame.pixel_format != esp_video_camera::RawVideoPixelFormat::YUV420_OUYY_EVYY || frame.stride_bytes != frame.width)
    return;
  if (encoder_ != nullptr && (frame.width != made_w_ || frame.height != made_h_)) {
    // The camera changed its mode (Screen camera's field of view): the encoder, and the cut, for the new size. Its
    // reference frame goes back to internal RAM first; the new one is smaller or the same for every mode offered.
    esp_h264_enc_close(encoder_);
    esp_h264_enc_del(encoder_);
    encoder_ = nullptr;
    if (ppa_ != nullptr) {
      ppa_unregister_client(ppa_);
      ppa_ = nullptr;
    }
    if (crop_ != nullptr) {
      heap_caps_free(crop_);
      crop_ = nullptr;
    }
    crop_x_ = crop_y_ = 0;
    encoder_failed_ = false;
    ESP_LOGI(TAG, "The camera gives %ux%u frames now (%ux%u before): a new encoder", frame.width, frame.height, made_w_,
             made_h_);
  }
  if (encoder_ == nullptr && !encoder_failed_) {
    made_w_ = frame.width;
    made_h_ = frame.height;
    // The stream's size: the camera's, cut to whole macroblocks, its middle. The camera's frame is the encoder's own
    // YUV420 (a line of U Y Y..., a line of V Y Y..., 3 bytes for 2 pixels): a cut starts on an even line, so the
    // lines keep their U and V, and on an even pixel, so the groups of two stay whole.
    width_ = frame.width & ~15;
    height_ = frame.height & ~15;
    if (width_ < 160 || height_ < 96 || static_cast<uint32_t>(width_) * height_ > MAX_PIXELS) {
      ESP_LOGE(TAG, "The camera gives %ux%u frames; the video takes %s. Give the camera a smaller mode in the part's "
               "YAML (esp_video_camera resolution).",
               frame.width, frame.height, width_ < 160 || height_ < 96 ? "nothing this small" : "at most 1280x800");
      encoder_failed_ = true;
      return;
    }
    if (width_ != frame.width || height_ != frame.height) {
      crop_x_ = ((frame.width - width_) / 2) & ~1;
      crop_y_ = ((frame.height - height_) / 2) & ~1;
      crop_ = static_cast<uint8_t *>(psram_dma(static_cast<size_t>(width_) * height_ * 3 / 2));
      // The PPA cuts it, by DMA: a memcpy of the frame on the camera's task took 77 ms (the camera's buffer is not
      // cached). Its burst of 64 bytes, as every PPA client on a screen: the panel keeps its share of the bus.
      ppa_client_config_t client{};
      client.oper_type = PPA_OPERATION_SRM;
      client.max_pending_trans_num = 1;
      client.data_burst_length = PPA_DATA_BURST_LENGTH_64;
      if (crop_ == nullptr || ppa_register_client(&client, &ppa_) != ESP_OK) {
        ESP_LOGE(TAG, "No memory or PPA for the cut frame");
        encoder_failed_ = true;
        return;
      }
    }
    const size_t internal_before = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    esp_h264_enc_cfg_hw_t cfg{};
    cfg.pic_type = ESP_H264_RAW_FMT_O_UYY_E_VYY;
    cfg.gop = GOP;
    cfg.fps = FRAMERATE;
    cfg.res.width = width_;
    cfg.res.height = height_;
    cfg.rc.bitrate = BITRATE;
    cfg.rc.qp_min = 25;
    // The rate control holds BITRATE with this cap: on a Guition 10.1 with fresh noise over half of every frame it sent
    // 983 to 1006 kbit/s at 42 and 1001 to 1002 at 51 (2026-10-29), so the whole range (0.6.11) bought nothing.
    cfg.rc.qp_max = 42;
    if (esp_h264_enc_hw_new(&cfg, &encoder_) != ESP_H264_ERR_OK || encoder_ == nullptr ||
        esp_h264_enc_open(encoder_) != ESP_H264_ERR_OK) {
      ESP_LOGE(TAG, "The H.264 encoder did not start (internal RAM free: %u bytes)",
               (unsigned) heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
      encoder_failed_ = true;
      encoder_ = nullptr;
      return;
    }
    const int kept = (int) internal_before - (int) heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    encoder_internal_ = kept;
    encoder_made_.store(true, std::memory_order_release);
    if (crop_ != nullptr) {
      ESP_LOGW(TAG, "H.264 encoder ready at %ux%u, the middle of the camera's %ux%u; it keeps %d bytes of internal RAM. "
               "The cut costs the camera's task about 60 ms a frame (fewer frames a second): a camera mode in whole "
               "macroblocks of 16 is better",
               width_, height_, frame.width, frame.height, kept);
    } else {
      ESP_LOGI(TAG, "H.264 encoder ready at %ux%u, the camera's own size; it keeps %d bytes of internal RAM", width_,
               height_, kept);
    }
  }
  if (encoder_ == nullptr || !playing_.load()) return;
  if (frame.width < crop_x_ + width_ || frame.height < crop_y_ + height_) return;  // a frame of another size: not ours
  const uint32_t now = static_cast<uint32_t>(esp_timer_get_time());
  // The camera's frame time, smoothed over the frames as they come: a frame may come a third of it early, so a camera
  // at 30 frames a second gives the video every other frame and one at 15 (a Full field of view) every frame, however
  // its frames jitter. A fixed 3 ms let a 15-a-second camera's frames fall through.
  // A gap of half a second or more is a pause (nobody watched), not the camera's pace.
  const uint32_t gap = now - last_arrival_us_;
  if (last_arrival_us_ != 0 && gap < 500000U) camera_us_ = camera_us_ == 0 ? gap : (3 * camera_us_ + gap) / 4;
  last_arrival_us_ = now;
  if (last_frame_us_ != 0 && now - last_frame_us_ + camera_us_ / 3 < 1000000U / FRAMERATE) return;
  last_frame_us_ = now;
  // A busy screen (plugin API 0.21): every other frame of the video's own pace, or one in four, while a finger is on
  // the glass or the screen shows signs of trouble. After the pace above, which already leaves out every other frame of
  // a camera at 30 frames a second. The viewer sees fewer frames a second, never a broken one: the encoder only gets
  // fewer.
  const uint8_t squeeze = screen_camera_->video_squeeze();
  if (squeeze > 0 && (squeeze == 1 ? offered_++ % 2 : offered_++ % 4) != 0) {
    left_out_.fetch_add(1, std::memory_order_relaxed);
    return;
  }
  // A frame dropped before it is encoded leaves the encoder's references whole: the stream goes on undamaged.
  int index = -1;
  for (int i = 0; i < SLOTS && index < 0; ++i) {
    uint8_t free = 0;
    if (slots_[i].state.compare_exchange_strong(free, 1)) index = i;
  }
  if (index < 0) {
    dropped_++;
    return;
  }
  Slot &slot = slots_[index];
  // The picture turned (Screen camera's frame hub): the frames after it would point back at pictures the other way
  // round until a key frame, and the camera's queue may still hold one or two of those. A key frame now, and one a few
  // frames on.
  const uint32_t turns = screen_camera_->frames().turns();
  if (turns != seen_turns_) {
    seen_turns_ = turns;
    want_idr_ = true;
    idr_in_ = TURN_IDR_FRAMES;
  } else if (idr_in_ > 0 && --idr_in_ == 0) {
    want_idr_ = true;
  }
  if (want_idr_.exchange(false)) {
    esp_h264_enc_param_hw_handle_t param = nullptr;
    if (esp_h264_enc_hw_get_param_hd(encoder_, &param) == ESP_H264_ERR_OK && param != nullptr)
      esp_h264_enc_force_idr(&param->base);
  }
  const uint8_t *picture = frame.data;
  if (crop_ != nullptr) {
    // The cut: the middle block of the camera's frame, YUV420 in and out, no scaling, by the PPA.
    const int64_t started = esp_timer_get_time();
    ppa_srm_oper_config_t cut{};
    cut.in.buffer = frame.data;
    cut.in.pic_w = frame.width;
    cut.in.pic_h = frame.height;
    cut.in.block_w = width_;
    cut.in.block_h = height_;
    cut.in.block_offset_x = crop_x_;
    cut.in.block_offset_y = crop_y_;
    cut.in.srm_cm = PPA_SRM_COLOR_MODE_YUV420;
    cut.in.yuv_range = PPA_COLOR_RANGE_LIMIT;
    cut.in.yuv_std = PPA_COLOR_CONV_STD_RGB_YUV_BT601;
    cut.out.buffer = crop_;
    cut.out.buffer_size = static_cast<size_t>(width_) * height_ * 3 / 2;
    cut.out.pic_w = width_;
    cut.out.pic_h = height_;
    cut.out.srm_cm = PPA_SRM_COLOR_MODE_YUV420;
    cut.out.yuv_range = PPA_COLOR_RANGE_LIMIT;
    cut.out.yuv_std = PPA_COLOR_CONV_STD_RGB_YUV_BT601;
    // No turn here: the sensor turns the picture (FrameHub::turn), for every consumer at once and at no cost.
    cut.rotation_angle = PPA_SRM_ROTATION_ANGLE_0;
    cut.scale_x = cut.scale_y = 1.0f;
    cut.mode = PPA_TRANS_MODE_BLOCKING;
    const bool ok = ppa_do_scale_rotate_mirror(ppa_, &cut) == ESP_OK;
    crop_us_ += static_cast<uint32_t>(esp_timer_get_time() - started);
    if (!ok) {
      failed_++;
      slot.state = 0;
      return;
    }
    picture = crop_;
  }
  esp_h264_enc_in_frame_t in{};
  in.raw_data.buffer = const_cast<uint8_t *>(picture);
  in.raw_data.len = static_cast<uint32_t>(width_) * height_ * 3 / 2;
  in.pts = frame.timestamp_90khz;
  esp_h264_enc_out_frame_t out{};
  out.raw_data.buffer = slot.data;
  out.raw_data.len = ENCODED_BYTES;
  const int64_t encoding = esp_timer_get_time();
  const bool encoded = esp_h264_enc_process(encoder_, &in, &out) == ESP_H264_ERR_OK;
  encode_us_ += static_cast<uint32_t>(esp_timer_get_time() - encoding);
  if (!encoded || out.length == 0 || out.length > ENCODED_BYTES) {
    failed_++;
    slot.state = 0;
    return;
  }
  slot.length = out.length;
  slot.timestamp = frame.timestamp_90khz;
  frames_++;
  bytes_ += out.length;
  if (xQueueSend(ready_, &index, 0) != pdTRUE) slot.state = 0;
}

}  // namespace esphome::screen_video
