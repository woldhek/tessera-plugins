// The video's sound: the microphone as Opus at 20 ms a frame, on the server's task, with its own clock against
// the camera's (RTCP in rtsp_server.cpp). Only in a build with a microphone.
#include "screen_video.h"
#ifdef USE_SCREEN_VIDEO_SOUND
#include "screen_video_shared.h"

#include <cstring>

#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "esphome/core/log.h"
#include "encoder/impl/esp_opus_enc.h"

namespace esphome::screen_video {

void ScreenVideo::setup_sound_() {
  if (mic_ == nullptr) return;
  const size_t internal_before = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
  sound_rate_ = mic_->get_audio_stream_info().get_sample_rate();
  // A second of sound between the microphone's task and the server's, in PSRAM: more than any pause of the sender.
  sound_ = xStreamBufferCreateWithCaps(sound_rate_ * 2, 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  // As Espressif's own WebRTC port drives this encoder: the library's defaults (complexity 0, no FEC), only the rate,
  // the channel and the bitrate set. esp_audio_codec 2.5.0: 2.6 and later build only for P4 chips of revision 3.
  esp_opus_enc_config_t opus = ESP_OPUS_ENC_CONFIG_DEFAULT();
  opus.sample_rate = sound_rate_;
  opus.channel = 1;
  opus.bitrate = 32000;
  opus.frame_duration = ESP_OPUS_ENC_FRAME_DURATION_20_MS;
  int out_bytes = 0;
  if (sound_ == nullptr || esp_opus_enc_open(&opus, sizeof(opus), &opus_) != ESP_AUDIO_ERR_OK ||
      esp_opus_enc_get_frame_size(opus_, &opus_in_bytes_, &out_bytes) != ESP_AUDIO_ERR_OK ||
      out_bytes > static_cast<int>(sizeof(opus_out_)) || opus_in_bytes_ <= 0 || opus_in_bytes_ > 1920) {
    ESP_LOGE(TAG, "No Opus encoder: the video goes without sound");
    opus_ = nullptr;
    return;
  }
  opus_out_bytes_ = out_bytes;
  ESP_LOGI(TAG, "Opus ready; it keeps %d bytes of internal RAM",
           (int) internal_before - (int) heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
  mic_->add_data_callback([this](const std::vector<uint8_t> &data) {
    mic_samples_ += data.size() / 2;
    mic_at_us_ = esp_timer_get_time();
    if (!wants_sound_.load()) return;
    // Whole chunks only. A partial write (an odd count of bytes once the buffer is nearly full) splits every later
    // 16-bit sample over two, and the sound is white noise until the next PLAY. A chunk that does not fit is dropped
    // whole, and its samples move the RTP clock on, so picture and sound stay in step.
    if (xStreamBufferSpacesAvailable(sound_) < data.size()) {
      sound_lost_ += data.size();
      sound_skipped_ += data.size() / 2;
      return;
    }
    xStreamBufferSend(sound_, data.data(), data.size(), 0);
  });
}

void ScreenVideo::send_sound_() {
  // Every whole 20 ms the microphone gave, as one Opus frame in one RTP packet (RFC 7587) to every viewer that set the
  // sound up: its timestamp counts at 48 kHz whatever the microphone's rate, 960 a frame.
  if (opus_ == nullptr) return;
  uint8_t raw[1920];
  uint8_t packet[4 + 12 + sizeof(opus_out_)];
  const uint32_t samples = opus_in_bytes_ / 2;
  while (xStreamBufferBytesAvailable(sound_) >= static_cast<size_t>(opus_in_bytes_)) {
    if (xStreamBufferReceive(sound_, raw, opus_in_bytes_, 0) != static_cast<size_t>(opus_in_bytes_)) break;
    if (sound_start_us_ == 0) {
      // When these samples were heard: the microphone's last delivery, less what it delivered after them.
      const uint32_t after = mic_samples_.load() - mic_samples_at_play_ - samples;
      sound_start_us_ = mic_at_us_.load() - static_cast<int64_t>(after) * 1000000 / sound_rate_ -
                        static_cast<int64_t>(samples) * 1000000 / sound_rate_;
      sound_start_ts_ = static_cast<uint32_t>(sound_start_us_ * 48 / 1000);
    }
    esp_audio_enc_in_frame_t in{raw, static_cast<uint32_t>(opus_in_bytes_)};
    esp_audio_enc_out_frame_t out{};
    out.buffer = opus_out_;
    out.len = opus_out_bytes_;
    const int64_t encoding = esp_timer_get_time();
    const esp_audio_err_t err = esp_opus_enc_process(opus_, &in, &out);
    opus_us_ += static_cast<uint32_t>(esp_timer_get_time() - encoding);
    sound_sent_ += sound_skipped_.exchange(0);  // the samples the microphone dropped: time passed without them
    const uint32_t ts = sound_start_ts_ + static_cast<uint32_t>(static_cast<uint64_t>(sound_sent_) * 48000 / sound_rate_);
    sound_sent_ += samples;
    if (err != ESP_AUDIO_ERR_OK || out.encoded_bytes == 0) continue;
    const size_t rtp_len = 12 + out.encoded_bytes;
    packet[0] = '$';
    packet[2] = rtp_len >> 8;
    packet[3] = rtp_len & 0xFF;
    uint8_t *hd = packet + 4;
    hd[0] = 0x80;
    hd[1] = 111;
    hd[4] = ts >> 24;
    hd[5] = ts >> 16;
    hd[6] = ts >> 8;
    hd[7] = ts;
    hd[8] = sound_ssrc_ >> 24;
    hd[9] = sound_ssrc_ >> 16;
    hd[10] = sound_ssrc_ >> 8;
    hd[11] = sound_ssrc_;
    memcpy(hd + 12, opus_out_, out.encoded_bytes);
    for (auto &v : viewers_) {
      if (v.fd < 0 || !v.playing || v.sound_channel < 0) continue;
      packet[1] = static_cast<uint8_t>(v.sound_channel);
      hd[2] = v.sound_seq >> 8;
      hd[3] = v.sound_seq & 0xFF;
      ++v.sound_seq;
      if (!send_all_(v.fd, packet, 4 + rtp_len)) drop_(v);
    }
    sound_packets_++;
    sound_octets_ += out.encoded_bytes;
    sound_report_packets_++;
  }
}

}  // namespace esphome::screen_video
#endif  // USE_SCREEN_VIDEO_SOUND
