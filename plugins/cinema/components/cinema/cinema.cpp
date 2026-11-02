#include "cinema.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace esphome::cinema {

using tessera::Font;
namespace ui = tessera::ui;

// TMDB's posters: the path the list gives, at one width for every place, so a film's poster is fetched once by the
// app whichever place shows it.
static const char *const POSTERS = "https://image.tmdb.org/t/p/w342";
// The film in the middle and up to SIDE on each side of it: the manifest's `pictures` is 2 * SIDE + 1.
static constexpr int SIDE = 3;
// A film's rating counts from this many votes on (a film just out has a handful).
static constexpr int VOTES = 20;

// One film, in fixed fields: the list is a tessera::List, one block in PSRAM, never many small strings in the memory
// inside the chip that a picture's download and Wi-Fi need (docs/FIRMWARE_API.md, "Memory").
struct Film {
  char title[52] = {};    // the app cuts a text at 48 bytes
  char poster[48] = {};   // TMDB's path, "/abc.jpg
  uint32_t date = 0;
  float rating = 0;
  int votes = 0;
  int genre[2] = {0, 0};   // TMDB's genre ids, the first two
  bool rated() const { return votes >= VOTES && rating > 0; }
};

// The tile. With pictures (a board that shows them) and room for it, a cover flow of posters: the film in the middle
// large and flat with its rating on it, its neighbours turned towards it in perspective and lying over one another, a
// reflection under every poster, its title and genres under it. It runs round (left of the first film stand the last)
// and moves on by itself every few seconds; a tap on a neighbour brings it to the middle, a tap beside the middle moves
// one film. One row high it is the poster beside its title; one cell, the poster alone. Without pictures the titles
// with their rating, as a list. The tile's options choose the films: which list, a genre, a lowest rating.
class FilmsTile : public tessera::Tile {
 public:
  explicit FilmsTile(const tessera::Plugin *plugin) : plugin_(plugin) {}

  void create(const tessera::TileContext &c) override {
    width_ = c.width;
    height_ = c.height;
    soon_ = std::string(c.options["list"] | "now_playing") == "upcoming";
    genre_ = atoi(c.options["genre"] | "");
    least_ = c.options["rating"] | 0.0f;
    every_ = c.options["every"] | 15;
    const bool pictures = ui::pictures();
    mode_ = !pictures ? (c.rows >= 2 ? Mode::LIST : Mode::TEXT)
          : c.rows >= 2 ? Mode::FLOW : c.width > c.height ? Mode::ROW : Mode::POSTER;
    gap_ = ui::px(6);

    switch (mode_) {
      case Mode::FLOW: make_flow(c.parent); break;
      case Mode::ROW: make_row(c.parent); break;
      case Mode::POSTER: make_poster(c.parent); break;
      case Mode::LIST: make_list(c.parent); break;
      case Mode::TEXT: make_text(c.parent); break;
    }
    // What the tile says while it has no films: in the middle, over everything.
    note_ = ui::text(c.parent, Font::BODY, theme::MUTED);
    lv_obj_set_width(note_, width_);
    lv_obj_set_style_text_align(note_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(note_, 0, (height_ - 2 * ui::line_height(Font::BODY)) / 2);
  }

  void on_state(JsonObjectConst data) override {
    wait_ = data["wait"] | "";
    stale_ = data["stale"] | false;
    const std::string was = at_ < static_cast<int>(films_.size()) ? films_[at_].title : "";
    films_.clear();
    for (JsonObjectConst item : data["items"].as<JsonArrayConst>()) {
      Film f;
      snprintf(f.title, sizeof(f.title), "%s", item["title"] | "");
      snprintf(f.poster, sizeof(f.poster), "%s", item["poster"] | "");
      f.date = item["date"] | 0u;
      f.rating = item["rating"] | 0.0f;
      f.votes = item["votes"] | 0;
      f.genre[0] = item["genre"] | 0;
      f.genre[1] = item["genre2"] | 0;
      // The tile's filters: one genre (of the first two TMDB names), a lowest rating.
      if (genre_ && f.genre[0] != genre_ && f.genre[1] != genre_) continue;
      if (least_ > 0 && (!f.rated() || f.rating < least_)) continue;
      films_.push_back(f);
    }
    // A new answer keeps the film you were looking at, where it is still in the list.
    at_ = 0;
    for (size_t i = 0; i < films_.size(); ++i) if (films_[i].title == was) at_ = static_cast<int>(i);
    show();
  }

  void on_tick(uint32_t epoch) override {
    // On by itself, every few seconds (the tile's `every`, 0 for never), counted from the last move or tap.
    const uint32_t now = esphome::millis();
    if (every_ > 0 && films_.size() > 1 && wait_.empty()) {
      if (!moved_) moved_ = now;
      if (now - moved_ >= static_cast<uint32_t>(every_) * 1000u)
        step(mode_ == Mode::LIST ? static_cast<int>(rows_.size()) : 1);
    }
    // The day words follow the clock: "Tomorrow" becomes "Today" at midnight.
    if (epoch / 60 != minute_) {
      minute_ = epoch / 60;
      texts();
    }
  }

  void on_theme() override {
    for (auto *l : {title_, meta_, count_}) if (l) ui::set_color(l, l == title_ ? theme::INK : theme::MUTED);
    for (auto &row : rows_) {
      ui::set_color(row.title, theme::INK);
      ui::set_color(row.day, theme::MUTED);
    }
    if (badge_) {
      ui::set_fill(badge_, tessera::Tone::NORMAL, theme::CAMERA_PAGE);
      ui::set_color(badge_text_, theme::CAMERA_INK);
    }
    if (note_) ui::set_color(note_, theme::MUTED);
  }

  void on_tap() override {
    if (films_.empty()) return;
    if (mode_ == Mode::LIST) {
      step(static_cast<int>(rows_.size()));
    } else if (mode_ == Mode::FLOW) {
      // The film whose poster was tapped comes to the middle (the nearer of two that overlap there); a tap beside the
      // posters moves one.
      const tessera::Point p = tessera::tap_point();
      int by = 1;
      if (p.x >= 0) {
        by = p.x < centre_x_ ? -1 : 1;
        int nearest = SIDE + 1;
        for (const auto &slot : slots_) {
          const int far = slot.offset < 0 ? -slot.offset : slot.offset;
          if (!slot.place || lv_obj_has_flag(slot.place, LV_OBJ_FLAG_HIDDEN) || far >= nearest) continue;
          if (p.x >= slot.x && p.x < slot.x + slot.w) {
            nearest = far;
            by = slot.offset ? slot.offset : 1;
          }
        }
      }
      step(by);
    } else {
      step(1);
    }
  }

 private:
  enum class Mode : uint8_t { FLOW, ROW, POSTER, LIST, TEXT };
  struct Slot {
    int offset = 0;   // films from the one in the middle: -SIDE .. SIDE
    int x = 0, w = 0;
    lv_obj_t *place = nullptr;
  };
  struct Row {
    lv_obj_t *title = nullptr, *day = nullptr;
  };

  // Move `by` films (or a page of the list), round, and count the next move from now. The flow slides the way it goes
  // at once (ui::slide_pictures: the next film's poster towards the middle), and the posters are in their places again
  // with the next picture.
  void step(int by) {
    if (mode_ == Mode::FLOW && pitch_) ui::slide_pictures(std::max(-width_, std::min(width_, -by * pitch_)), 320);
    at_ = wrap(at_ + by);
    moved_ = esphome::millis();
    show();
  }

  // The cover flow. The middle poster is as high as the room allows (a poster is 2:3, never wider than half the card),
  // its reflection running on under the title; each neighbour is turned towards the middle, the first a gap beside it,
  // the next ones closer together and lying under the one inside them, spread so the outermost reaches the card's
  // edge. A neighbour that does not fit whole is left out: a place over the card's edge gets no picture.
  void make_flow(lv_obj_t *parent) {
    const int text = ui::line_height(Font::TITLE) + ui::line_height(Font::BODY);
    // The posters and the upper half of their reflections above the words; the rest of a reflection runs on under them.
    const int room = std::max(1, height_ - text - gap_);
    int h = std::min(room * 100 / (100 + tessera::MIRROR_PERCENT / 2), height_ * 100 / (100 + tessera::MIRROR_PERCENT));
    int w = h * 2 / 3;
    if (w > width_ / 2) {
      w = width_ / 2;
      h = w * 3 / 2;
    }
    const int mirrored = h * (100 + tessera::MIRROR_PERCENT) / 100;
    centre_x_ = (width_ - w) / 2;
    const int gap = ui::px(10);
    // The neighbours: as high as the middle one at their near edge, as wide as the turned poster.
    const int side = std::min(w * 55 / 100, centre_x_ - gap);
    std::vector<Slot> frames;
    if (side >= w / 4) {
      const int spread = SIDE > 1 ? (centre_x_ - gap - side) / (SIDE - 1) : 0;
      const int stepx = std::max(w / 6, std::min(w * 80 / 100, spread));
      for (int k = SIDE; k >= 1; --k) {   // the outermost first: a place made later lies over one made earlier
        const int inner = centre_x_ - gap - side - (k - 1) * stepx;
        if (inner < 0) continue;
        frames.push_back({-k, inner, side, nullptr});
        frames.push_back({k, width_ - inner - side, side, nullptr});
      }
    }
    frames.push_back({0, centre_x_, w, nullptr});
    // How far one film is from the next: the middle of the middle poster to the middle of its first neighbour.
    pitch_ = side >= w / 4 ? w / 2 + gap + side / 2 : w + gap;
    for (Slot f : frames) {
      // No block while a poster comes (plugin API 0.17): blocks the height of poster and reflection would read as long
      // bars, and the core keeps a place without one so, whatever comes of its picture.
      f.place = ui::picture(parent, tessera::Placeholder::NONE);
      lv_obj_set_size(f.place, f.w, mirrored);
      lv_obj_set_pos(f.place, f.x, 0);
      slots_.push_back(f);
    }
    badge_right_ = centre_x_ + w;
    badge_top_ = 0;
    make_badge(parent);
    title_ = ui::label(parent, Font::TITLE, theme::INK);
    lv_obj_set_width(title_, width_);
    lv_obj_set_style_text_align(title_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(title_, 0, height_ - text);
    meta_ = ui::label(parent, Font::BODY, theme::MUTED);
    lv_obj_set_width(meta_, width_);
    lv_obj_set_style_text_align(meta_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(meta_, 0, height_ - ui::line_height(Font::BODY));
  }

  // One row high: the poster at the left, as high as the card, its rating on it; the title (two lines when they fit),
  // its genres and its day beside it.
  void make_row(lv_obj_t *parent) {
    const int w = std::min(height_ * 2 / 3, width_ / 2);
    Slot slot;
    slot.w = w;
    slot.place = ui::picture(parent);
    lv_obj_set_size(slot.place, w, height_);
    lv_obj_set_pos(slot.place, 0, 0);
    slots_.push_back(slot);
    badge_right_ = w;
    make_badge(parent);
    const int x = w + ui::px(10), tw = width_ - x;
    const int th = ui::line_height(Font::TITLE), bh = ui::line_height(Font::BODY);
    const bool two = 2 * th + 2 * bh <= height_;
    title_ = two ? ui::text(parent, Font::TITLE, theme::INK) : ui::label(parent, Font::TITLE, theme::INK);
    lv_obj_set_width(title_, tw);
    lv_obj_set_pos(title_, x, 0);
    meta_ = ui::label(parent, Font::BODY, theme::MUTED);
    lv_obj_set_width(meta_, tw);
    lv_obj_set_pos(meta_, x, (two ? 2 : 1) * th);
    count_ = ui::label(parent, Font::BODY, theme::MUTED);
    lv_obj_set_width(count_, tw);
    lv_obj_set_pos(count_, x, height_ - bh);
  }

  // One cell standing up: the poster over the whole card, its rating on it.
  void make_poster(lv_obj_t *parent) {
    Slot slot;
    slot.w = width_;
    slot.place = ui::picture(parent);
    lv_obj_set_size(slot.place, width_, height_);
    lv_obj_set_pos(slot.place, 0, 0);
    slots_.push_back(slot);
    badge_right_ = width_;
    make_badge(parent);
  }

  // The rating on the big poster, in its top right corner: a dark pill over the picture in both looks, as a camera's
  // name over its image, with the number and a dot in the colour of how good it is, as TMDB's own ring.
  void make_badge(lv_obj_t *parent) {
    badge_ = ui::block(parent, theme::CAMERA_PAGE);
    lv_obj_set_style_bg_opa(badge_, LV_OPA_70, 0);
    lv_obj_remove_flag(badge_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(badge_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(badge_, 0, 0);
    badge_dot_ = ui::block(badge_, theme::INK);
    lv_obj_remove_flag(badge_dot_, LV_OBJ_FLAG_CLICKABLE);
    badge_text_ = ui::label(badge_, badge_font(), theme::CAMERA_INK);
    lv_obj_add_flag(badge_, LV_OBJ_FLAG_HIDDEN);
  }
  Font badge_font() const { return mode_ == Mode::FLOW ? Font::TITLE : Font::BODY; }

  // The badge for the film in the middle: its size from the number, its dot's tone from how good it is.
  void badge(const Film *f) {
    if (!badge_) return;
    if (!f || !f->rated()) {
      lv_obj_add_flag(badge_, LV_OBJ_FLAG_HIDDEN);
      return;
    }
    char r[8];
    snprintf(r, sizeof(r), "%.1f", f->rating);
    const Font font = badge_font();
    const bool big = mode_ == Mode::FLOW;
    const int pad = ui::px(big ? 7 : 5), dot = ui::px(big ? 8 : 6), space = ui::px(big ? 5 : 3);
    const int h = ui::line_height(font) + ui::px(big ? 4 : 2);
    const int w = pad + dot + space + ui::text_width(r, font) + pad;
    const int inset = ui::px(big ? 6 : 4);
    lv_obj_set_size(badge_, w, h);
    lv_obj_set_style_radius(badge_, h / 2, 0);
    lv_obj_set_pos(badge_, badge_right_ - w - inset, badge_top_ + inset);
    lv_obj_set_size(badge_dot_, dot, dot);
    lv_obj_set_style_radius(badge_dot_, dot / 2, 0);
    lv_obj_set_pos(badge_dot_, pad, (h - dot) / 2);
    ui::set_fill(badge_dot_, f->rating >= 7 ? tessera::Tone::GOOD : f->rating >= 5.5f ? tessera::Tone::BUSY
                                                                   : tessera::Tone::ALERT, theme::INK);
    lv_obj_set_pos(badge_text_, pad + dot + space, (h - ui::line_height(font)) / 2);
    lv_obj_set_width(badge_text_, ui::text_width(r, font) + 1);
    ui::set_text(badge_text_, r);
    lv_obj_remove_flag(badge_, LV_OBJ_FLAG_HIDDEN);
  }

  // Without pictures, two rows or more: as many films as fit, each its title with its rating (or day) at the right.
  void make_list(lv_obj_t *parent) {
    const int line = std::max(ui::line_height(Font::BODY_LARGE), ui::line_height(Font::BODY)) + ui::px(4);
    const int count = std::max(1, std::min(8, height_ / line));
    const int day_w = width_ * 35 / 100;
    for (int i = 0; i < count; ++i) {
      Row row;
      row.title = ui::label(parent, Font::BODY_LARGE, theme::INK);
      lv_obj_set_width(row.title, width_ - day_w - gap_);
      lv_obj_set_pos(row.title, 0, i * line);
      row.day = ui::label(parent, Font::BODY, theme::MUTED);
      lv_obj_set_width(row.day, day_w);
      lv_obj_set_style_text_align(row.day, LV_TEXT_ALIGN_RIGHT, 0);
      lv_obj_set_pos(row.day, width_ - day_w, i * line + (ui::line_height(Font::BODY_LARGE) - ui::line_height(Font::BODY)) / 2);
      rows_.push_back(row);
    }
  }

  // Without pictures, one row: the title, its genres and rating.
  void make_text(lv_obj_t *parent) {
    const int th = ui::line_height(Font::TITLE), bh = ui::line_height(Font::BODY);
    title_ = 2 * th + bh <= height_ ? ui::text(parent, Font::TITLE, theme::INK) : ui::label(parent, Font::TITLE, theme::INK);
    lv_obj_set_width(title_, width_);
    lv_obj_set_pos(title_, 0, 0);
    meta_ = ui::label(parent, Font::BODY, theme::MUTED);
    lv_obj_set_width(meta_, width_);
    lv_obj_set_pos(meta_, 0, height_ - bh);
  }

  // The day of a film in the screen's words: "In 3 days" for one still to come, "Today", else the date itself.
  std::string day(const Film &f) const {
    if (!f.date) return "";
    const int32_t days = tessera::days_from_today(f.date);
    if (days == 0) return plugin_->text("today");
    if (days > 0 && days != INT32_MIN) {
      const std::string soon = tessera::days_text(days);
      if (!soon.empty() && days <= 6) return soon;
    }
    return tessera::date_text(f.date);
  }

  // A film's genres in the screen's language ("Action · Thriller"), from TMDB's ids.
  std::string genres(const Film &f) const {
    std::string text;
    for (int id : f.genre) {
      if (!id) continue;
      const std::string word = plugin_->text(("g" + std::to_string(id)).c_str());
      if (word.empty()) continue;
      text += (text.empty() ? "" : dot()) + word;
    }
    return text;
  }
  std::string dot() const { return ui::covers(" · ", Font::BODY) ? " · " : ", "; }

  // Under the title: its genres, and for a film still to come the day it comes.
  std::string meta(const Film &f) const {
    if (stale_) return plugin_->text("old");
    std::string text = genres(f);
    if (soon_ || text.empty()) {
      const std::string when = day(f);
      if (!when.empty()) text = text.empty() ? when : text + dot() + when;
    }
    return text;
  }

  // Without pictures there is no badge: the rating, or the day of a film to come, in words.
  std::string aside(const Film &f) const {
    if (!soon_ && f.rated()) {
      char r[8];
      snprintf(r, sizeof(r), "%.1f", f.rating);
      return tessera::fill(plugin_->text("rating"), "r", r);
    }
    return day(f);
  }

  // A position in the list, round: before the first film comes the last.
  int wrap(int i) const {
    const int n = films_.size();
    return n ? ((i % n) + n) % n : 0;
  }

  // Whether a place `offset` films from the middle shows a film: the flow runs round, but never shows a film twice
  // (with few films the outer places stay empty; with an even count the one opposite the middle shows at the right).
  bool shown(int offset) const {
    const int n = films_.size(), far = offset < 0 ? -offset : offset;
    return 2 * far < n || (2 * far == n && offset > 0);
  }

  // The places and the words for the film at at_: every poster's address, turned towards the middle in the flow, with
  // its reflection; a place without a film hidden.
  void show() {
    const int n = films_.size();
    const char *why = wait_.empty() ? (n == 0 ? "none" : nullptr)
                    : wait_ == "not_filled" ? "no_key" : wait_ == "asking" ? "waiting" : "failed";
    ui::set_text(note_, why ? plugin_->text(why) : "");
    const bool flow = mode_ == Mode::FLOW;
    for (auto &slot : slots_) {
      const int i = wrap(at_ + slot.offset);
      const bool there = !why && n > 0 && shown(slot.offset);
      const std::string url = there && films_[i].poster[0] ? std::string(POSTERS) + films_[i].poster : "";
      const tessera::Turn turn = !flow || !slot.offset ? tessera::Turn::NONE
                               : slot.offset < 0 ? tessera::Turn::RIGHT : tessera::Turn::LEFT;
      ui::set_picture(slot.place, url, tessera::Fit::COVER, turn, flow);
      if (there) lv_obj_remove_flag(slot.place, LV_OBJ_FLAG_HIDDEN);
      else lv_obj_add_flag(slot.place, LV_OBJ_FLAG_HIDDEN);
    }
    texts();
  }

  void texts() {
    const int n = films_.size();
    const bool none = !wait_.empty() || n == 0;
    const Film *f = none ? nullptr : &films_[at_];
    if (title_) ui::set_text(title_, f ? f->title : "");
    if (meta_) {
      std::string line = f ? meta(*f) : "";
      // Without pictures a row has no badge: its rating goes with the genres.
      if (f && mode_ == Mode::TEXT && !stale_) {
        const std::string more = aside(*f);
        if (!more.empty() && line.find(more) == std::string::npos) line = line.empty() ? more : line + dot() + more;
      }
      ui::set_text(meta_, line);
    }
    if (count_) ui::set_text(count_, f ? day(*f) : "");
    badge(f);
    for (size_t r = 0; r < rows_.size(); ++r) {
      const size_t i = at_ + r;
      const bool there = !none && i < films_.size();
      ui::set_text(rows_[r].title, there ? films_[i].title : "");
      ui::set_text(rows_[r].day, there ? aside(films_[i]) : "");
    }
  }

  const tessera::Plugin *plugin_;
  Mode mode_ = Mode::TEXT;
  int width_ = 0, height_ = 0, gap_ = 0, centre_x_ = 0, at_ = 0;
  int genre_ = 0, every_ = 15, pitch_ = 0;
  float least_ = 0;
  bool soon_ = false, stale_ = false;
  uint32_t minute_ = 0, moved_ = 0;
  tessera::Text wait_;   // the app's word while it has no films; kept, so in PSRAM
  tessera::List<Film> films_;
  tessera::List<Slot> slots_;
  tessera::List<Row> rows_;
  lv_obj_t *title_{}, *meta_{}, *count_{}, *note_{};
  lv_obj_t *badge_{}, *badge_dot_{}, *badge_text_{};
  int badge_right_ = 0, badge_top_ = 0;
};

void Cinema::setup() {
  add_tile("films", [this]() { return new FilmsTile(this); });
}

}  // namespace esphome::cinema
