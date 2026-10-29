#pragma once
// The camera's frames, shared (docs/PICTURE_ARCHITECTURE.md in the Tessera repository, section 5). esp_video_camera
// hands its raw frames to one consumer, and this hub is that one: Screen camera's light level and motion and the part
// Video's encoder add themselves in their setup() and switch themselves on and off, each frame goes to every consumer
// that is on, in the order they added themselves, on the camera's capture task, and the camera runs while any is on.
// Each consumer keeps its own pace (sensing reads a frame every 400 ms, the video 15 a second).
//
// The camera hands out one layout, set before it first starts: the first consumer to add itself names it, and a later
// one must read it. The video's setup() runs before sensing's (AFTER_WIFI before LATE), so with the part Video the
// frames are its YUV420, and sensing, which reads both, takes them; without it they are RGB565.
//
// The hub also picks how fast the sensor runs (frame_rate.h; Tessera's docs/PLATFORM_PLAN.md 4.6, "as it picks the
// frame rate"): each consumer says what it needs now (ask), Home Assistant's pictures and stream need full speed, and
// the sensor runs as fast as the most any asks, slow while none needs more than sensing. Which sensors can change their
// rate is the camera's business (esp_video_camera's camera_sensor.h), never a board's.
//
// A Full field of view on a sensor that halves only its columns (the OV02C10, Tessera's docs/CAMERA.md "The whole
// field of view") comes as a frame as tall as it is wide or taller: the hub halves its rows once, on the PPA, for every
// consumer, so each gets the whole field at its true shape; but only while a consumer takes the picture itself (the
// video while someone watches). Light level and motion take any frame, halved or not, so while only they read, the
// frame goes to them as the sensor made it and the PPA does nothing: the video is on from boot, and halving each frame
// for an encoder that throws it away cost the PPA and PSRAM about 11 MB a second on the Guition (2026-10-28).
#include <atomic>

#include "esphome/components/esp_video_camera/esp_video_camera.h"
#include "frame_rate.h"

namespace esphome::screen_camera {

class FrameHub : public esp_video_camera::RawVideoFrameConsumer {
 public:
  using Format = esp_video_camera::RawVideoPixelFormat;
  using Consumer = esp_video_camera::RawVideoFrameConsumer;
  static constexpr int MAX_CONSUMERS = 4;

  void set_camera(esp_video_camera::ESPVideoCamera *camera) { camera_ = camera; }
  // From a consumer's setup(), on the main loop. `either`: the consumer reads RGB565 and the video's YUV420 alike.
  // `any_size`: it reads any frame, a Full field of view halved or not (light level and motion); else it takes the
  // picture at its true shape, and gets frames only while it wants them (wants). False when the camera hands its frames
  // to another consumer already, or in a layout this one does not read.
  bool add(Consumer *consumer, Format format, bool either = false, bool any_size = false);
  // Whether a consumer of the picture takes frames now (from the main loop; true until it says otherwise): the video
  // while someone watches, and until its encoder is made.
  void wants(Consumer *consumer, bool wants);
  // On or off, from the main loop: the camera starts when the first consumer is on and stops when none is. False when
  // the consumer was not added, or the camera hands out no raw frames (a pipeline without them).
  bool set_on(Consumer *consumer, bool on);
  // How many frames a second a consumer needs now (frame_rate::NONE, SENSE or FULL), from the main loop: full at once,
  // slower once nobody has needed full speed for a while.
  void ask(Consumer *consumer, uint8_t fps);
  // Once a second from the main loop: the sensor's rate as the asks and Home Assistant's want it now, and ten seconds
  // after each change the frames a second that really came, in the log (what took, on whichever sensor).
  void tick(uint32_t now_ms);

  // On the camera's task.
  void consume_raw_video_frame(const esp_video_camera::RawVideoFrame &frame) override;
  // The picture turned 0 or 180 degrees (picture_turn.h), from the main loop: the sensor flips and mirrors its readout,
  // which the camera applies between frames on its task and again whenever it starts. The rotation lives in the sensor
  // on purpose: it costs nothing, and every consumer gets it, the stills too, without a turn in each one's PPA step.
  void turn(int degrees);
  // How often the picture turned since boot, for a consumer on the camera's task: the video asks for a key frame, sensing
  // starts its motion grid again.
  uint32_t turns() const { return turns_.load(std::memory_order_acquire); }

 protected:
  struct Entry {
    std::atomic<Consumer *> consumer{nullptr};
    std::atomic<bool> on{false};
    std::atomic<bool> wants{true};  // a consumer of the picture takes frames now
    bool any_size{false};
    uint8_t asks{frame_rate::NONE};  // main loop
  };
  Entry *find_(Consumer *consumer);
  bool any_on_() const;
  void pick_rate_(uint32_t now_ms);
  // A Full field of view's frame, its rows halved into half_ (camera's task); false when there is no room or no PPA.
  bool halve_(const esp_video_camera::RawVideoFrame &frame, esp_video_camera::RawVideoFrame &half);

  esp_video_camera::ESPVideoCamera *camera_{nullptr};
  Entry entries_[MAX_CONSUMERS];
  std::atomic<int> count_{0};  // entries added; the camera's task reads up to it
  bool registered_{false};
  int turn_{0};
  std::atomic<uint32_t> turns_{0};
  Format format_{Format::RGB565_LE};
  // The sensor's rate (main loop): the one asked of the camera, and when full speed was last needed.
  uint8_t rate_{frame_rate::FULL};
  uint32_t full_at_ms_{0};
  // The frames that came since boot (camera's task), and the count after a change (main loop): from when, and how many
  // had come by then; 0 for no count running. It starts a second after the change, once the sensor took it.
  std::atomic<uint32_t> frames_{0}, halved_{0};
  uint32_t count_from_ms_{0}, counted_{0}, halved_counted_{0};
  bool counting_{false};
  // The halved frame (camera's task): its PPA client with bursts of 64 bytes, as every PPA client on a screen, and its
  // buffer in PSRAM, made at the first tall frame.
  ppa_client_handle_t ppa_{nullptr};
  uint8_t *half_{nullptr};
  size_t half_bytes_{0};
  bool half_told_{false};
};

}  // namespace esphome::screen_camera
