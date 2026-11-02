#pragma once
// The screen's own camera as live video: H.264 from the ESP32-P4's hardware encoder and the microphone as Opus, served
// over RTSP for Home Assistant (its go2rtc turns it into WebRTC without transcoding). See __init__.py and README.md,
// "Video". Every number below was measured on a reTerminal D1001; the comments say what went wrong without it.
#include <atomic>
#include <cstdint>
#include <string>

#include "esphome/core/component.h"
#include "esphome/core/defines.h"
#include "esphome/components/esp_video_camera/esp_video_camera.h"
#include "esphome/components/screen_camera/screen_camera.h"
#ifdef USE_SCREEN_VIDEO_SOUND
#include "esphome/components/microphone/microphone_source.h"
#include <freertos/stream_buffer.h>
#endif

#include "driver/ppa.h"
#include "esp_h264_enc_single_hw.h"

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

namespace esphome::screen_video {

class ScreenVideo : public Component, public esp_video_camera::RawVideoFrameConsumer {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::AFTER_WIFI; }

  void set_camera(esp_video_camera::ESPVideoCamera *c) { camera_ = c; }
  void set_screen_camera(screen_camera::ScreenCamera *s) { screen_camera_ = s; }
  void set_port(uint16_t p) { port_ = p; }
  void set_password(const char *p) { password_ = p; }
#ifdef USE_SCREEN_VIDEO_SOUND
  void set_microphone_source(microphone::MicrophoneSource *m) { mic_ = m; }
#endif

  // On the camera's capture task, from Screen camera's frame hub, for every frame from boot on.
  void consume_raw_video_frame(const esp_video_camera::RawVideoFrame &frame) override;

 protected:
  // The stream: the camera's own size, 15 frames a second, 1 Mbit/s, a key frame every second. The encoder wants
  // width and height in whole macroblocks (16), so a camera whose size is not (the OV02C10's 1288 x 728) has the edge
  // of its frames cut off on the way: its middle, 1280 x 720. The encoder is made for that size once, at the first
  // frame, and keeps it (a viewer never chooses another). Its reference frame lives in internal RAM and grows with the
  // size, three rows of macroblocks across (92 KB at 1280 wide). It stays there: in PSRAM its traffic shares the memory
  // bus with the panel's frame buffer, and the panel ran dry dozens of times a minute (blue frames over the live tiles
  // of a reTerminal D1001, 2026-10-25). The video takes no camera above MAX_PIXELS: that sensor's smaller mode goes in
  // the part's YAML (esp_video_camera `resolution`), never in a board file.
  static constexpr uint32_t MAX_PIXELS = 1280UL * 800UL;
  static constexpr uint8_t FRAMERATE = 15, GOP = 15;
  static constexpr uint32_t BITRATE = 1000000;
  // One access unit at most. A key frame of a dark, noisy room is over 200 KB, and an encoder whose output did not fit
  // stays stuck for good, so this is generous.
  static constexpr size_t ENCODED_BYTES = 1024 * 1024;
  // Access units between the encoder (camera's task) and the sender (the server's task).
  static constexpr int SLOTS = 3;
  // go2rtc, Home Assistant's own stream (HLS and stills), and one more. With one, Home Assistant's own connection
  // waited beside go2rtc's and its frontend fell back to HLS: three seconds late and no sound.
  static constexpr int MAX_VIEWERS = 3;
  // RTP packets of an access unit are gathered and sent in one go, in PSRAM: internal RAM is the Wi-Fi's and lwIP's,
  // and below about 90 KB of it free the whole chip stalled now and then.
  static constexpr size_t OUT_BYTES = 64 * 1024;
  // What a viewer's connection holds unsent (lwIP's send buffer, 64 KB with Tessera's P4 setting) lives in internal
  // RAM, and so does every packet the Wi-Fi chip hands over: with three viewers, live camera tiles on the glass and the
  // voice assistant, internal RAM ran out and the Wi-Fi driver's next packet aborted the chip. Below this much free the
  // video skips frames, and starts again with a key frame once there is room.
  static constexpr size_t INTERNAL_LOW = 80 * 1024, INTERNAL_AGAIN = 110 * 1024;

  struct Slot {
    uint8_t *data{nullptr};
    uint32_t length{0};
    uint32_t timestamp{0};
    std::atomic<uint8_t> state{0};  // 0 free, 1 filled, 2 sending
  };
  struct Viewer {
    int fd{-1};
    std::string pending;
    bool playing{false};
    int video_channel{0};
    int sound_channel{-1};
    uint16_t seq{0}, sound_seq{0};
    uint32_t video_packets{0}, video_octets{0};
    uint32_t silent_since{0}, reports_at{0};
    bool left_behind{false};  // its connection was full: no picture until a key frame finds room (serve_)
    // The rest of a sound packet the connection took only part of (send_sound_, MSG_DONTWAIT): it goes first, before
    // anything else, so the interleaved stream stays whole (unsent_first_).
    std::string sound_unsent;
  };

  static void server_task_(void *self);
  void serve_();
  void accept_(int listener);
  void read_(Viewer &v);
  bool request_(Viewer &v, const std::string &request);
  void drop_(Viewer &v);
  bool anyone_playing_() const;
  bool send_all_(int fd, const uint8_t *data, size_t length);
  bool writable_(int fd);
  bool unsent_first_(Viewer &v);
  bool queue_(int fd, const uint8_t *data, size_t length);
  bool flush_(int fd);
  bool send_access_unit_(Viewer &v, const Slot &slot);
  bool send_reports_(Viewer &v);
#ifdef USE_SCREEN_VIDEO_SOUND
  void setup_sound_();
  void send_sound_();
#endif

  esp_video_camera::ESPVideoCamera *camera_{nullptr};
  screen_camera::ScreenCamera *screen_camera_{nullptr};
  uint16_t port_{8554};
  const char *password_{""};
  std::string expected_auth_;

  // The camera's task owns the encoder, and the size it was made for.
  esp_h264_enc_handle_t encoder_{nullptr};
  bool encoder_failed_{false};
  uint16_t width_{0}, height_{0};         // the stream's size: the camera's, cut to whole macroblocks
  uint16_t made_w_{0}, made_h_{0};        // the camera's frame size the encoder was made for
  uint16_t crop_x_{0}, crop_y_{0};        // where the stream's frame starts in the camera's, 0 when they are one
  uint8_t *crop_{nullptr};                // the cut frame, in PSRAM, only when the camera's size needs cutting
  ppa_client_handle_t ppa_{nullptr};      // what cuts it: the P4's pixel DMA engine (a memcpy took 77 ms a frame)
  std::atomic<uint32_t> behind_{0};       // pictures a viewer did not get for a full connection (serve_)
  std::atomic<uint32_t> crop_us_{0};      // what the cutting costs, for the loop's line
  std::atomic<uint32_t> encode_us_{0};    // and the encoding
  uint32_t reported_crop_us_{0}, reported_encode_us_{0};
  uint32_t last_frame_us_{0};
  uint32_t last_arrival_us_{0};           // the camera's last frame, and its frame time smoothed (the video's pace)
  uint32_t camera_us_{0};
  uint32_t seen_turns_{0};                // the hub's turns the stream follows (video_encoder.cpp)
  uint8_t idr_in_{0};                     // frames until the second key frame after a turn
  // The internal RAM the encoder took when it was made, for the loop's line (the camera's task writes it before its
  // first frame).
  std::atomic<int> encoder_internal_{0};
  static constexpr uint8_t TURN_IDR_FRAMES = 4;
  Slot slots_[SLOTS];
  QueueHandle_t ready_{nullptr};  // indices of filled slots, oldest first

  // The server's task owns the viewers and the send batch.
  Viewer viewers_[MAX_VIEWERS];
  uint8_t *out_{nullptr};
  size_t out_len_{0};
  uint32_t ssrc_{0x54455353};

  std::atomic<bool> playing_{false};  // a viewer plays: the camera's task encodes
  // The encoder is made (camera's task); and whether the frame hub was last told the video takes frames (main loop).
  std::atomic<bool> encoder_made_{false};
  bool picture_said_{true};
  // The frames a busy screen asked the video to leave out (plugin API 0.21, tessera::squeeze), counted on the camera's
  // task: the n-th frame offered, and how many were left out since boot.
  uint32_t offered_{0};
  std::atomic<uint32_t> left_out_{0};
  std::atomic<bool> want_idr_{false};
  bool watched_{false};                // what the loop last told Screen camera (its dot)

  // What the tasks count, for the loop's line every minute while someone watches.
  std::atomic<uint32_t> frames_{0}, bytes_{0}, dropped_{0}, failed_{0}, held_{0};
  bool holding_{false};  // the server's task: internal RAM is low, frames are skipped
  uint32_t reported_ms_{0}, reported_frames_{0}, reported_bytes_{0};

#ifdef USE_SCREEN_VIDEO_SOUND
  // 20 ms of sound an Opus frame, at the microphone's own rate (16 kHz on every screen with one).
  microphone::MicrophoneSource *mic_{nullptr};
  StreamBufferHandle_t sound_{nullptr};   // little-endian samples, from the microphone's task to the server's
  std::atomic<bool> wants_sound_{false};  // a viewer set the sound up and plays: the loop starts the microphone
  bool listening_{false};
  uint32_t sound_rate_{16000};
  uint32_t sound_ssrc_{0x54455354};
  void *opus_{nullptr};
  uint8_t opus_out_[1500];
  int opus_in_bytes_{0}, opus_out_bytes_{0};
  std::atomic<uint32_t> sound_packets_{0}, sound_lost_{0}, sound_skipped_{0};
  std::atomic<uint32_t> sound_behind_{0};  // sound packets a viewer did not get for a full connection (send_sound_)
  std::atomic<uint32_t> opus_us_{0};  // what the Opus encoder took, for the loop's Sound line
  uint32_t reported_opus_us_ = 0;
  uint32_t reported_sound_{0};
  // The sound's clock: when the first packet's samples were heard (esp_timer, us) and its RTP timestamp (48 kHz).
  int64_t sound_start_us_{0};
  uint32_t sound_start_ts_{0};
  uint32_t sound_sent_{0};  // samples sent since PLAY, at the microphone's rate
  uint32_t sound_octets_{0}, sound_report_packets_{0};
  // The microphone's last delivery: when, and how many samples it had delivered in all by then.
  std::atomic<int64_t> mic_at_us_{0};
  std::atomic<uint32_t> mic_samples_{0};
  uint32_t mic_samples_at_play_{0};
#endif
};

}  // namespace esphome::screen_video
