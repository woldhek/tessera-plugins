// The video's server, on a task of its own: RTSP for up to MAX_VIEWERS viewers, RTP over each one's connection, the
// access units the encoder left in its slots, RTCP sender reports.
#include "screen_video.h"
#include "screen_video_shared.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <string>

#include <lwip/sockets.h>
#include <mbedtls/base64.h>
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"
#ifdef USE_SCREEN_VIDEO_SOUND
#include "encoder/impl/esp_opus_enc.h"
#endif

namespace esphome::screen_video {

static constexpr size_t RTP_PAYLOAD = 1400;  // an RTP packet's payload at most: under any Wi-Fi MTU

// ---- RTSP: RTP over each viewer's own connection (RFC 2326 10.12), H.264 as RFC 6184 (single NAL units, FU-A) ----
void ScreenVideo::server_task_(void *self) {
  static_cast<ScreenVideo *>(self)->serve_();
  // Only when the port could not be opened. A task made with its stack in PSRAM is not deleted from itself: it rests.
  while (true) vTaskDelay(portMAX_DELAY);
}

bool ScreenVideo::queue_(int fd, const uint8_t *data, size_t length) {
  if (length > OUT_BYTES) return send_all_(fd, data, length);
  if (out_len_ + length > OUT_BYTES && !flush_(fd)) return false;
  memcpy(out_ + out_len_, data, length);
  out_len_ += length;
  return true;
}

bool ScreenVideo::flush_(int fd) {
  const size_t length = out_len_;
  out_len_ = 0;
  return length == 0 || send_all_(fd, out_, length);
}

bool ScreenVideo::send_all_(int fd, const uint8_t *data, size_t length) {
  while (length > 0) {
    const int n = send(fd, data, length, 0);
    if (n <= 0) return false;
    data += n;
    length -= n;
  }
  return true;
}

// Whether a viewer's connection has room for a picture: lwIP says a socket is writable while more than half of its send
// buffer is free (TCP_SNDLOWAT), so this is true while less than about 32 KB waits unsent on it, a third of a second of
// video. Beyond that a picture would wait in send() for the Wi-Fi, and the sound with it: on a reTerminal D1001 whose
// uplink stalled behind four live tiles the sound reached the app in bursts a second late (2.4 s at most) and the
// viewing screen cut most of it away (2026-10-29). A picture that finds no room is left behind instead; the sound goes.
bool ScreenVideo::writable_(int fd) {
  fd_set writable;
  FD_ZERO(&writable);
  FD_SET(fd, &writable);
  timeval now{0, 0};
  return ::select(fd + 1, nullptr, &writable, nullptr, &now) > 0 && FD_ISSET(fd, &writable);
}

// Whether an access unit holds a key frame (a NAL unit of type 5): what a viewer left behind starts again from.
static bool has_idr(const uint8_t *d, size_t len) {
  for (size_t i = 0; i + 3 < len; ++i)
    if (d[i] == 0 && d[i + 1] == 0 && d[i + 2] == 1) {
      if ((d[i + 3] & 0x1F) == 5) return true;
      i += 2;
    }
  return false;
}

// The value of a header line, found without regard to case.
static std::string header(const std::string &request, const char *name) {
  const std::string key = std::string("\r\n") + name + ":";
  std::string lower = request, lkey = key;
  for (auto &ch : lower) ch = tolower(ch);
  for (auto &ch : lkey) ch = tolower(ch);
  size_t at = lower.find(lkey);
  if (at == std::string::npos) return "";
  at += key.size();
  while (at < request.size() && request[at] == ' ') ++at;
  const size_t end = request.find("\r\n", at);
  return request.substr(at, end == std::string::npos ? std::string::npos : end - at);
}

void ScreenVideo::serve_() {
  const int listener = socket(AF_INET, SOCK_STREAM, 0);
  sockaddr_in at{};
  at.sin_family = AF_INET;
  at.sin_port = htons(port_);
  at.sin_addr.s_addr = htonl(INADDR_ANY);
  int on = 1;
  setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
  if (listener < 0 || bind(listener, reinterpret_cast<sockaddr *>(&at), sizeof(at)) != 0 || listen(listener, 2) != 0) {
    ESP_LOGE(TAG, "Cannot listen on port %u", port_);
    return;
  }
  // One user, "tessera", with Basic authentication (RTSP's own, as go2rtc and ffmpeg send it). The password is worked
  // out from the screen's API key when it is built (__init__.py), and so by Home Assistant's tessera_video.
  unsigned char encoded[128];
  size_t encoded_len = 0;
  const std::string user_pass = std::string("tessera:") + password_;
  mbedtls_base64_encode(encoded, sizeof(encoded), &encoded_len, reinterpret_cast<const unsigned char *>(user_pass.data()),
                        user_pass.size());
  expected_auth_ = "Basic " + std::string(reinterpret_cast<char *>(encoded), encoded_len);
  while (true) {
    // Whoever has something to say, within the sender's pace (sound every 20 ms).
    fd_set readable;
    FD_ZERO(&readable);
    FD_SET(listener, &readable);
    int highest = listener;
    for (auto &v : viewers_) {
      if (v.fd < 0) continue;
      FD_SET(v.fd, &readable);
      highest = std::max(highest, v.fd);
    }
    timeval wait{0, 10000};
    if (::select(highest + 1, &readable, nullptr, nullptr, &wait) > 0) {  // POSIX, not esphome::select
      if (FD_ISSET(listener, &readable)) accept_(listener);
      for (auto &v : viewers_)
        if (v.fd >= 0 && FD_ISSET(v.fd, &readable)) read_(v);
    }
    const uint32_t now = millis();
    for (auto &v : viewers_)
      if (v.fd >= 0 && !v.playing && now - v.silent_since > 30000) drop_(v);
    if (!anyone_playing_()) continue;
    // Every access unit the encoder finished, to every viewer that plays; the sound first (a picture late is a
    // picture dropped, a sound late is a gap).
    // Room in internal RAM first (INTERNAL_LOW): heap_caps_get_free_size reads a counter, it walks nothing.
    const size_t internal = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    if (!holding_ && internal < INTERNAL_LOW) {
      holding_ = true;
      ESP_LOGW(TAG, "Internal RAM low (%u bytes free): the video skips frames until there is room", (unsigned) internal);
    } else if (holding_ && internal >= INTERNAL_AGAIN) {
      holding_ = false;
      want_idr_ = true;  // the frames in between are gone: a key frame to start from
      ESP_LOGI(TAG, "Room again in internal RAM: the video goes on");
    }
    int index;
    while (xQueueReceive(ready_, &index, 0) == pdTRUE) {
#ifdef USE_SCREEN_VIDEO_SOUND
      if (wants_sound_.load()) send_sound_();
#endif
      Slot &slot = slots_[index];
      slot.state = 2;
      if (holding_) {
        held_++;
      } else {
        // A viewer whose connection is full gets no picture until it has room again, and then a key frame first: the
        // pictures in between are gone for it. Its sound goes on in between (send_sound_: small packets, sent as the
        // pictures are, every dozen packets of one as well).
        const bool idr = has_idr(slot.data, slot.length);
        for (auto &v : viewers_) {
          if (v.fd < 0 || !v.playing) continue;
          if (!writable_(v.fd)) {
            if (!v.left_behind) want_idr_ = true;
            v.left_behind = true;
            behind_++;
            continue;
          }
          if (v.left_behind) {
            if (!idr) {
              behind_++;
              continue;
            }
            v.left_behind = false;
          }
          if (!send_access_unit_(v, slot)) drop_(v);
        }
      }
      slot.state = 0;
    }
#ifdef USE_SCREEN_VIDEO_SOUND
    if (wants_sound_.load()) send_sound_();
#endif
    // Sender reports every second on both tracks: what a viewer lines picture and sound up by.
    for (auto &v : viewers_) {
      if (v.fd < 0 || !v.playing || now - v.reports_at < 1000) continue;
      v.reports_at = now;
      if (!send_reports_(v)) drop_(v);
    }
  }
}

bool ScreenVideo::anyone_playing_() const {
  for (const auto &v : viewers_)
    if (v.fd >= 0 && v.playing) return true;
  return false;
}

void ScreenVideo::accept_(int listener) {
  const int fd = accept(listener, nullptr, nullptr);
  if (fd < 0) return;
  Viewer *free = nullptr;
  for (auto &v : viewers_)
    if (v.fd < 0 && free == nullptr) free = &v;
  if (free == nullptr) {
    ESP_LOGW(TAG, "A viewer was refused: %d watch already", MAX_VIEWERS);
    close(fd);
    return;
  }
  timeval send_wait{2, 0};
  setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &send_wait, sizeof(send_wait));
  int nodelay = 1;
  setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof(nodelay));
  *free = Viewer{};
  free->fd = fd;
  free->silent_since = millis();
}

void ScreenVideo::drop_(Viewer &v) {
  if (v.fd < 0) return;
  close(v.fd);
  v.fd = -1;
  v.playing = false;
  if (!anyone_playing_()) {
    playing_ = false;
    int index;
    while (xQueueReceive(ready_, &index, 0) == pdTRUE) slots_[index].state = 0;
#ifdef USE_SCREEN_VIDEO_SOUND
    wants_sound_ = false;
#endif
    ESP_LOGI(TAG, "Nobody watches any more");
  }
}

void ScreenVideo::read_(Viewer &v) {
  char chunk[1024];
  const int n = recv(v.fd, chunk, sizeof(chunk), 0);
  if (n == 0 || (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK)) {
    drop_(v);
    return;
  }
  if (n <= 0) return;
  v.pending.append(chunk, n);
  v.silent_since = millis();
  // What the viewer sent: RTCP over the connection ('$' channel length data), skipped, or RTSP requests.
  while (v.fd >= 0 && !v.pending.empty()) {
    if (v.pending[0] == '$') {
      if (v.pending.size() < 4) break;
      const size_t len = (static_cast<uint8_t>(v.pending[2]) << 8) | static_cast<uint8_t>(v.pending[3]);
      if (v.pending.size() < 4 + len) break;
      v.pending.erase(0, 4 + len);
      continue;
    }
    const size_t end = v.pending.find("\r\n\r\n");
    if (end == std::string::npos) {
      if (v.pending.size() > 4096) drop_(v);  // no request is this long
      break;
    }
    const std::string request = "\r\n" + v.pending.substr(0, end + 2);
    v.pending.erase(0, end + 4);
    if (!request_(v, request)) drop_(v);
  }
}

bool ScreenVideo::request_(Viewer &v, const std::string &request) {
  sockaddr_in self{};
  socklen_t self_len = sizeof(self);
  getsockname(v.fd, reinterpret_cast<sockaddr *>(&self), &self_len);
  char ip[16];
  inet_ntoa_r(self.sin_addr, ip, sizeof(ip));
  const std::string method = request.substr(2, request.find(' ', 2) - 2);
  const std::string cseq = header(request, "CSeq");
  std::string reply = "RTSP/1.0 200 OK\r\nCSeq: " + cseq + "\r\n";
  std::string body;
  const bool allowed = header(request, "Authorization") == expected_auth_;
  bool teardown = false;
  if (method != "OPTIONS" && !allowed) {
    reply = "RTSP/1.0 401 Unauthorized\r\nCSeq: " + cseq + "\r\nWWW-Authenticate: Basic realm=\"Tessera\"\r\n";
  } else if (method == "OPTIONS") {
    reply += "Public: OPTIONS, DESCRIBE, SETUP, PLAY, TEARDOWN, GET_PARAMETER\r\n";
  } else if (method == "DESCRIBE") {
    body = std::string("v=0\r\no=- 0 0 IN IP4 ") + ip + "\r\ns=Tessera camera\r\nc=IN IP4 0.0.0.0\r\nt=0 0\r\n" +
           "a=control:*\r\nm=video 0 RTP/AVP 96\r\na=rtpmap:96 H264/90000\r\n" +
           "a=fmtp:96 packetization-mode=1;profile-level-id=42e01f\r\na=control:track1\r\n";
#ifdef USE_SCREEN_VIDEO_SOUND
    // Opus in RTP counts at 48 kHz and says two channels whatever it carries (RFC 7587).
    if (opus_ != nullptr)
      body += "m=audio 0 RTP/AVP 111\r\na=rtpmap:111 opus/48000/2\r\n"
              "a=fmtp:111 minptime=10;useinbandfec=1;sprop-stereo=0\r\na=control:track2\r\n";
#endif
    std::string base = request.substr(method.size() + 3, request.find(' ', method.size() + 3) - method.size() - 3);
    if (base.empty() || base.back() != '/') base += '/';
    reply += "Content-Base: " + base + "\r\nContent-Type: application/sdp\r\n";
  } else if (method == "SETUP") {
    const std::string transport = header(request, "Transport");
    if (transport.find("interleaved") == std::string::npos) {
      reply = "RTSP/1.0 461 Unsupported Transport\r\nCSeq: " + cseq + "\r\n";
    } else {
      // The channels the viewer asked for (interleaved=a-b): the picture's on track1, the sound's on track2.
      const size_t at = transport.find("interleaved=");
      const bool sound = request.substr(0, request.find("RTSP/1.0")).find("track2") != std::string::npos;
      const int channel = at == std::string::npos ? (sound ? 2 : 0) : std::clamp(atoi(transport.c_str() + at + 12), 0, 254);
      if (sound) {
        v.sound_channel = channel;
      } else {
        v.video_channel = channel;
      }
      reply += "Transport: RTP/AVP/TCP;unicast;interleaved=" + std::to_string(channel) + "-" +
               std::to_string(channel + 1) + "\r\nSession: 54455353;timeout=60\r\n";
    }
  } else if (method == "PLAY") {
    reply += "Session: 54455353\r\nRange: npt=0.000-\r\n";
  } else if (method == "TEARDOWN") {
    reply += "Session: 54455353\r\n";
    teardown = true;
  } else if (method != "GET_PARAMETER" && method != "SET_PARAMETER") {
    reply = "RTSP/1.0 405 Method Not Allowed\r\nCSeq: " + cseq + "\r\n";
  }
  if (!body.empty()) reply += "Content-Length: " + std::to_string(body.size()) + "\r\n";
  reply += "\r\n" + body;
  if (!send_all_(v.fd, reinterpret_cast<const uint8_t *>(reply.data()), reply.size())) return false;
  if (teardown) return false;
  if (method == "PLAY" && allowed) {
    const bool first = !anyone_playing_();
    v.playing = true;
    v.reports_at = 0;
    if (first) {
      int stale;
      while (xQueueReceive(ready_, &stale, 0) == pdTRUE) {
      }
      for (auto &slot : slots_) slot.state = 0;
      last_frame_us_ = 0;
      playing_ = true;
    }
    want_idr_ = true;  // a key frame for the newcomer, soon
#ifdef USE_SCREEN_VIDEO_SOUND
    if (opus_ != nullptr && v.sound_channel >= 0 && !wants_sound_.load()) {
      vTaskDelay(pdMS_TO_TICKS(5));  // a write of the microphone's task that was under way finishes
      xStreamBufferReset(sound_);
      sound_skipped_ = 0;
      esp_opus_enc_reset(opus_);
      sound_start_us_ = 0;
      sound_sent_ = 0;
      sound_octets_ = 0;
      sound_report_packets_ = 0;
      mic_samples_at_play_ = mic_samples_.load();
      wants_sound_ = true;
    }
#endif
    ESP_LOGI(TAG, "A viewer watches%s", first ? "" : " (one more)");
  }
  return true;
}

bool ScreenVideo::send_access_unit_(Viewer &v, const Slot &slot) {
  const int fd = v.fd;
  // The access unit's NAL units, between Annex-B start codes (00 00 01 or 00 00 00 01).
  const uint8_t *d = slot.data;
  const size_t len = slot.length;
  size_t starts[64], ends[64];
  int count = 0;
  for (size_t i = 0; i + 3 <= len && count < 64; ++i) {
    if (d[i] == 0 && d[i + 1] == 0 && d[i + 2] == 1) {
      if (count > 0) ends[count - 1] = (i > 0 && d[i - 1] == 0) ? i - 1 : i;
      starts[count++] = i + 3;
      i += 2;
    }
  }
  if (count == 0) return true;
  ends[count - 1] = len;
  uint8_t packet[4 + 12 + 2 + RTP_PAYLOAD];
  int since_sound = 0;
  auto rtp = [&](const uint8_t *payload_head, size_t head_len, const uint8_t *payload, size_t payload_len,
                 bool marker) -> bool {
    const size_t rtp_len = 12 + head_len + payload_len;
    packet[0] = '$';
    packet[1] = static_cast<uint8_t>(v.video_channel);
    packet[2] = rtp_len >> 8;
    packet[3] = rtp_len & 0xFF;
    uint8_t *h = packet + 4;
    h[0] = 0x80;
    h[1] = (marker ? 0x80 : 0) | 96;
    h[2] = v.seq >> 8;
    h[3] = v.seq & 0xFF;
    ++v.seq;
    h[4] = slot.timestamp >> 24;
    h[5] = slot.timestamp >> 16;
    h[6] = slot.timestamp >> 8;
    h[7] = slot.timestamp;
    h[8] = ssrc_ >> 24;
    h[9] = ssrc_ >> 16;
    h[10] = ssrc_ >> 8;
    h[11] = ssrc_;
    if (head_len) memcpy(h + 12, payload_head, head_len);
    memcpy(h + 12 + head_len, payload, payload_len);
    v.video_packets++;
    v.video_octets += head_len + payload_len;
    if (!queue_(fd, packet, 4 + rtp_len)) return false;
#ifdef USE_SCREEN_VIDEO_SOUND
    // The sound between the packets of a long picture (a key frame is 50 to 200 KB, a second of video or more on a
    // slow uplink), so it never waits for a whole picture to pass.
    if (++since_sound >= 12 && wants_sound_.load()) {
      since_sound = 0;
      if (!flush_(fd)) return false;
      send_sound_();
      if (v.fd < 0) return false;  // its own sound could not be sent: the viewer is gone
    }
#endif
    return true;
  };
  for (int k = 0; k < count; ++k) {
    const uint8_t *nal = d + starts[k];
    const size_t nal_len = ends[k] - starts[k];
    if (nal_len == 0) continue;
    const bool last = k == count - 1;
    if (nal_len <= RTP_PAYLOAD) {
      if (!rtp(nullptr, 0, nal, nal_len, last)) return false;
      continue;
    }
    // FU-A: the NAL unit's header in two bytes, then its body in pieces.
    const uint8_t indicator = (nal[0] & 0xE0) | 28;
    const uint8_t type = nal[0] & 0x1F;
    size_t at = 1;
    while (at < nal_len) {
      const size_t piece = std::min(RTP_PAYLOAD - 2, nal_len - at);
      const bool first = at == 1, end = at + piece == nal_len;
      const uint8_t head[2] = {indicator, static_cast<uint8_t>((first ? 0x80 : 0) | (end ? 0x40 : 0) | type)};
      if (!rtp(head, 2, nal + at, piece, last && end)) return false;
      at += piece;
    }
  }
  return flush_(fd);
}

// An RTCP sender report (RFC 3550 6.4.1) over the connection, on the channel after the track's own.
static void sender_report(uint8_t *at, uint8_t channel, uint32_t ssrc, uint64_t now_us, uint32_t rtp, uint32_t packets,
                          uint32_t octets) {
  at[0] = '$';
  at[1] = channel;
  at[2] = 0;
  at[3] = 28;
  uint8_t *r = at + 4;
  r[0] = 0x80;
  r[1] = 200;  // SR
  r[2] = 0;
  r[3] = 6;  // 28 bytes, in 32-bit words less one
  const uint32_t ntp_s = static_cast<uint32_t>(now_us / 1000000) + 2208988800UL;
  const uint32_t ntp_f = static_cast<uint32_t>(((now_us % 1000000) << 32) / 1000000);
  const uint32_t words[6] = {ssrc, ntp_s, ntp_f, rtp, packets, octets};
  for (int i = 0; i < 6; ++i) {
    r[4 + i * 4] = words[i] >> 24;
    r[5 + i * 4] = words[i] >> 16;
    r[6 + i * 4] = words[i] >> 8;
    r[7 + i * 4] = words[i];
  }
}

bool ScreenVideo::send_reports_(Viewer &v) {
  // Both clocks against one wall clock, esp_timer: the camera stamps its frames at 90 kHz from it, the sound's start
  // was taken from it.
  const int fd = v.fd;
  const int64_t now = esp_timer_get_time();
  uint8_t buf[2 * 32];
  size_t len = 0;
  if (v.video_packets > 0) {
    sender_report(buf + len, v.video_channel + 1, ssrc_, now, static_cast<uint32_t>(static_cast<uint64_t>(now) * 9 / 100),
                  v.video_packets, v.video_octets);
    len += 32;
  }
#ifdef USE_SCREEN_VIDEO_SOUND
  if (v.sound_channel >= 0 && sound_start_us_ != 0) {
    const uint32_t rtp = sound_start_ts_ + static_cast<uint32_t>((now - sound_start_us_) * 48 / 1000);
    sender_report(buf + len, v.sound_channel + 1, sound_ssrc_, now, rtp, sound_report_packets_, sound_octets_);
    len += 32;
  }
#endif
  if (len == 0) return true;
  if (!queue_(fd, buf, len)) return false;
  return flush_(fd);
}


}  // namespace esphome::screen_video
