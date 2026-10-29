#pragma once
#include <algorithm>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

#include "esphome/components/smart_display/plugin_api.h"

namespace esphome::voice_assist {

// Where a question is: Home Assistant's Assist pipeline tells it through voice_assistant's triggers (plugin.yaml).
enum class Phase : uint8_t { IDLE, LISTENING, THINKING, ANSWERING, FAILED };

// What the popup shows now, as the plugin knows it (VoiceAssist::view): its own drawing has no other source.
struct PopupView {
  Phase phase = Phase::IDLE;
  std::string word;      // listening, thinking, answering, or what went wrong
  uint32_t icon = 0;     // the icon in the disc
  tessera::Tone tone = tessera::Tone::ACCENT;
  std::string heard;     // what Assist understood
  std::string said;      // the part of the answer said so far: it grows while the speaker speaks
};

// The popup of a question (plugin API 0.18): a disc in the colour of where it is with its icon, on one line with the
// popup's close key and as big, the word for it beside,
// and under them what was heard and the answer as far as it is spoken. While the screen listens, rings ripple out of
// the disc; while Assist thinks, an arc turns around it and three dots pulse in it; while it answers, a ring breathes
// slowly. The card is as high as what it says: the disc and its word alone while it listens, the words under them as
// they come, and when the answer is longer than the card may grow, its last lines, as a chat shows them.
class VoicePopup : public tessera::Popup {
 public:
  VoicePopup(std::function<PopupView()> read, std::function<void()> tapped, std::function<void()> dismissed)
      : read_(std::move(read)), tapped_(std::move(tapped)), dismissed_(std::move(dismissed)) {}

  void open(const tessera::PopupContext &c) override {
    namespace ui = tessera::ui;
    using tessera::Font;
    width_ = c.width;
    tallest_ = c.height;
    const bool large = ui::large();
    // The disc is the popup's close key's size, in the room's top left corner: the two stand on one line, as far from the
    // card's top and sides. What moves around it (the ripples, the ring, the arc) reaches out over it, into the card's
    // edge, and takes no room.
    disc_ = c.key > 0 ? c.key : ui::px(large ? 60 : 40);
    head_ = disc_;
    const int at = 0;
    for (auto &ripple : ripples_) {
      ripple = circle(c.parent, disc_, at);
      lv_obj_set_style_bg_opa(ripple, LV_OPA_TRANSP, 0);
      lv_obj_set_style_border_width(ripple, ui::px(large ? 3 : 2), 0);
    }
    // The ring that breathes while it answers, and the arc that turns while it thinks: a little outside the disc.
    const int ring = disc_ + 2 * ui::px(large ? 7 : 5);
    ring_ = circle(c.parent, ring, (disc_ - ring) / 2);
    lv_obj_set_style_bg_opa(ring_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(ring_, ui::px(large ? 3 : 2), 0);
    arc_ = circle(c.parent, ring, (disc_ - ring) / 2);
    lv_obj_set_style_bg_opa(arc_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(arc_, ui::px(large ? 4 : 3), 0);
    lv_obj_set_style_border_side(arc_, LV_BORDER_SIDE_TOP, 0);
    fill_ = circle(c.parent, disc_, at);
    icon_ = ui::label(fill_, Font::ICON_SMALL, theme::ON_ACCENT);
    lv_obj_set_width(icon_, disc_);
    lv_obj_set_style_text_align(icon_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(icon_);
    const int dot = std::max(4, disc_ / 8);
    for (int i = 0; i < 3; ++i) {
      dots_[i] = ui::block(fill_, theme::ON_ACCENT);
      lv_obj_set_size(dots_[i], dot, dot);
      lv_obj_set_style_radius(dots_[i], LV_RADIUS_CIRCLE, 0);
      lv_obj_align(dots_[i], LV_ALIGN_CENTER, (i - 1) * dot * 2, 0);
    }
    word_ = ui::label(c.parent, Font::HEADLINE, theme::INK);
    const int word_x = disc_ + ui::px(large ? 16 : 10);
    lv_obj_set_pos(word_, word_x, (head_ - ui::line_height(Font::HEADLINE)) / 2);
    word_w_ = std::max(1, width_ - word_x - c.corner);   // short of the popup's close key
    lv_obj_set_width(word_, word_w_);
    // Under the disc: what was heard, then the answer in a box that shows its last whole lines.
    text_top_ = disc_ + ui::px(large ? 14 : 8);
    heard_ = ui::text(c.parent, Font::BODY, theme::INK_SOFT);
    lv_obj_set_width(heard_, width_);
    lv_obj_set_pos(heard_, 0, text_top_);
    box_ = lv_obj_create(c.parent);
    lv_obj_remove_style_all(box_);
    lv_obj_remove_flag(box_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(box_, LV_OBJ_FLAG_SCROLLABLE);
    said_ = ui::label(box_, Font::BODY_LARGE, theme::INK);
    lv_label_set_long_mode(said_, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(said_, width_);
    lv_obj_set_height(said_, LV_SIZE_CONTENT);
    show(true);
  }

  void on_interval(uint32_t) override { show(false); }
  void on_theme() override {
    tessera::ui::set_color(word_, theme::INK);
    tessera::ui::set_color(heard_, theme::INK_SOFT);
    tessera::ui::set_color(said_, theme::INK);
    show(true);
  }
  void on_tap() override { if (tapped_) tapped_(); }
  void on_dismiss() override { if (dismissed_) dismissed_(); }

  // Draws what the plugin says now, and only what changed: a new word of the answer is a new text of one label.
  void show(bool force) {
    namespace ui = tessera::ui;
    using tessera::Font;
    const PopupView v = read_();
    if (force || v.phase != phase_ || v.tone != tone_) {
      const bool moved = force || v.phase != phase_;
      phase_ = v.phase;
      tone_ = v.tone;
      const lv_color_t colour = ui::tone_color(v.tone, theme::ACCENT);
      ui::set_fill(fill_, v.tone, theme::ACCENT);
      for (lv_obj_t *part : {ripples_[0], ripples_[1], ring_, arc_}) lv_obj_set_style_border_color(part, colour, 0);
      if (moved) motion();
    }
    if (force || v.icon != icon_code_) {
      icon_code_ = v.icon;
      ui::set_text(icon_, ui::icon(v.icon));
    }
    // The word in the largest of the screen's fonts that fits beside the disc (a narrow glass standing up).
    if (force || std::string_view(word_text_) != v.word) {
      word_text_.assign(v.word.data(), v.word.size());
      Font face = Font::BODY;
      for (Font f : {Font::HEADLINE, Font::TITLE, Font::BODY_LARGE, Font::BODY}) {
        face = f;
        if (ui::text_width(v.word, f) <= word_w_) break;
      }
      ui::set_font(word_, face);
      lv_obj_set_y(word_, (head_ - ui::line_height(face)) / 2);
      ui::set_text(word_, v.word);
    }
    // What was heard: up to two lines while there is no answer yet, one above the answer; as many as its words take.
    const bool answering = !v.said.empty();
    const int line = ui::line_height(Font::BODY), big = ui::line_height(Font::BODY_LARGE);
    const int heard_lines = std::min(answering ? 1 : 2, lines(v.heard, Font::BODY));
    ui::set_text(heard_, v.heard);
    lv_obj_set_height(heard_, std::max(1, heard_lines * line));
    hide(heard_, heard_lines == 0);
    const int box_top = text_top_ + heard_lines * line + (heard_lines ? ui::px(6) : 0);
    // The answer as high as its lines, up to the whole lines the card may grow to: the card follows.
    const int most = std::max(0, tallest_ - box_top) / big;
    const int said_lines = std::min(most, lines(v.said, Font::BODY_LARGE));
    hide(box_, said_lines == 0);
    lv_obj_set_pos(box_, 0, box_top);
    lv_obj_set_size(box_, width_, std::max(1, said_lines * big));
    if (force || std::string_view(said_text_) != v.said) {
      said_text_.assign(v.said.data(), v.said.size());
      ui::set_text(said_, v.said);
    }
    // Its last lines when it is longer: it moves up a line at a time as it grows.
    const int over = lines(v.said, Font::BODY_LARGE) - said_lines;
    lv_obj_set_y(said_, over > 0 ? -over * big : 0);
  }

 protected:
  lv_obj_t *circle(lv_obj_t *parent, int size, int at) {
    lv_obj_t *c = tessera::ui::block(parent, theme::ACCENT);
    lv_obj_set_size(c, size, size);
    lv_obj_set_pos(c, at, at);
    lv_obj_set_style_radius(c, LV_RADIUS_CIRCLE, 0);
    return c;
  }
  static void hide(lv_obj_t *part, bool hidden) {
    if (hidden) lv_obj_add_flag(part, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_remove_flag(part, LV_OBJ_FLAG_HIDDEN);
  }
  // How many lines a text takes at the room's width.
  int lines(const std::string &text, tessera::Font font) const {
    if (text.empty()) return 0;
    const lv_font_t *face = tessera::ui::font(font);
    if (!face) return 1;
    lv_point_t size;
    lv_text_get_size(&size, text.c_str(), face, 0, 0, width_, LV_TEXT_FLAG_NONE);
    return std::max(1, (int) (size.y / std::max(1, (int) lv_font_get_line_height(face))));
  }
  // The disc's motion for where it is: ripples while it listens, the arc and the dots while it thinks, the ring breathing
  // while it answers; after a failure it stands still.
  void motion() {
    namespace ui = tessera::ui;
    using tessera::Motion;
    const bool listening = phase_ == Phase::LISTENING, thinking = phase_ == Phase::THINKING,
               answering = phase_ == Phase::ANSWERING;
    for (int i = 0; i < 2; ++i) {
      hide(ripples_[i], !listening);
      if (listening) ui::animate(ripples_[i], Motion::RIPPLE, 1600, i * 800);
      else ui::still(ripples_[i]);
    }
    hide(arc_, !thinking);
    if (thinking) ui::animate(arc_, Motion::SPIN, 900);
    else ui::still(arc_);
    for (int i = 0; i < 3; ++i) {
      hide(dots_[i], !thinking);
      if (thinking) ui::animate(dots_[i], Motion::PULSE, 1200, i * 200);
      else ui::still(dots_[i]);
    }
    hide(icon_, thinking);
    hide(ring_, !answering);
    if (answering) ui::animate(ring_, Motion::PULSE, 2400);
    else ui::still(ring_);
  }

  std::function<PopupView()> read_;
  std::function<void()> tapped_, dismissed_;
  int width_ = 0, word_w_ = 1, tallest_ = 0, disc_ = 0, head_ = 0, text_top_ = 0;
  Phase phase_ = Phase::IDLE;
  tessera::Tone tone_ = tessera::Tone::NORMAL;
  uint32_t icon_code_ = 0;
  tessera::Text said_text_, word_text_;
  lv_obj_t *ripples_[2]{}, *ring_{}, *arc_{}, *fill_{}, *icon_{}, *dots_[3]{}, *word_{}, *heard_{}, *box_{}, *said_{};
};

}  // namespace esphome::voice_assist
