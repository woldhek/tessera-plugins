#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "esphome/core/component.h"
#include "esphome/components/media_player/media_player.h"
#include "esphome/components/micro_wake_word/micro_wake_word.h"
#include "esphome/components/select/select.h"
#include "esphome/components/switch/switch.h"
#include "esphome/components/voice_assistant/voice_assistant.h"
#include "esphome/components/smart_display/plugin_api.h"
#include "esphome/components/smart_display/plugin_sound.h"
#include "voice_popup.h"

namespace esphome::voice_assist {

// The plugin: a tile that says what the assistant does and asks it on a tap, an item for the top bar while it is busy,
// on the screen's settings page the wake word, the phrase it listens for, its two sounds and a row that asks now, and
// the sounds themselves: one when it starts to listen, a plop again and again while Assist thinks; and the popup of a
// question (voice_popup.h), from the moment it listens until a while after it answered.
class VoiceAssist : public Component, public tessera::Plugin {
 public:
  void setup() override;
  void loop() override;
  float get_setup_priority() const override { return setup_priority::LATE; }
  void on_ready() override;
  void on_interval(uint32_t now_ms) override;
  bool settings(tessera::SettingsPage &page) override;
  void before_update() override { stop(); }

  void set_assistant(voice_assistant::VoiceAssistant *a) { assistant_ = a; }
  void set_wake_word(micro_wake_word::MicroWakeWord *w) { wake_word_ = w; }
  void set_wake_word_on(switch_::Switch *s) { wake_word_on_ = s; }
  void set_wake_phrase(select::Select *s) { wake_phrase_ = s; }
  void add_phrase(micro_wake_word::WakeWordModel *m) { phrases_.push_back(m); }
  void set_speaker(speaker::Speaker *s) { sounds_.set_speaker(s); }
  void set_media_player(media_player::MediaPlayer *m) { media_player_ = m; }
  void set_wake_sound_on(switch_::Switch *s) { wake_sound_on_ = s; }
  void set_thinking_sound_on(switch_::Switch *s) { thinking_sound_on_ = s; }
  void set_show_popup_on(switch_::Switch *s) { show_popup_on_ = s; }
  // Show popup was turned off: a popup that shows goes now.
  void popup_off();
  void set_wake_sound(tessera::Sound s) { wake_sound_ = s; }
  void set_thinking_sound(tessera::Sound s) { thinking_sound_ = s; }

  // From plugin.yaml's triggers.
  void phase(Phase p);
  void heard(const std::string &text);
  void answer(const std::string &text);
  // The answer goes to the speaker (`url` of the intent's progress when it streams, of the speech's end when it does
  // not; "" is no answer to play yet): the words in the popup follow from when it is heard.
  void speaking(const std::string &url);
  // The pipeline ended (its run end), which comes before a spoken answer has finished playing.
  void ended();
  void failed(const std::string &code, const std::string &message);
  // Wake phrase chose its option `index`: that model listens, the others rest.
  void choose_phrase(size_t index);

  // The microphone and the speaker through the screen's audio owner (plugin API 0.19, tessera::audio, the role VOICE):
  // the wake word holds the microphone while it listens, and a session holds the speaker too, so music pauses for it and
  // a doorbell's ring takes both. listen(): the wake word should listen (the setting on, Home Assistant connected) or
  // not; session(): a session starts (on_listening) or ends (on_end).
  void listen(bool on);
  void session(bool on);

  // A tap on the tile, or Ask on the settings page: ask now, or stop what runs.
  void ask();
  void stop();
  bool busy() const { return assistant_ != nullptr && assistant_->is_running(); }

  // What the tile and the top bar show.
  Phase current() const { return phase_; }
  const std::string &question() const { return heard_; }
  const std::string &reply() const { return answer_; }
  std::string idle_line() const;
  std::string phase_word() const;
  // The icon and its colour for where it is: a microphone that lights up while it listens, dots in amber while Assist
  // thinks, a speaker while it answers, the confused robot in red after a failure, the robot when it rests.
  uint32_t phase_icon() const;
  tessera::Tone phase_tone() const;
  // A reply stays on the tile a while after it was spoken, and so does a failure.
  bool reply_fresh() const;
  bool showing_failure() const { return phase_ == Phase::FAILED && reply_fresh(); }
  // What the popup shows now.
  PopupView view() const;

 protected:
  voice_assistant::VoiceAssistant *assistant_{};
  micro_wake_word::MicroWakeWord *wake_word_{};
  switch_::Switch *wake_word_on_{};
  select::Select *wake_phrase_{};
  std::vector<micro_wake_word::WakeWordModel *> phrases_;
  switch_::Switch *wake_sound_on_{}, *thinking_sound_on_{}, *show_popup_on_{};
  tessera::Sound wake_sound_, thinking_sound_;
  tessera::SoundPlayer sounds_;

  // Starts to listen: the wake sound, once, when it was not listening yet; and the popup.
  void listening_();
  // The wake word's claim on the microphone: made, or made again after a session, with the wake word's own answers to
  // losing it and getting it back. False when a higher role holds it; on_interval tries again, a refused claim is never
  // queued.
  bool claim_wake_word_();
  bool wake_wanted_{false};  // the wake word should listen
  bool wake_holds_{false};   // and holds the microphone
  // The popup: shown while it listens, thinks and answers, and closed a while after (close_at_).
  void popup_show_();
  void popup_update_();
  // The answer as far as the speaker has said it, at about the pace of speech, whole words.
  std::string said_() const;
  media_player::MediaPlayer *media_player_{};
  VoicePopup *popup_{};          // the popup that shows, or none: the core deletes it, on_dismiss tells us first
  uint32_t speak_asked_{0};      // millis() when the answer went to the speaker, 0 for none
  uint32_t speak_from_{0};       // millis() when it began to be heard, 0 before
  uint32_t close_at_{0};         // millis() to close the popup at, 0 while it is busy

  Phase phase_{Phase::IDLE};
  std::string heard_, answer_;
  uint32_t answered_at_{0};     // millis() of the last reply, or of the failure
};

}  // namespace esphome::voice_assist
