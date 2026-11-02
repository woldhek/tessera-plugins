#include "new_music.h"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <string>

namespace esphome::new_music {

using tessera::Font;
namespace ui = tessera::ui;

// Deezer's covers: the md5 the list gives, at one size for every place, so a release's cover is fetched once by the app
// whichever place shows it.
static const char *const COVERS = "https://cdn-images.dzcdn.net/images/cover/";
static const char *const COVER_SIZE = "/500x500-000000-80-0-0.jpg";
// The release in the middle and up to SIDE on each side of it: the manifest's `pictures` is 2 * SIDE + 1.
static constexpr int SIDE = 3;
// A release counts as new this many days after it came out.
static constexpr int NEW_DAYS = 14;
// What plays it: Music Assistant finds the release by its title and artist on whatever service it has.
static const char *const PLAY = "music_assistant.play_media";
static const uint32_t ICON_PLAY = 0xF040A;

// A kept text (in PSRAM) as a plain one, for a call that takes std::string.
static std::string plain(const tessera::Text &t) { return std::string(t.data(), t.size()); }

static std::string cover_url(const Release &r) { return r.cover[0] ? std::string(COVERS) + r.cover + COVER_SIZE : ""; }

// Whether two texts are the same but for case: a player says "Patient Zero" where Deezer said "Patient Zero", or not.
static bool same(const char *a, const char *b) {
  if (!a || !b || !*a) return false;
  for (; *a && *b; ++a, ++b)
    if (std::tolower(static_cast<unsigned char>(*a)) != std::tolower(static_cast<unsigned char>(*b))) return false;
  return !*a && !*b;
}

// Whether the player plays this release now: its album and, when it names one, the artist at the start of its artist
// ("Taylor Swift, Ed Sheeran" plays a release of Taylor Swift).
static bool playing(const Release &r, const std::string &state, const std::string &album, const std::string &artist) {
  if (state != "playing" || !same(album.c_str(), r.title)) return false;
  if (artist.empty()) return true;
  const size_t n = strlen(r.artist);
  return artist.size() >= n && same(artist.substr(0, n).c_str(), r.artist);
}

static Release::Kind kind_of(const char *word) {
  return !strcmp(word, "single") ? Release::SINGLE : !strcmp(word, "ep") ? Release::EP : Release::ALBUM;
}
static const char *kind_key(Release::Kind k) { return k == Release::SINGLE ? "single" : k == Release::EP ? "ep" : "album"; }

// How new a release is: >= 0 the days since it came out, < 0 the days until it comes (Deezer lists a release ahead).
static int32_t age(const Release &r) {
  if (!r.date) return INT32_MIN;
  const int32_t days = tessera::days_from_today(r.date);
  return days == INT32_MIN ? INT32_MIN : -days;
}

// The day of a release in the screen's words: "Today", "Out in 3 days" for one to come, else its date.
static std::string day(const tessera::Plugin *plugin, const Release &r) {
  const int32_t a = age(r);
  if (a == INT32_MIN) return "";
  if (a == 0) return plugin->text("today");
  if (a < 0) {
    const std::string soon = -a <= 6 ? tessera::days_text(-a) : tessera::date_text(r.date);
    return tessera::fill(plugin->text("out"), "day", soon);
  }
  return tessera::date_text(r.date);
}

// The words beside a cover, as many of them as fit `width`: the artist, then the kind, then the day.
static std::string meta(const tessera::Plugin *plugin, const Release &r, int width, Font font, bool with_artist = true) {
  const std::string dot = ui::covers(" · ", font) ? " · " : ", ";
  std::string parts[3] = {with_artist ? r.artist : "", plugin->text(kind_key(r.kind)), day(plugin, r)};
  std::string line;
  for (const auto &part : parts) {
    if (part.empty()) continue;
    const std::string next = line.empty() ? part : line + dot + part;
    if (!line.empty() && ui::text_width(next, font) > width) break;
    line = next;
  }
  return line;
}

// A record, for a board without pictures (the CYD): a dark disc with two grooves and a label in one of the four
// colours of the Tessera mark, the artist's own (the same artist, the same colour), a hole in the middle. Drawn of
// rounded blocks only, so it costs no picture memory and no flash of its own.
struct Vinyl {
  lv_obj_t *body = nullptr, *outer = nullptr, *inner = nullptr, *label = nullptr, *hole = nullptr;
  int size = 0;

  void make(lv_obj_t *parent, int d) {
    size = d;
    body = round(parent, theme::CAMERA_PAGE, d);
    // A ring in the card's colour round it: records that lie over one another stay apart.
    lv_obj_set_style_border_width(body, std::max(2, ui::px(2)), 0);
    outer = groove(d * 84 / 100);
    inner = groove(d * 64 / 100);
    label = round(body, theme::MARK_BLUE, d * 38 / 100);
    hole = round(label, theme::CARD, std::max(2, d * 6 / 100));
    lv_obj_center(label);
    lv_obj_center(hole);
  }
  void place(int x, int y) const { lv_obj_set_pos(body, x, y); }
  void show(bool on) const {
    if (on) lv_obj_remove_flag(body, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(body, LV_OBJ_FLAG_HIDDEN);
  }
  // The colours, again with every change of the look.
  void paint(const Release &r) const {
    static const theme::Role COLOURS[] = {theme::MARK_BLUE, theme::MARK_AMBER, theme::MARK_PURPLE, theme::MARK_GREEN};
    uint32_t h = 5381;
    for (const char *c = r.artist[0] ? r.artist : r.title; *c; ++c) h = h * 33 + static_cast<unsigned char>(*c);
    ui::set_fill(body, tessera::Tone::NORMAL, theme::CAMERA_PAGE);
    lv_obj_set_style_border_color(body, ui::color(theme::CARD), 0);
    for (auto *g : {outer, inner}) lv_obj_set_style_border_color(g, ui::color(theme::CAMERA_INK), 0);
    ui::set_fill(label, tessera::Tone::NORMAL, COLOURS[h % 4]);
    ui::set_fill(hole, tessera::Tone::NORMAL, theme::CARD);
  }

 private:
  static lv_obj_t *round(lv_obj_t *parent, theme::Role fill, int d) {
    lv_obj_t *o = ui::block(parent, fill);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(o, 0, 0);
    lv_obj_set_size(o, d, d);
    lv_obj_set_style_radius(o, LV_RADIUS_CIRCLE, 0);
    return o;
  }
  lv_obj_t *groove(int d) {
    lv_obj_t *o = round(body, theme::CAMERA_PAGE, d);
    lv_obj_set_style_bg_opa(o, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(o, std::max(1, ui::px(1)), 0);
    lv_obj_set_style_border_opa(o, LV_OPA_20, 0);
    lv_obj_center(o);
    return o;
  }
};

// The tile. With pictures and room for it, a cover flow: the release in the middle large and flat with how new it is
// on it, its neighbours turned towards it in perspective and lying over one another, a reflection under every cover,
// its title and artist under it. It runs round and moves on by itself every few seconds. A tap left of the middle cover
// goes back one, right of it on one, on it opens the release. One row high it is the cover beside its title; one cell,
// the cover alone; one column, the cover over its words. Without pictures records stand in for the covers, in the
// same places. The tile's entity is the Music Assistant player that plays.
class ReleasesTile : public tessera::Tile {
 public:
  explicit ReleasesTile(NewMusic *plugin) : plugin_(plugin) {}

  void create(const tessera::TileContext &c) override {
    width_ = c.width;
    height_ = c.height;
    tile_ = c.tile;
    player_ = tessera::Text(c.entity ? c.entity : "");
    every_ = c.options["every"] | 15;
    pictures_ = ui::pictures();
    mode_ = c.rows >= 2 ? (c.columns >= 2 ? Mode::FLOW : Mode::STACK) : c.width > c.height ? Mode::ROW : Mode::COVER;
    gap_ = ui::px(6);
    switch (mode_) {
      case Mode::FLOW: make_flow(c.parent); break;
      case Mode::ROW: make_row(c.parent); break;
      case Mode::COVER: make_cover(c.parent); break;
      case Mode::STACK: make_stack(c.parent); break;
    }
    // What the tile says while it has nothing to show: in the middle, over everything.
    note_ = ui::text(c.parent, Font::BODY, theme::MUTED);
    lv_obj_set_width(note_, width_);
    lv_obj_set_style_text_align(note_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(note_, 0, (height_ - 2 * ui::line_height(Font::BODY)) / 2);
  }

  void on_state(JsonObjectConst data) override {
    // The player's part comes again with every change of the player, the list only with a new answer.
    if (!data["state"].isNull()) {
      state_ = tessera::Text(data["state"] | "");
      album_ = tessera::Text(data["attributes"]["media_album_name"] | "");
      playing_artist_ = tessera::Text(data["attributes"]["media_artist"] | "");
      plugin_->player_name = tessera::Text(data["name"] | "");
    }
    if (data["items"].isNull() && data["wait"].isNull()) {
      texts();
      return;
    }
    wait_ = tessera::Text(data["wait"] | "");
    stale_ = data["stale"] | false;
    const std::string was = at_ < static_cast<int>(releases_.size()) ? releases_[at_].cover : "";
    releases_.clear();
    for (JsonObjectConst item : data["items"].as<JsonArrayConst>()) {
      Release r;
      snprintf(r.title, sizeof(r.title), "%s", item["title"] | "");
      snprintf(r.artist, sizeof(r.artist), "%s", item["artist"] | "");
      snprintf(r.cover, sizeof(r.cover), "%s", item["cover"] | "");
      r.date = item["date"] | 0u;
      r.kind = kind_of(item["kind"] | "album");
      if (!r.title[0]) continue;
      // A release two picked artists share comes once.
      bool twice = false;
      for (const auto &o : releases_) twice = twice || (r.cover[0] && !strcmp(o.cover, r.cover)) || !strcmp(o.title, r.title);
      if (!twice) releases_.push_back(r);
    }
    // A new answer keeps the release you were looking at, where it is still in the list.
    at_ = 0;
    for (size_t i = 0; i < releases_.size(); ++i) if (was == releases_[i].cover) at_ = static_cast<int>(i);
    show();
  }

  void on_tick(uint32_t epoch) override {
    // On by itself, every few seconds (the tile's `every`, 0 for never), counted from the last move or tap.
    const uint32_t now = esphome::millis();
    if (every_ > 0 && releases_.size() > 1 && wait_.empty()) {
      if (!moved_) moved_ = now;
      if (now - moved_ >= static_cast<uint32_t>(every_) * 1000u) step(1);
    }
    // The day words follow the clock: "Out tomorrow" becomes "Today" at midnight.
    if (epoch / 60 != minute_) {
      minute_ = epoch / 60;
      texts();
    }
  }

  void on_theme() override {
    if (title_) ui::set_color(title_, theme::INK);
    if (meta_) ui::set_color(meta_, theme::MUTED);
    if (badge_) ui::set_color(badge_text_, theme::CAMERA_INK);
    if (note_) ui::set_color(note_, theme::MUTED);
    show();
  }

  void on_tap() override {
    if (releases_.empty() || !wait_.empty()) return;
    const tessera::Point p = tessera::tap_point();
    if (mode_ == Mode::FLOW) {
      // Left of the middle cover back, right of it on, on it the release itself.
      if (p.x >= 0 && p.x < centre_x_) return step(-1);
      if (p.x >= centre_x_ + cover_w_) return step(1);
      return open(at_);
    }
    open(at_);
  }

 private:
  enum class Mode : uint8_t { FLOW, ROW, COVER, STACK };
  struct Slot {
    int offset = 0;   // releases from the one in the middle: -SIDE .. SIDE
    int x = 0, w = 0;
    lv_obj_t *place = nullptr;   // a picture's place, or without pictures:
    Vinyl vinyl;                 // a record
  };

  // The card of one release, with what the tile knows of it: the card is made after this and reads it from the plugin.
  void open(int i) {
    plugin_->chosen = releases_[i];
    moved_ = esphome::millis();
    tessera::open_card(plugin_->plugin_id(), "release", plain(player_), tile_);
  }

  // The art of one place: a picture's place (w x h, the reflection's room in h), or a record of w in the middle of h.
  void art(lv_obj_t *parent, Slot &slot, int x, int y, int w, int h, bool block = true) {
    if (pictures_) {
      slot.place = ui::picture(parent, block ? tessera::Placeholder::BLOCK : tessera::Placeholder::NONE);
      lv_obj_set_size(slot.place, w, h);
      lv_obj_set_pos(slot.place, x, y);
    } else {
      slot.vinyl.make(parent, w);
      slot.vinyl.place(x, y + (std::min(h, cover_h_) - w) / 2);
    }
  }

  // Move `by` releases, round, and count the next move from now. The flow slides the way it
  // goes at once (the next cover towards the middle), and the covers are in their places again with the next picture.
  void step(int by) {
    if (mode_ == Mode::FLOW && pitch_) ui::slide_pictures(std::max(-width_, std::min(width_, -by * pitch_)), 320);
    at_ = wrap(at_ + by);
    moved_ = esphome::millis();
    show();
  }

  // The cover flow, as Cinema's with square covers. The middle cover is as high as the room allows (never wider than
  // half the card), its reflection running on under the words; each neighbour is turned towards the middle, the first
  // a gap beside it, the next ones closer together and lying under the one inside them. A neighbour that does not fit
  // whole is left out.
  void make_flow(lv_obj_t *parent) {
    const int text = ui::line_height(Font::TITLE) + ui::line_height(Font::BODY);
    const int room = std::max(1, height_ - text - gap_);
    // A record has no reflection: it takes the room a cover and its reflection would.
    int h = pictures_ ? std::min(room * 100 / (100 + tessera::MIRROR_PERCENT / 2), height_ * 100 / (100 + tessera::MIRROR_PERCENT))
                      : room;
    h = std::min(h, width_ / 2);
    cover_h_ = h;
    const int w = h;
    cover_w_ = w;
    const int mirrored = pictures_ ? h * (100 + tessera::MIRROR_PERCENT) / 100 : h;
    centre_x_ = (width_ - w) / 2;
    const int gap = ui::px(10);
    const int side = std::min(w * 55 / 100, centre_x_ - gap);
    tessera::List<Slot> frames;
    if (!pictures_) {
      // Records: the neighbours a stack behind the middle one, each smaller and further out, as records leaning on a
      // shelf; the outermost first, so one further in lies over it.
      for (int k = SIDE; k >= 1; --k) {
        const int d = w * (100 - 19 * k) / 100;
        const int out = w * (40 + 32 * (k - 1)) / 100;   // how far its far edge reaches past the middle record
        const int left = centre_x_ - out, right = centre_x_ + w + out - d;
        if (left < 0 || d < w / 4) continue;
        frames.push_back({-k, left, d, nullptr});
        frames.push_back({k, right, d, nullptr});
      }
    } else if (side >= w / 4) {
      const int spread = SIDE > 1 ? (centre_x_ - gap - side) / (SIDE - 1) : 0;
      // Close together, as a stack of records: on a wide tile the flow keeps to the middle.
      const int stepx = std::max(w / 6, std::min(side * 70 / 100, spread));
      for (int k = SIDE; k >= 1; --k) {   // the outermost first: a place made later lies over one made earlier
        const int inner = centre_x_ - gap - side - (k - 1) * stepx;
        if (inner < 0) continue;
        frames.push_back({-k, inner, side, nullptr});
        frames.push_back({k, width_ - inner - side, side, nullptr});
      }
    }
    frames.push_back({0, centre_x_, w, nullptr});
    pitch_ = side >= w / 4 ? w / 2 + gap + side / 2 : w + gap;
    for (Slot f : frames) {
      // No block while a cover comes (plugin API 0.17): a block the height of cover and reflection reads as a bar.
      art(parent, f, f.x, 0, f.w, mirrored, false);
      slots_.push_back(f);
    }
    badge_right_ = centre_x_ + w;
    make_badge(parent);
    title_ = ui::label(parent, Font::TITLE, theme::INK);
    lv_obj_set_width(title_, width_);
    lv_obj_set_style_text_align(title_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(title_, 0, height_ - text);
    meta_ = ui::label(parent, Font::BODY, theme::MUTED);
    lv_obj_set_width(meta_, width_);
    lv_obj_set_style_text_align(meta_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(meta_, 0, height_ - ui::line_height(Font::BODY));
    meta_w_ = width_;
  }

  // One row high: the cover at the left, as high as the card; the title (two lines when they fit) and the artist,
  // kind and day beside it.
  void make_row(lv_obj_t *parent) {
    const int w = std::min(height_, width_ / 2);
    Slot slot;
    slot.w = w;
    cover_h_ = w;
    art(parent, slot, 0, (height_ - w) / 2, w, w);
    slots_.push_back(slot);
    make_words(parent, w + ui::px(10));
  }

  // The title (two lines when they fit) over the artist, kind and day, from `x` to the card's right edge, in the middle
  // of the card's height. Beside a small cover the day says how new it is, as no badge fits on it.
  void make_words(lv_obj_t *parent, int x) {
    const int tw = width_ - x;
    const int th = ui::line_height(Font::TITLE), bh = ui::line_height(Font::BODY), space = ui::px(2);
    title_two_ = 2 * th + space + bh <= height_;
    title_ = title_two_ ? ui::text(parent, Font::TITLE, theme::INK) : ui::label(parent, Font::TITLE, theme::INK);
    lv_obj_set_width(title_, tw);
    if (title_two_) lv_obj_set_height(title_, 2 * th);
    meta_ = ui::label(parent, Font::BODY, theme::MUTED);
    lv_obj_set_width(meta_, tw);
    words_x_ = x;
    meta_w_ = tw;
  }

  // The words of make_words, in the middle of the height for the lines the title takes now.
  void place_words(const Release *r) {
    if (!title_ || mode_ == Mode::FLOW || mode_ == Mode::STACK) return;
    const int th = ui::line_height(Font::TITLE), bh = ui::line_height(Font::BODY), space = ui::px(2);
    const int lines = title_two_ && r && ui::text_width(r->title, Font::TITLE) > meta_w_ ? 2 : 1;
    const int top = std::max(0, (height_ - lines * th - space - bh) / 2);
    if (title_two_) lv_obj_set_height(title_, lines * th);
    lv_obj_set_pos(title_, words_x_, top);
    lv_obj_set_pos(meta_, words_x_, top + lines * th + space);
  }

  // One cell: the cover over the whole card, how new it is on it.
  void make_cover(lv_obj_t *parent) {
    const int w = std::min(width_, height_);
    Slot slot;
    slot.w = w;
    cover_h_ = w;
    cover_w_ = w;
    art(parent, slot, (width_ - w) / 2, (height_ - w) / 2, w, w);
    slots_.push_back(slot);
    badge_right_ = (width_ + w) / 2;
    badge_top_ = (height_ - w) / 2;
    make_badge(parent);
  }

  // On the big cover, in its top right corner: a dark pill over the picture in both looks, as Cinema's rating, with a
  // dot in the accent and "New", "Out in 3 days" or "Playing".
  void make_badge(lv_obj_t *parent) {
    // On a record the badge is part of it, in the corner of its square, so it moves and hides with it.
    for (const auto &slot : slots_) {
      if (pictures_ || slot.offset || !slot.vinyl.body) continue;
      parent = slot.vinyl.body;
      badge_right_ = slot.vinyl.size;
      badge_top_ = 0;
      cover_w_ = slot.vinyl.size;
    }
    badge_ = ui::block(parent, theme::CAMERA_PAGE);
    lv_obj_set_style_bg_opa(badge_, LV_OPA_80, 0);
    lv_obj_remove_flag(badge_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(badge_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(badge_, 0, 0);
    badge_dot_ = ui::block(badge_, theme::INK);
    lv_obj_remove_flag(badge_dot_, LV_OBJ_FLAG_CLICKABLE);
    badge_text_ = ui::label(badge_, Font::BODY, theme::CAMERA_INK);
    lv_obj_add_flag(badge_, LV_OBJ_FLAG_HIDDEN);
  }

  void badge(const Release *r) {
    if (!badge_) return;
    const int32_t a = r ? age(*r) : INT32_MIN;
    const bool now = r && playing(*r, plain(state_), plain(album_), plain(playing_artist_));
    std::string words;
    tessera::Tone tone = tessera::Tone::ACCENT;
    if (now) {
      words = plugin_->text("playing");
      tone = tessera::Tone::GOOD;
    } else if (a != INT32_MIN && a < 0) {
      words = day(plugin_, *r);
      tone = tessera::Tone::BUSY;
    } else if (a != INT32_MIN && a <= NEW_DAYS) {
      words = plugin_->text("new");
    }
    // The large face on a large cover; where its words would take more than two thirds of the cover, the small one.
    const int across = cover_w_ ? cover_w_ : width_;
    bool big = mode_ == Mode::FLOW &&
               ui::px(7) * 2 + ui::px(8) + ui::px(5) + ui::text_width(words, Font::TITLE) <= across * 2 / 3;
    const Font font = big ? Font::TITLE : Font::BODY;
    if (words.empty() || !ui::covers(words, font)) {
      lv_obj_add_flag(badge_, LV_OBJ_FLAG_HIDDEN);
      return;
    }
    ui::set_font(badge_text_, font);
    const int pad = ui::px(big ? 7 : 5), dot = ui::px(big ? 8 : 6), space = ui::px(big ? 5 : 3);
    const int h = ui::line_height(font) + ui::px(big ? 4 : 2);
    const int inset = ui::px(big ? 6 : 4);
    const int room = std::max(0, cover_w_ ? cover_w_ - 2 * inset : width_ - 2 * inset);
    const int tw = std::min(ui::text_width(words, font) + 1, std::max(0, room - 2 * pad - dot - space));
    const int w = pad + dot + space + tw + pad;
    lv_obj_set_size(badge_, w, h);
    lv_obj_set_style_radius(badge_, h / 2, 0);
    lv_obj_set_pos(badge_, badge_right_ - w - inset, badge_top_ + inset);
    lv_obj_set_size(badge_dot_, dot, dot);
    lv_obj_set_style_radius(badge_dot_, dot / 2, 0);
    lv_obj_set_pos(badge_dot_, pad, (h - dot) / 2);
    ui::set_fill(badge_dot_, tone, theme::INK);
    lv_obj_set_pos(badge_text_, pad + dot + space, (h - ui::line_height(font)) / 2);
    lv_obj_set_width(badge_text_, tw);
    ui::set_text(badge_text_, words);
    lv_obj_remove_flag(badge_, LV_OBJ_FLAG_HIDDEN);
  }

  // One column, two rows or more: the cover as wide as the card, how new it is on it, its title and words under it.
  void make_stack(lv_obj_t *parent) {
    const int text = ui::line_height(Font::TITLE) + ui::line_height(Font::BODY);
    const int w = std::max(1, std::min(width_, height_ - text - gap_));
    cover_w_ = w;
    Slot slot;
    slot.w = w;
    cover_h_ = w;
    art(parent, slot, (width_ - w) / 2, 0, w, w);
    slots_.push_back(slot);
    badge_right_ = (width_ + w) / 2;
    make_badge(parent);
    title_ = ui::label(parent, Font::TITLE, theme::INK);
    lv_obj_set_width(title_, width_);
    lv_obj_set_style_text_align(title_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(title_, 0, height_ - text);
    meta_ = ui::label(parent, Font::BODY, theme::MUTED);
    lv_obj_set_width(meta_, width_);
    lv_obj_set_style_text_align(meta_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(meta_, 0, height_ - ui::line_height(Font::BODY));
    meta_w_ = width_;
  }

  // A position in the list, round: before the first release comes the last.
  int wrap(int i) const {
    const int n = releases_.size();
    return n ? ((i % n) + n) % n : 0;
  }

  // Whether a place `offset` releases from the middle shows one: the flow runs round, but never shows a release twice.
  bool shown(int offset) const {
    const int n = releases_.size(), far = offset < 0 ? -offset : offset;
    return 2 * far < n || (2 * far == n && offset > 0);
  }

  const char *why() const {
    if (wait_.empty()) return releases_.empty() ? "none" : nullptr;
    return wait_ == "not_filled" ? "no_artists" : wait_ == "asking" ? "waiting" : "failed";
  }

  void show() {
    const int n = releases_.size();
    const char *note = why();
    ui::set_text(note_, note ? plugin_->text(note) : "");
    const bool flow = mode_ == Mode::FLOW;
    for (auto &slot : slots_) {
      const int i = wrap(at_ + slot.offset);
      const bool there = !note && n > 0 && shown(slot.offset);
      const tessera::Turn turn = !flow || !slot.offset ? tessera::Turn::NONE
                               : slot.offset < 0 ? tessera::Turn::RIGHT : tessera::Turn::LEFT;
      if (slot.place) {
        ui::set_picture(slot.place, there ? cover_url(releases_[i]) : "", tessera::Fit::COVER, turn, flow);
        if (there) lv_obj_remove_flag(slot.place, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(slot.place, LV_OBJ_FLAG_HIDDEN);
      } else if (slot.vinyl.body) {
        if (there) slot.vinyl.paint(releases_[i]);
        slot.vinyl.show(there);
      }
    }
    texts();
  }

  void texts() {
    const bool none = why() != nullptr;
    const Release *r = none ? nullptr : &releases_[at_];
    if (title_) ui::set_text(title_, r ? r->title : "");
    place_words(r);
    if (meta_) ui::set_text(meta_, !r ? "" : stale_ ? plugin_->text("old") : meta(plugin_, *r, meta_w_, Font::BODY));
    badge(r);
  }

  NewMusic *plugin_;
  Mode mode_ = Mode::FLOW;
  int width_ = 0, height_ = 0, gap_ = 0, centre_x_ = 0, cover_w_ = 0, at_ = 0, tile_ = -1;
  int every_ = 15, pitch_ = 0, meta_w_ = 0, words_x_ = 0, cover_h_ = 0;
  bool stale_ = false, title_two_ = false, pictures_ = true;
  uint32_t minute_ = 0, moved_ = 0;
  tessera::Text wait_, player_, state_, album_, playing_artist_;
  tessera::List<Release> releases_;
  tessera::List<Slot> slots_;
  lv_obj_t *title_{}, *meta_{}, *note_{};
  lv_obj_t *badge_{}, *badge_dot_{}, *badge_text_{};
  int badge_right_ = 0, badge_top_ = 0;
};

// The card of one release: its cover large, then its kind and day over its title, the artist under it, and a key that plays it on the tile's
// Music Assistant player. Under the key, how that goes: starting, playing, or what to do when there is no player.
class ReleaseCard : public tessera::Card {
 public:
  explicit ReleaseCard(NewMusic *plugin) : plugin_(plugin) {}

  void open(const tessera::CardContext &c) override {
    release_ = plugin_->chosen;
    player_ = tessera::Text(c.entity ? c.entity : "");
    pad_ = ui::px(16);
    card_h_ = c.height;
    // The words, from the top: the kind and day, the title (two lines in the largest face that fits), the artist, and
    // the key a gap under it, which says how playing goes. Without a player a note takes the key's place.
    const int bh = ui::line_height(Font::BODY), bl = ui::line_height(Font::BODY_LARGE);
    key_h_ = std::max(ui::mm(9), bl + ui::px(18));
    const int gap = ui::px(12);
    const int foot = player_.empty() ? 2 * bh : key_h_;
    auto need = [&](int lines, Font face) {
      return bh + ui::px(6) + lines * ui::line_height(face) + ui::px(4) + bl + gap + foot;
    };
    // The cover, as large as the card allows beside the words: at the left of a card lying down, the words in the
    // middle of its height; on top of a card standing up, the words under it. Without pictures (the CYD) a record
    // stands in for it, a little smaller, so the words keep their room.
    int top = pad_, room = c.height - 2 * pad_;
    x0_ = pad_;
    width_ = c.width;
    {
      const bool pictures = ui::pictures();
      beside_ = c.width > c.height;
      side_ = beside_ ? std::min(c.height - 2 * pad_, c.width * (pictures ? 45 : 36) / 100)
                      : std::max(c.height * 40 / 100, std::min(c.width - 2 * pad_, room - ui::px(14) - need(2, Font::HEADLINE)));
      if (pictures) {
        cover_ = ui::picture(c.parent);
        lv_obj_set_size(cover_, side_, side_);
        lv_obj_set_pos(cover_, pad_, pad_);
        ui::set_picture(cover_, cover_url(release_), tessera::Fit::COVER);
      } else {
        vinyl_.make(c.parent, side_);
        vinyl_.place(pad_, pad_);
        vinyl_.paint(release_);
      }
      if (beside_) {
        x0_ = pad_ + side_ + ui::px(20);
        width_ = c.width - x0_ + pad_;
      } else {
        top = pad_ + side_ + ui::px(14);
        room = c.height - pad_ - top;
      }
    }
    const int w = width_ - 2 * pad_;
    Font face = Font::HEADLINE;
    int lines = 2;
    if (need(2, face) > room) lines = 1;
    if (need(lines, face) > room) face = Font::TITLE;
    // A title that does not fit its lines in the large face takes the smaller one, never dots where a face fits.
    if (face == Font::HEADLINE && ui::text_width(release_.title, face) > lines * w * 9 / 10) {
      face = Font::TITLE;
      if (lines == 1 && need(2, face) <= room) lines = 2;
    }
    const int hh = ui::line_height(face);
    int y = top;
    kind_ = ui::label(c.parent, Font::BODY, theme::MUTED);
    lv_obj_set_width(kind_, w);
    lv_obj_set_pos(kind_, x0_, y);
    y += bh + ui::px(6);
    title_ = lines == 2 ? ui::text(c.parent, face, theme::INK) : ui::label(c.parent, face, theme::INK);
    lv_obj_set_size(title_, w, lines * hh);
    lv_obj_set_pos(title_, x0_, y);
    title_y_ = y;
    title_lines_ = lines;
    title_face_ = face;
    y += lines * hh + ui::px(4);
    artist_ = ui::label(c.parent, Font::BODY_LARGE, theme::INK_SOFT);
    lv_obj_set_width(artist_, w);
    lv_obj_set_pos(artist_, x0_, y);
    // The key, as wide as its words, a finger high at least.
    key_ = ui::block(c.parent, theme::ACCENT);
    lv_obj_remove_flag(key_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(key_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(key_, 0, 0);
    lv_obj_set_style_radius(key_, key_h_ / 2, 0);
    key_icon_ = ui::label(key_, Font::ICON_SMALL, theme::ON_ACCENT);
    ui::set_text(key_icon_, ui::icon(ICON_PLAY));
    key_text_ = ui::label(key_, Font::BODY_LARGE, theme::ON_ACCENT);
    note_ = ui::text(c.parent, Font::BODY, theme::MUTED);
    lv_obj_set_size(note_, w, 2 * bh);
    paint();
  }

  void on_state(JsonObjectConst data) override {
    if (data["state"].isNull()) return;
    state_ = tessera::Text(data["state"] | "");
    album_ = tessera::Text(data["attributes"]["media_album_name"] | "");
    playing_artist_ = tessera::Text(data["attributes"]["media_artist"] | "");
    paint();
  }

  void on_theme() override {
    if (vinyl_.body) vinyl_.paint(release_);
    ui::set_color(kind_, theme::MUTED);
    ui::set_color(title_, theme::INK);
    ui::set_color(artist_, theme::INK_SOFT);
    ui::set_color(note_, theme::MUTED);
    ui::set_color(key_icon_, theme::ON_ACCENT);
    ui::set_color(key_text_, theme::ON_ACCENT);
  }

  // A player that does not start within a while: the key says Play again, so the person can try again.
  void on_tick(uint32_t) override {
    if (sent_ == Sent::YES && esphome::millis() - sent_at_ > 20000) {
      sent_ = Sent::NO;
      paint();
    }
  }

  void on_tap() override {
    const tessera::Point p = tessera::tap_point();
    if (player_.empty() || p.y < key_y_ - ui::px(8) || p.y > key_y_ + key_h_ + ui::px(8)) return;
    if (p.x < key_x_ - ui::px(8) || p.x > key_x_ + key_w_ + ui::px(8)) return;
    JsonDocument d;
    d["media_id"] = release_.title;
    d["media_type"] = "album";
    if (release_.artist[0]) d["artist"] = release_.artist;
    sent_ = tessera::action(PLAY, plain(player_), d) ? Sent::YES : Sent::FAILED;
    sent_at_ = esphome::millis();
    paint();
  }

 private:
  enum class Sent : uint8_t { NO, YES, FAILED };

  void paint() {
    const Release &r = release_;
    std::string line = plugin_->text(kind_key(r.kind));
    const std::string when = day(plugin_, r);
    if (!when.empty()) line += (ui::covers(" · ", Font::BODY) ? " · " : ", ") + when;
    ui::set_text(kind_, line);
    ui::set_text(title_, r.title);
    ui::set_text(artist_, r.artist);
    // A short title takes one line: the artist and the key move up under it. Beside the cover the words stand in the
    // middle of its height, and the two together in the middle of the card.
    const int hh = ui::line_height(title_face_), bh = ui::line_height(Font::BODY), bl = ui::line_height(Font::BODY_LARGE);
    const int used = title_lines_ == 2 && ui::text_width(r.title, title_face_) <= width_ - 2 * pad_ ? 1 : title_lines_;
    lv_obj_set_height(title_, used * hh);
    int y = title_y_;
    if (beside_) {
      const int block = bh + ui::px(6) + used * hh + ui::px(4) + bl + ui::px(12) + key_h_;
      const int top = std::max(pad_, (card_h_ - std::max(side_, block)) / 2);
      if (cover_) lv_obj_set_y(cover_, top);
      if (vinyl_.body) lv_obj_set_y(vinyl_.body, top);
      y = top + std::max(0, (side_ - block) / 2);
      lv_obj_set_y(kind_, y);
      y += bh + ui::px(6);
      lv_obj_set_y(title_, y);
    }
    y += used * hh + ui::px(4);
    lv_obj_set_y(artist_, y);
    y += bl + ui::px(12);
    key_y_ = y;
    lv_obj_set_pos(note_, x0_, y);
    const bool now = playing(r, plain(state_), plain(album_), plain(playing_artist_));
    if (now) sent_ = Sent::NO;
    // The key: "Play on Living room" when that fits, else "Play".
    const int room = width_ - 2 * pad_;
    const int icon_w = ui::line_height(Font::ICON_SMALL), space = ui::px(8), inner = ui::px(22);
    std::string words = plugin_->text(now ? "playing" : sent_ == Sent::YES ? "sent" : sent_ == Sent::FAILED ? "retry" : "play");
    const std::string name = plain(plugin_->player_name);
    if (!name.empty() && !now && sent_ == Sent::NO) {
      const std::string on = tessera::fill(plugin_->text("play_on"), "player", name);
      if (inner + icon_w + space + ui::text_width(on, Font::BODY_LARGE) + inner <= room) words = on;
    }
    const int tw = std::min(ui::text_width(words, Font::BODY_LARGE) + 1, room - 2 * inner - icon_w - space);
    key_w_ = inner + icon_w + space + tw + inner;
    key_x_ = x0_;
    lv_obj_set_size(key_, key_w_, key_h_);
    lv_obj_set_pos(key_, key_x_, key_y_);
    lv_obj_set_pos(key_icon_, inner, (key_h_ - ui::line_height(Font::ICON_SMALL)) / 2);
    lv_obj_set_width(key_text_, tw);
    lv_obj_set_pos(key_text_, inner + icon_w + space, (key_h_ - ui::line_height(Font::BODY_LARGE)) / 2);
    ui::set_text(key_text_, words);
    if (player_.empty()) lv_obj_add_flag(key_, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_remove_flag(key_, LV_OBJ_FLAG_HIDDEN);
    // The key's colour says it too: green while it plays, amber when Home Assistant was not reached.
    ui::set_fill(key_, now ? tessera::Tone::GOOD : sent_ == Sent::FAILED ? tessera::Tone::BUSY : tessera::Tone::NORMAL,
                 theme::ACCENT);
    ui::set_text(note_, player_.empty() ? plugin_->text("no_player") : "");
  }

  NewMusic *plugin_;
  Release release_;
  tessera::Text player_, state_, album_, playing_artist_;
  int width_ = 0, pad_ = 0, key_x_ = 0, key_y_ = 0, key_w_ = 0, key_h_ = 0, title_y_ = 0, title_lines_ = 2;
  int card_h_ = 0, side_ = 0;
  bool beside_ = false;
  Font title_face_ = Font::HEADLINE;
  Sent sent_ = Sent::NO;
  uint32_t sent_at_ = 0;
  int x0_ = 0;
  Vinyl vinyl_;
  lv_obj_t *cover_{}, *kind_{}, *title_{}, *artist_{}, *key_{}, *key_icon_{}, *key_text_{}, *note_{};
};

void NewMusic::setup() {
  add_tile("releases", [this]() { return new ReleasesTile(this); });
  add_card("release", [this]() { return new ReleaseCard(this); });
}

}  // namespace esphome::new_music
