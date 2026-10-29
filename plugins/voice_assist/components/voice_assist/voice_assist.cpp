#include "voice_assist.h"

#include "esphome/components/api/api_server.h"
#include "esphome/core/hal.h"

namespace esphome::voice_assist {

using tessera::Font;
namespace ui = tessera::ui;

// Icons of Tessera's set: the assistant, listening, thinking, answering, and the assistant that could not help.
static constexpr uint32_t ICON_ASSISTANT = 0xF06A9;   // robot
static constexpr uint32_t ICON_LISTENING = 0xF036C;   // microphone
static constexpr uint32_t ICON_THINKING = 0xF01D8;    // dots-horizontal
static constexpr uint32_t ICON_ANSWERING = 0xF057E;   // volume-high
static constexpr uint32_t ICON_FAILED = 0xF169F;      // robot-confused
// How long a reply or a failure stays on the tile after it was spoken.
static constexpr uint32_t REPLY_SHOWN_MS = 20000;
// How long the popup stays after the answer was spoken or a failure, to read it again (its close key or a tap closes it
// sooner), and after a question that came to nothing (nothing heard, "never mind").
static constexpr uint32_t POPUP_AFTER_MS = 30000, POPUP_EMPTY_MS = 2000;
// The pace of the words in the popup: about that of Home Assistant's voices (Piper, Home Assistant Cloud), in
// characters a second. The whole answer shows once the speaker is done.
static constexpr uint32_t SPOKEN_CHARS_PER_S = 15;
// How long after the answer went to the speaker its words start anyway, when the media player never says it plays.
static constexpr uint32_t HEARD_AFTER_MS = 1500;
static bool is_on(switch_::Switch *s) { return s == nullptr || s->state; }

// The tile: the assistant's icon, what it does now, and under it what it heard or answered. A tap asks, or stops.
class AssistTile : public tessera::Tile {
 public:
  explicit AssistTile(VoiceAssist *plugin) : plugin_(plugin) {}

  void create(const tessera::TileContext &c) override {
    width_ = c.width;
    height_ = c.height;
    wide_ = c.columns >= 2 && c.width > c.height * 3 / 2;
    icon_ = ui::label(c.parent, Font::ICON, theme::MUTED);
    lv_obj_set_style_text_align(icon_, LV_TEXT_ALIGN_CENTER, 0);
    status_ = ui::label(c.parent, Font::TITLE, theme::INK);
    detail_ = ui::label(c.parent, Font::BODY, theme::INK_SOFT);
    lv_label_set_long_mode(detail_, LV_LABEL_LONG_DOT);
    if (!wide_) {
      lv_obj_set_style_text_align(status_, LV_TEXT_ALIGN_CENTER, 0);
      lv_obj_set_style_text_align(detail_, LV_TEXT_ALIGN_CENTER, 0);
    }
  }

  void on_tick(uint32_t) override {
    const Phase phase = plugin_->current();
    const bool busy = phase == Phase::LISTENING || phase == Phase::THINKING || phase == Phase::ANSWERING;
    const bool failed = plugin_->showing_failure();
    ui::set_text(icon_, ui::icon(plugin_->phase_icon()));
    ui::set_tone(icon_, plugin_->phase_tone(), theme::MUTED);
    std::string status = busy || failed ? plugin_->phase_word() : plugin_->idle_line();
    // Under it: what was heard while it thinks, the reply while and after it answers.
    std::string detail;
    if (phase == Phase::THINKING) detail = plugin_->question();
    else if (phase == Phase::ANSWERING || plugin_->reply_fresh()) detail = plugin_->reply();
    layout(status, detail);
  }

  void on_theme() override {
    ui::set_color(status_, theme::INK);
    ui::set_color(detail_, theme::INK_SOFT);
    on_tick(0);
  }

  void on_tap() override { plugin_->ask(); }

 private:
  // The icon, the status line and as many lines of the detail as fit: beside each other on a wide card, under each
  // other on any other. The status takes the largest of the screen's fonts that fits.
  void layout(const std::string &status, const std::string &detail) {
    const int icon_h = ui::line_height(Font::ICON);
    const int text_w = wide_ ? width_ - icon_h - ui::px(12) : width_;
    Font face = Font::BODY;
    for (Font f : {Font::HEADLINE, Font::TITLE, Font::BODY_LARGE, Font::BODY}) {
      face = f;
      if (ui::text_width(status, f) <= text_w) break;
    }
    ui::set_font(status_, face);
    ui::set_text(status_, status);
    ui::set_text(detail_, detail);
    const int status_h = ui::line_height(face), line_h = ui::line_height(Font::BODY);
    if (wide_) {
      lv_obj_set_pos(icon_, 0, (height_ - icon_h) / 2);
      lv_obj_set_size(icon_, icon_h, icon_h);
      const int lines = detail.empty() ? 0 : std::max(1, std::min(3, (height_ - status_h) / line_h));
      const int top = (height_ - status_h - lines * line_h) / 2;
      lv_obj_set_pos(status_, icon_h + ui::px(12), top);
      lv_obj_set_size(status_, text_w, status_h);
      lv_obj_set_pos(detail_, icon_h + ui::px(12), top + status_h);
      lv_obj_set_size(detail_, text_w, lines ? lines * line_h : 1);
      return;
    }
    const int room = height_ - icon_h - status_h;
    const int lines = detail.empty() || room < line_h ? 0 : std::min(3, room / line_h);
    const int top = std::max(0, (height_ - icon_h - status_h - lines * line_h) / 2);
    lv_obj_set_pos(icon_, 0, top);
    lv_obj_set_size(icon_, width_, icon_h);
    lv_obj_set_pos(status_, 0, top + icon_h);
    lv_obj_set_size(status_, width_, status_h);
    lv_obj_set_pos(detail_, 0, top + icon_h + status_h);
    lv_obj_set_size(detail_, width_, lines ? lines * line_h : 1);
  }

  VoiceAssist *plugin_;
  int width_ = 0, height_ = 0;
  bool wide_ = false;
  lv_obj_t *icon_{}, *status_{}, *detail_{};
};

void VoiceAssist::setup() {
  add_tile("assist", [this]() { return new AssistTile(this); });
  add_bar_item("busy", [this]() {
    tessera::BarItem item;
    const bool fresh_failure = showing_failure();
    if (busy() || phase_ == Phase::LISTENING || phase_ == Phase::THINKING || phase_ == Phase::ANSWERING || fresh_failure) {
      item.shown = true;
      item.icon = phase_icon();
      item.tone = phase_tone();
      item.text = phase_word();
    }
    return item;
  });
}

void VoiceAssist::on_ready() {
  // The phrase Wake phrase kept over the restart listens; the others rest.
  auto index = wake_phrase_ != nullptr ? wake_phrase_->active_index() : optional<size_t>{};
  choose_phrase(index.has_value() ? *index : 0);
}

bool VoiceAssist::settings(tessera::SettingsPage &page) {
  page.icon = "\U000F06A9";
  page.toggle(text("wake_word"), [this] { return wake_word_on_ != nullptr && wake_word_on_->state; },
              [this](bool on) { on ? wake_word_on_->turn_on() : wake_word_on_->turn_off(); });
  if (wake_phrase_ != nullptr) {
    std::vector<std::string> options;
    for (size_t i = 0; i < wake_phrase_->size(); ++i) options.emplace_back(wake_phrase_->option_at(i));
    page.choice(text("wake_phrase"), options,
                [this] { auto index = wake_phrase_->active_index(); return index.has_value() ? (int) *index : 0; },
                [this](int index) { wake_phrase_->make_call().set_index((size_t) index).perform(); });
  }
  page.toggle(text("wake_sound"), [this] { return is_on(wake_sound_on_); },
              [this](bool on) { if (wake_sound_on_ != nullptr) on ? wake_sound_on_->turn_on() : wake_sound_on_->turn_off(); });
  page.toggle(text("thinking_sound"), [this] { return is_on(thinking_sound_on_); },
              [this](bool on) {
                if (thinking_sound_on_ != nullptr) on ? thinking_sound_on_->turn_on() : thinking_sound_on_->turn_off();
              });
  page.toggle(text("show_popup"), [this] { return is_on(show_popup_on_); },
              [this](bool on) { if (show_popup_on_ != nullptr) on ? show_popup_on_->turn_on() : show_popup_on_->turn_off(); });
  page.action(text("ask"), "\U000F06A9", [this] { ask(); }, nullptr,
              [this]() -> std::string { return busy() ? phase_word() : std::string(); })
      .active([this] { return busy(); });
  return true;
}

void VoiceAssist::loop() {
  // While Assist thinks: the thinking sound again as soon as the last one went to the speaker, so its own length sets
  // the rhythm and nothing is cut off. The player hands the speaker what it takes, never waiting.
  if (phase_ == Phase::THINKING && is_on(thinking_sound_on_) && !sounds_.playing()) sounds_.play(thinking_sound_);
  sounds_.loop();
  const uint32_t now = millis();
  // The answer is heard once the media player announces it (it buffers first), or a moment after it went there.
  if (speak_asked_ && !speak_from_) {
    const bool playing = media_player_ != nullptr && media_player_->state == media_player::MEDIA_PLAYER_STATE_ANNOUNCING;
    if (playing || now - speak_asked_ >= HEARD_AFTER_MS) {
      speak_from_ = now;
      popup_update_();
    }
  }
  // Spoken: the pipeline ran out and voice_assistant has nothing playing any more (it keeps running until the
  // announcement finished, after the pipeline's own end).
  if (phase_ == Phase::ANSWERING && !busy()) {
    phase_ = Phase::IDLE;
    tessera::refresh();
    popup_update_();
  }
  if (popup_ != nullptr && !busy() && phase_ != Phase::LISTENING && !close_at_)
    close_at_ = now + (answer_.empty() && phase_ != Phase::FAILED ? POPUP_EMPTY_MS : POPUP_AFTER_MS);
  if (popup_ != nullptr && close_at_ && (int32_t) (now - close_at_) >= 0) {
    popup_ = nullptr;
    close_at_ = 0;
    tessera::close_popup(this);
  }
}

void VoiceAssist::listening_() {
  if (phase_ != Phase::LISTENING && is_on(wake_sound_on_)) sounds_.play(wake_sound_);
  phase_ = Phase::LISTENING;
  heard_.clear();
  answer_.clear();
  speak_asked_ = speak_from_ = close_at_ = 0;
  popup_show_();
}

void VoiceAssist::popup_off() {
  if (popup_ == nullptr) return;
  popup_ = nullptr;
  close_at_ = 0;
  tessera::close_popup(this);
}

void VoiceAssist::popup_show_() {
  if (popup_ != nullptr || !is_on(show_popup_on_)) return;
  tessera::show_popup(this, [this]() {
    popup_ = new VoicePopup([this]() { return view(); },
                            // A tap on it stops what runs; once it is done, it closes.
                            [this]() {
                              if (busy()) {
                                assistant_->request_stop();
                              } else {
                                popup_ = nullptr;
                                close_at_ = 0;
                                tessera::close_popup(this);
                              }
                            },
                            // It went by itself (a tap beside it, an alert, standby, an update): what runs stops too.
                            [this]() {
                              popup_ = nullptr;
                              close_at_ = 0;
                              stop();
                            });
    return popup_;
  });
}

void VoiceAssist::popup_update_() {
  if (popup_ != nullptr) popup_->show(false);
}

std::string VoiceAssist::said_() const {
  if (answer_.empty()) return {};
  // A failure, and an answer the speaker is done with, show whole.
  if (phase_ == Phase::FAILED || phase_ == Phase::IDLE) return answer_;
  if (!speak_from_) return {};
  const size_t chars = (size_t) ((millis() - speak_from_) * SPOKEN_CHARS_PER_S / 1000);
  if (chars >= answer_.size()) return answer_;
  // Whole words only, and never half a character.
  size_t end = answer_.find(' ', chars);
  if (end == std::string::npos) return answer_;
  return answer_.substr(0, end);
}

PopupView VoiceAssist::view() const {
  PopupView v;
  // Once it is done (spoken, or nothing came of it) the disc stands still with the assistant and its name, until the
  // popup closes.
  v.phase = phase_;
  v.icon = phase_ == Phase::IDLE ? ICON_ASSISTANT : phase_icon();
  v.tone = phase_ == Phase::FAILED ? tessera::Tone::ALERT : phase_ == Phase::THINKING ? tessera::Tone::BUSY
                                                                                       : tessera::Tone::ACCENT;
  v.word = phase_ == Phase::IDLE ? std::string(text("name")) : phase_word();
  v.heard = heard_;
  v.said = said_();
  return v;
}

void VoiceAssist::phase(Phase p) {
  if (p == Phase::LISTENING) listening_();
  else phase_ = p;
  tessera::refresh();
  popup_update_();
}

void VoiceAssist::heard(const std::string &text) {
  heard_ = text;
  tessera::refresh();
  popup_update_();
}

void VoiceAssist::answer(const std::string &text) {
  // The answer is about to play on the same speaker: a plop that is still going stops here.
  if (sounds_.playing(thinking_sound_)) sounds_.stop();
  answer_ = text;
  answered_at_ = millis();
  phase_ = Phase::ANSWERING;
  tessera::refresh();
  popup_update_();
}

void VoiceAssist::speaking(const std::string &url) {
  if (url.empty()) return;
  // A streamed answer starts to play before Assist has finished it (its words come with tts_start, later): the thinking
  // is over here, and so is a plop that is still going.
  if (sounds_.playing(thinking_sound_)) sounds_.stop();
  if (!speak_asked_) speak_asked_ = millis();
  if (phase_ == Phase::LISTENING || phase_ == Phase::THINKING) {
    phase_ = Phase::ANSWERING;
    answered_at_ = millis();
  }
  tessera::refresh();
  popup_update_();
}

void VoiceAssist::ended() {
  // A question that came to nothing (nothing heard, "never mind") ends here; a spoken answer ends in loop(), once the
  // speaker is done with it, and a failure stays as it is.
  if (phase_ == Phase::LISTENING || phase_ == Phase::THINKING) phase_ = Phase::IDLE;
  tessera::refresh();
  popup_update_();
}

void VoiceAssist::failed(const std::string &code, const std::string &message) {
  ESP_LOGW("voice_assist", "Assist failed: %s (%s)", code.c_str(), message.c_str());
  answer_ = message.empty() ? code : message;
  answered_at_ = millis();
  phase_ = Phase::FAILED;
  speak_asked_ = speak_from_ = 0;
  tessera::refresh();
  popup_update_();
}

void VoiceAssist::choose_phrase(size_t index) {
  for (size_t i = 0; i < phrases_.size(); ++i) {
    if (i == index) phrases_[i]->enable();
    else phrases_[i]->disable();
  }
  tessera::refresh();
}

bool VoiceAssist::claim_wake_word_() {
  if (wake_word_ == nullptr) return false;
  using tessera::audio::Role;
  using tessera::audio::Side;
  wake_holds_ = tessera::audio::claim(
      Side::IN, Role::VOICE, 0,
      [this]() {
        wake_holds_ = false;
        wake_word_->stop();
      },
      [this]() {
        wake_holds_ = true;
        if (wake_wanted_) wake_word_->start();
      });
  return wake_holds_;
}

void VoiceAssist::listen(bool on) {
  using tessera::audio::Role;
  using tessera::audio::Side;
  wake_wanted_ = on;
  if (wake_word_ == nullptr) return;
  if (on) {
    if (claim_wake_word_()) wake_word_->start();
    return;
  }
  wake_word_->stop();
  if (!busy()) tessera::audio::release(Side::IN, Role::VOICE, 0);
  wake_holds_ = false;
}

void VoiceAssist::session(bool on) {
  using tessera::audio::Role;
  using tessera::audio::Side;
  if (on) {
    // The same claim as the wake word's (VOICE, 0) on the microphone, and the speaker beside it: music pauses and stays
    // paused (media waits for nothing), and a ring that takes either side stops the session.
    const bool held = tessera::audio::claim(Side::BOTH, Role::VOICE, 0, [this]() {
      wake_holds_ = false;
      if (wake_word_ != nullptr) wake_word_->stop();
      if (busy()) assistant_->request_stop();
    });
    if (!held && busy()) assistant_->request_stop();
    return;
  }
  tessera::audio::release(Side::OUT, Role::VOICE, 0);
  // The microphone stays the wake word's (its own answers back) while it should listen; otherwise it goes.
  if (wake_wanted_) claim_wake_word_();
  else tessera::audio::release(Side::IN, Role::VOICE, 0);
}

void VoiceAssist::on_interval(uint32_t) {
  // A wake word refused the microphone (a ring had it) listens again once nobody above it holds it.
  if (wake_wanted_ && !wake_holds_ && !busy() &&
      tessera::audio::open(tessera::audio::Side::IN, tessera::audio::Role::VOICE) && claim_wake_word_())
    wake_word_->start();
}

void VoiceAssist::ask() {
  if (assistant_ == nullptr) return;
  if (busy()) {
    assistant_->request_stop();
    return;
  }
  listening_();
  assistant_->request_start(false, true);
  tessera::refresh();
  popup_update_();
}

void VoiceAssist::stop() {
  if (busy()) assistant_->request_stop();
}

bool VoiceAssist::reply_fresh() const {
  return answered_at_ != 0 && millis() - answered_at_ < REPLY_SHOWN_MS;
}

std::string VoiceAssist::idle_line() const {
  if (api::global_api_server == nullptr || !api::global_api_server->is_connected()) return text("away");
  if (wake_word_on_ != nullptr && wake_word_on_->state && wake_phrase_ != nullptr && wake_phrase_->has_state())
    return tessera::fill(text("say_phrase"), "phrase", std::string(wake_phrase_->current_option()));
  return text("tap_to_ask");
}

uint32_t VoiceAssist::phase_icon() const {
  switch (phase_) {
    case Phase::LISTENING: return ICON_LISTENING;
    case Phase::THINKING: return ICON_THINKING;
    case Phase::ANSWERING: return ICON_ANSWERING;
    case Phase::FAILED: return showing_failure() ? ICON_FAILED : ICON_ASSISTANT;
    default: return ICON_ASSISTANT;
  }
}

tessera::Tone VoiceAssist::phase_tone() const {
  switch (phase_) {
    case Phase::LISTENING: return tessera::Tone::ACCENT;
    case Phase::THINKING: return tessera::Tone::BUSY;
    case Phase::ANSWERING: return tessera::Tone::ACCENT;
    case Phase::FAILED: return showing_failure() ? tessera::Tone::ALERT : tessera::Tone::NORMAL;
    default: return tessera::Tone::NORMAL;
  }
}

std::string VoiceAssist::phase_word() const {
  // A failure that is no longer fresh is quiet again.
  if (phase_ == Phase::FAILED && !reply_fresh()) return idle_line();
  switch (phase_) {
    case Phase::LISTENING: return text("listening");
    case Phase::THINKING: return text("thinking");
    case Phase::ANSWERING: return text("answering");
    case Phase::FAILED: return text("failed");
    default: return idle_line();
  }
}

}  // namespace esphome::voice_assist
