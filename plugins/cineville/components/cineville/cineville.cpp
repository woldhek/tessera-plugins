#include "cineville.h"
#include "esphome/core/log.h"
#include <algorithm>
#include <cstdio>
#include <cstring>

namespace esphome::cineville {

namespace ui = tessera::ui;
using tessera::Font;
using tessera::Tone;

static const char *const TAG = "cineville";

static constexpr uint32_t ICON_LEFT = 0xF0141, ICON_RIGHT = 0xF0142;

static void copy(char *to, size_t size, const char *from) { std::snprintf(to, size, "%s", from ? from : ""); }

// ---- The sensor's lists ----

// The Cineville integration's sensor has parallel lists: titles[i], highlights[i], next_start[i], ... are film i. A
// list Tessera had to shorten to fit is shortened with the others, so the titles say how many films came.
void FilmList::read(JsonObjectConst data) {
  const char *wait = data["wait"] | "";
  wrong = std::strcmp(wait, "wrong_entity") == 0;
  const char *state = data["state"] | "";
  unavailable = !std::strcmp(state, "unavailable") || !std::strcmp(state, "unknown");
  JsonObjectConst a = data["attributes"].as<JsonObjectConst>();
  const char *kind = a["cineville_list"] | "";
  if (!*state || a.isNull()) return;  // nothing chosen yet, or the entity has not come
  if (!*kind) {
    // Not a sensor of the Cineville integration, or one without its lists (unavailable).
    wrong = !unavailable;
    return;
  }
  soon = std::strcmp(kind, "soon") == 0;
  JsonArrayConst titles = a["titles"].as<JsonArrayConst>(), picks = a["highlights"].as<JsonArrayConst>(),
                 directors = a["directors"].as<JsonArrayConst>(), shows = a["shows"].as<JsonArrayConst>(),
                 today = a["shows_today"].as<JsonArrayConst>(), next = a["next_start"].as<JsonArrayConst>(),
                 venues = a["next_venue"].as<JsonArrayConst>(), premieres = a["premieres"].as<JsonArrayConst>();
  films.clear();
  const int n = std::min<int>(titles.size(), MAX_FILMS);
  for (int i = 0; i < n; ++i) {
    Film f;
    copy(f.title, sizeof(f.title), titles[i] | "");
    if (!f.title[0]) continue;
    copy(f.director, sizeof(f.director), directors[i] | "");
    copy(f.venue, sizeof(f.venue), venues[i] | "");
    const char *p = picks[i] | "";
    f.pick = !std::strcmp(p, "week") ? PICK_WEEK : !std::strcmp(p, "tip") ? PICK_TIP : 0;
    f.shows = static_cast<uint16_t>(std::max(0, std::min(65535, shows[i] | 0)));
    f.today = static_cast<uint16_t>(std::max(0, std::min(65535, today[i] | 0)));
    f.next = next[i] | 0u;
    f.premiere = premieres[i] | 0u;
    films.push_back(f);
  }
  loaded = true;
  wrong = false;
}

void FilmList::pick(const Filter &filter, tessera::List<uint8_t> &out) const {
  out.clear();
  const uint32_t now = tessera::epoch();
  for (int i = 0; i < static_cast<int>(films.size()); ++i) {
    const Film &f = films[i];
    if (filter.picks && !f.pick) continue;
    if (!soon) {
      // Now showing: only what is still to see. The sensor says the next showtime as it was when it last asked
      // Cineville, so a film whose next showtime has passed waits for the next answer.
      if (!f.next || (now && f.next < now)) continue;
      if (filter.today && now && tessera::days_from_today(f.next) != 0) continue;
    }
    out.push_back(static_cast<uint8_t>(i));
  }
}

// ---- Rows ----

static int pad() { return ui::px(ui::large() ? 7 : 4); }
static int bar_width() { return std::max(2, ui::px(ui::large() ? 4 : 3)); }
static int text_x() { return bar_width() + ui::px(ui::large() ? 10 : 6); }

// The lines of a row: the title, the director (on the card), and the pick with when and where.
int FilmRows::content() const {
  return ui::line_height(title_font_) + ((subs_ ? 1 : 0) + (director_ ? 1 : 0)) * ui::line_height(Font::BODY);
}

// No row takes a tap of its own (a tap on the tile opens the card, on the card only its foot pages), so a row is as
// high as its lines.
int FilmRows::row() const { return content() + 2 * pad(); }

void FilmRows::make(Cineville *plugin, lv_obj_t *parent, int width, int slots, Font title, bool subs, bool director) {
  plugin_ = plugin;
  width_ = width;
  title_font_ = title;
  subs_ = subs;
  director_ = director;
  count_ = std::max(0, std::min(slots, MAX_SLOTS));
  const int x = text_x();
  for (int i = 0; i < count_; ++i) {
    Slot &s = slots_[i];
    s.bar = ui::block(parent, theme::ACCENT);
    lv_obj_set_width(s.bar, bar_width());
    s.title = ui::label(parent, title, theme::INK);
    lv_obj_set_width(s.title, std::max(1, width - x));
    s.sub = ui::label(parent, Font::BODY, theme::MUTED);
    lv_obj_set_width(s.sub, std::max(1, width - x));
    for (auto *o : {s.bar, s.title, s.sub}) lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
    if (director_) {
      s.info = ui::label(parent, Font::BODY, theme::INK_SOFT);
      lv_obj_set_width(s.info, std::max(1, width - x));
      lv_obj_add_flag(s.info, LV_OBJ_FLAG_HIDDEN);
    }
  }
}

// When a showtime is, as briefly as the screen can say it: "19:15" today, "Tomorrow 19:15", "Sat 31 Oct 19:15".
static std::string when(uint32_t at) {
  const int32_t days = tessera::days_from_today(at);
  const std::string clock = tessera::clock_text(at);
  if (days == 0) return clock;
  if (days == 1) return tessera::days_text(1) + " " + clock;
  return tessera::date_text(at) + " " + clock;
}

std::string FilmRows::sub_text(const FilmList &list, const Film &f, bool pick) const {
  std::string out;
  auto add = [&out](const std::string &part) {
    if (part.empty()) return;
    if (!out.empty()) out += " · ";
    out += part;
  };
  if (pick && f.pick == PICK_WEEK) add(plugin_->text("pick_week"));
  else if (pick && f.pick == PICK_TIP) add(plugin_->text("pick_tip"));
  if (list.soon) {
    if (f.premiere) add(tessera::fill(plugin_->text("opens"), "date", tessera::date_text(f.premiere)));
    if (f.shows) add(tessera::format(plugin_->text("previews"), f.shows));
  } else if (f.next) {
    std::string next = when(f.next);
    if (f.venue[0]) next += std::string(" ") + f.venue;
    add(next);
  }
  return out;
}

void FilmRows::paint(Slot &s, const FilmList &list, const Film &f) {
  // The bar: Cineville's pick in the accent, any other film a quiet line.
  ui::set_fill(s.bar, f.pick ? Tone::ACCENT : Tone::NORMAL, f.pick ? theme::ACCENT : theme::TRACK);
  ui::set_text(s.title, f.title);
  ui::set_color(s.title, theme::INK);
  // The pick's words when they fit with when and where; on a narrow row the coloured bar and line say it alone.
  std::string sub = sub_text(list, f, true);
  if (f.pick && ui::text_width(sub, Font::BODY) > width_ - text_x()) {
    const std::string rest = sub_text(list, f, false);
    if (!rest.empty()) sub = rest;
  }
  ui::set_text(s.sub, sub);
  ui::set_tone(s.sub, f.pick ? Tone::ACCENT : Tone::NORMAL, theme::MUTED);
  if (s.info) {
    ui::set_text(s.info, f.director);
    ui::set_color(s.info, theme::INK_SOFT);
  }
}

int FilmRows::show(const FilmList &list, const tessera::List<uint8_t> &order, int first, int height, bool spread) {
  const int h0 = row(), lt = ui::line_height(title_font_), ls = ui::line_height(Font::BODY), lines = content();
  const int total = static_cast<int>(order.size()) - first;
  const int n = std::max(0, std::min({count_, total, std::max(1, height / std::max(1, h0))}));
  // Every row in use and more to come (a tall tile): spread the rows over the room instead of a gap under them.
  const int h = (spread && n == count_ && n > 0) ? std::max(h0, height / n) : h0;
  const int x = text_x();
  for (int i = 0; i < count_; ++i) {
    Slot &s = slots_[i];
    if (i >= n) {
      s.film = -1;
      for (auto *o : {s.bar, s.title, s.sub, s.info}) if (o) lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
      continue;
    }
    const int index = order[first + i];
    const Film &f = list.films[index];
    const int y = i * h, top = y + (h - lines) / 2;
    s.film = index;
    lv_obj_set_pos(s.bar, 0, top);
    lv_obj_set_height(s.bar, lines);
    lv_obj_set_pos(s.title, x, top);
    if (s.info) lv_obj_set_pos(s.info, x, top + lt);
    lv_obj_set_pos(s.sub, x, top + lt + (s.info ? ls : 0));
    paint(s, list, f);
    lv_obj_remove_flag(s.bar, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(s.title, LV_OBJ_FLAG_HIDDEN);
    if (s.info) lv_obj_remove_flag(s.info, LV_OBJ_FLAG_HIDDEN);
    if (subs_) lv_obj_remove_flag(s.sub, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(s.sub, LV_OBJ_FLAG_HIDDEN);
  }
  return n;
}

void FilmRows::repaint(const FilmList &list, const tessera::List<uint8_t> &order) {
  for (int i = 0; i < count_; ++i) {
    Slot &s = slots_[i];
    if (s.film >= 0 && s.film < static_cast<int>(list.films.size())) paint(s, list, list.films[s.film]);
  }
}

void FilmRows::hide() {
  for (int i = 0; i < count_; ++i) {
    slots_[i].film = -1;
    for (auto *o : {slots_[i].bar, slots_[i].title, slots_[i].sub, slots_[i].info})
      if (o) lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
  }
}

// The word for a list that shows nothing, or nullptr when it shows films.
static const char *empty_note(const tessera::Text &entity, const FilmList &list, const Filter &filter, bool any) {
  if (entity.empty()) return "choose_sensor";
  if (list.wrong) return "wrong_sensor";
  if (!list.loaded) return list.unavailable ? "unavailable" : "loading";
  if (any) return nullptr;
  if (filter.picks) return "no_picks";
  if (filter.today && !list.soon) return "none_today";
  return list.soon ? "none_soon" : "none";
}

// ---- The tile: the first films of the list; a tap opens the whole list on a card ----

void FilmTile::create(const tessera::TileContext &c) {
  width_ = c.width;
  height_ = c.height;
  tile_ = c.tile;
  entity_ = roomy::text(std::string(c.entity ? c.entity : ""));
  name_ = roomy::text(std::string(c.name ? c.name : ""));
  filter_.today = std::strcmp(c.options["period"] | "week", "today") == 0;
  filter_.picks = c.options["picks_only"] | false;

  const Font title = ui::large() ? Font::BODY_LARGE : Font::BODY;
  // A tile at least 3 cm wide with room for three rows of two lines (or a wide one) shows each film's second line
  // (the pick, when and where); a smaller tile shows the titles alone, and more of them.
  const int two = ui::line_height(title) + ui::line_height(Font::BODY) + 2 * pad();
  const bool subs = width_ >= ui::mm(30) && (height_ >= 3 * two || (height_ >= two && width_ >= 2 * height_));
  const int one = subs ? two : ui::line_height(title) + 2 * pad();
  rows_.make(plugin_, c.parent, width_, std::min(FilmRows::TILE_SLOTS, std::max(1, height_ / one)), title, subs, false);

  note_ = ui::text(c.parent, Font::BODY, theme::MUTED);
  lv_obj_set_size(note_, width_, std::min(height_, 2 * ui::line_height(Font::BODY)));
  lv_obj_set_style_text_align(note_, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(note_, LV_ALIGN_CENTER, 0, 0);
  more_ = ui::label(c.parent, Font::BODY, theme::ACCENT);
  lv_obj_set_pos(more_, text_x(), height_ - ui::line_height(Font::BODY));
  lv_obj_set_width(more_, std::max(1, width_ - text_x()));
  lv_obj_add_flag(more_, LV_OBJ_FLAG_HIDDEN);
  layout();
}

void FilmTile::on_state(JsonObjectConst data) {
  const char *name = data["name"] | "";
  if (*name) sensor_name_ = roomy::text(std::string(name));
  list_.read(data);
  if (list_.wrong) ESP_LOGW(TAG, "%s is not a sensor of the Cineville integration", entity_.c_str());
  layout();
}

void FilmTile::layout() {
  list_.pick(filter_, order_);
  const char *note = empty_note(entity_, list_, filter_, !order_.empty());
  if (note) {
    rows_.hide();
    lv_obj_add_flag(more_, LV_OBJ_FLAG_HIDDEN);
    ui::set_text(note_, plugin_->text(note));
    lv_obj_remove_flag(note_, LV_OBJ_FLAG_HIDDEN);
    return;
  }
  lv_obj_add_flag(note_, LV_OBJ_FLAG_HIDDEN);
  const int total = static_cast<int>(order_.size());
  const int fits = std::max(1, height_ / std::max(1, rows_.row()));
  if (total <= std::min(fits, FilmRows::TILE_SLOTS)) {
    rows_.show(list_, order_, 0, height_, false);
    lv_obj_add_flag(more_, LV_OBJ_FLAG_HIDDEN);
    return;
  }
  // Not all fit: room at the bottom for "5 more", which (as a tap anywhere) opens the whole list.
  const int room = height_ - ui::line_height(Font::BODY);
  const int shown = rows_.show(list_, order_, 0, room, true);
  if (shown == 0) {
    lv_obj_add_flag(more_, LV_OBJ_FLAG_HIDDEN);
    rows_.show(list_, order_, 0, height_, false);
    return;
  }
  ui::set_text(more_, tessera::format(plugin_->text("more"), total - shown));
  lv_obj_remove_flag(more_, LV_OBJ_FLAG_HIDDEN);
}

void FilmTile::on_tap() {
  if (entity_.empty() || !list_.loaded) return;
  plugin_->opening = filter_;
  const std::string title = roomy::plain(name_.empty() ? sensor_name_ : name_);
  tessera::open_card(plugin_->plugin_id(), "films", roomy::plain(entity_), tile_, title);
}

void FilmTile::on_tick(uint32_t epoch) {
  // Showtimes pass and days turn: once a minute the list drops what has gone, and "Tomorrow" becomes today.
  if (epoch % 60 == 0) layout();
}

void FilmTile::on_theme() {
  rows_.repaint(list_, order_);
  ui::set_color(note_, theme::MUTED);
  ui::set_color(more_, theme::ACCENT);
}

// ---- The card: the whole list, a page at a time, each film with its director ----

void FilmCard::open(const tessera::CardContext &c) {
  width_ = c.width;
  height_ = c.height;
  entity_ = roomy::text(std::string(c.entity ? c.entity : ""));
  filter_ = plugin_->opening;
  foot_ = std::max(ui::mm(10), ui::line_height(Font::BODY) + 2 * pad());
  const Font title = Font::BODY_LARGE;
  // Each film with its director on a line of its own, where the glass is large enough for it.
  const bool director = ui::large();
  const int lines = ui::line_height(title) + (director ? 2 : 1) * ui::line_height(Font::BODY);
  const int row = lines + 2 * pad();
  per_page_ = std::max(1, std::min(FilmRows::MAX_SLOTS, (height_ - foot_) / row));
  rows_.make(plugin_, c.parent, width_, per_page_, title, true, director);

  note_ = ui::text(c.parent, Font::BODY_LARGE, theme::MUTED);
  lv_obj_set_size(note_, width_, 2 * ui::line_height(Font::BODY_LARGE));
  lv_obj_set_style_text_align(note_, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(note_, LV_ALIGN_CENTER, 0, 0);

  // The foot: a line, then the previous page at the left, "2 / 3" in the middle, the next page at the right; each key
  // a third of the width, as wide as a finger needs.
  rule_ = ui::block(c.parent, theme::LINE);
  lv_obj_set_style_radius(rule_, 0, 0);
  lv_obj_set_size(rule_, width_, std::max(1, ui::px(1)));
  lv_obj_set_pos(rule_, 0, height_ - foot_);
  const int icon_h = ui::line_height(Font::ICON_SMALL), icon_y = height_ - foot_ + (foot_ - icon_h) / 2;
  prev_ = ui::label(c.parent, Font::ICON_SMALL, theme::ACCENT);
  ui::set_text(prev_, ui::icon(ICON_LEFT));
  lv_obj_set_pos(prev_, 0, icon_y);
  next_ = ui::label(c.parent, Font::ICON_SMALL, theme::ACCENT);
  ui::set_text(next_, ui::icon(ICON_RIGHT));
  lv_obj_set_pos(next_, width_ - ui::text_width(ui::icon(ICON_RIGHT), Font::ICON_SMALL), icon_y);
  page_ = ui::label(c.parent, Font::BODY, theme::MUTED);
  lv_obj_set_width(page_, width_ / 3);
  lv_obj_set_style_text_align(page_, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_pos(page_, width_ / 3, height_ - foot_ + (foot_ - ui::line_height(Font::BODY)) / 2);
  layout();
}

void FilmCard::on_state(JsonObjectConst data) {
  list_.read(data);
  layout();
}

void FilmCard::layout() {
  list_.pick(filter_, order_);
  for (auto *o : {rule_, prev_, next_, page_}) lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
  const char *note = empty_note(entity_, list_, filter_, !order_.empty());
  if (note) {
    rows_.hide();
    ui::set_text(note_, plugin_->text(note));
    lv_obj_remove_flag(note_, LV_OBJ_FLAG_HIDDEN);
    return;
  }
  lv_obj_add_flag(note_, LV_OBJ_FLAG_HIDDEN);
  const int total = static_cast<int>(order_.size());
  const int pages = std::max(1, (total + per_page_ - 1) / per_page_);
  page_index_ = std::max(0, std::min(page_index_, pages - 1));
  rows_.show(list_, order_, page_index_ * per_page_, height_ - foot_, false);
  if (pages > 1) {
    lv_obj_remove_flag(rule_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(page_, LV_OBJ_FLAG_HIDDEN);
    ui::set_text(page_, tessera::fill(tessera::fill(plugin_->text("page"), "page", std::to_string(page_index_ + 1)).c_str(),
                                      "pages", std::to_string(pages)));
    if (page_index_ > 0) lv_obj_remove_flag(prev_, LV_OBJ_FLAG_HIDDEN);
    if (page_index_ + 1 < pages) lv_obj_remove_flag(next_, LV_OBJ_FLAG_HIDDEN);
  }
}

void FilmCard::on_tap() {
  const auto p = tessera::tap_point();
  if (p.y < height_ - foot_) return;
  const int pages = std::max(1, (static_cast<int>(order_.size()) + per_page_ - 1) / per_page_);
  if (p.x < width_ / 3 && page_index_ > 0) --page_index_;
  else if (p.x >= width_ * 2 / 3 && page_index_ + 1 < pages) ++page_index_;
  else return;
  layout();
}

void FilmCard::on_tick(uint32_t epoch) {
  if (epoch % 60 == 0) layout();
}

void FilmCard::on_theme() {
  rows_.repaint(list_, order_);
  ui::set_color(note_, theme::MUTED);
  ui::set_color(prev_, theme::ACCENT);
  ui::set_color(next_, theme::ACCENT);
  ui::set_color(page_, theme::MUTED);
  ui::set_fill(rule_, Tone::NORMAL, theme::LINE);
}

// ---- The plugin ----

void Cineville::setup() {
  add_tile("films", [this]() { return new FilmTile(this); });
  add_card("films", [this]() { return new FilmCard(this); });
}

}  // namespace esphome::cineville
