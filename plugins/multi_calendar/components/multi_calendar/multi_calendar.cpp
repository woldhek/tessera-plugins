#include "multi_calendar.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>

namespace esphome::multi_calendar {

namespace ui = tessera::ui;
using tessera::Font;
using tessera::Tone;

// Ask the calendars again this often while the tile is on the glass; a change of the tile's own calendar asks at once.
static constexpr uint32_t TILE_EVERY = 300;
// A question that has not been answered after this long no longer holds up the next one.
static constexpr uint32_t ANSWER_WAIT = 10;

// ---- Days and moments ----

// Days since 1970-01-01 of a day of the civil calendar (Howard Hinnant's days_from_civil).
static int32_t days_from_civil(int y, int m, int d) {
  y -= m <= 2;
  const int era = (y >= 0 ? y : y - 399) / 400;
  const int yoe = y - era * 400;
  const int doy = (153 * (m > 2 ? m - 3 : m + 9) + 2) / 5 + d - 1;
  const int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + doe - 719468;
}

// Today on the screen's calendar and the minute of the day it is now; INT32_MIN while the clock is not set.
static void now_local(int32_t &today, int &minute) {
  today = INT32_MIN;
  minute = 0;
  const uint32_t now = tessera::epoch();
  if (now == 0) return;
  const auto t = tessera::local_time(now);
  today = days_from_civil(t.year, t.month, t.day);
  minute = t.hour * 60 + t.minute;
}

// The seconds since 1970 of the moment `days` days from now, for the day words of the screen.
static uint32_t in_days(int days) {
  return static_cast<uint32_t>(static_cast<int64_t>(tessera::epoch()) + static_cast<int64_t>(days) * 86400);
}

// `count` digits of `text` as a number, -1 when one is not a digit.
static int digits(const char *text, int count) {
  int value = 0;
  for (int i = 0; i < count; ++i) {
    if (text[i] < '0' || text[i] > '9') return -1;
    value = value * 10 + (text[i] - '0');
  }
  return value;
}

struct Moment {
  int32_t day{INT32_MIN};
  int16_t minute{-1};  // -1: a date without a time of day
  bool ok{false};
};

// "2026-10-30" (an all-day event) or "2026-10-30T14:00:00+01:00" / "2026-10-30 14:00:00": the day and the minute of the
// day as Home Assistant wrote them, in its own time zone, which is the screen's.
static Moment parse(const char *text) {
  Moment m;
  if (!text || std::strlen(text) < 10 || text[4] != '-' || text[7] != '-') return m;
  const int y = digits(text, 4), mo = digits(text + 5, 2), d = digits(text + 8, 2);
  if (y < 0 || mo < 1 || mo > 12 || d < 1 || d > 31) return m;
  m.day = days_from_civil(y, mo, d);
  m.ok = true;
  if (std::strlen(text) >= 16 && (text[10] == 'T' || text[10] == ' ') && text[13] == ':') {
    const int h = digits(text + 11, 2), mi = digits(text + 14, 2);
    if (h >= 0 && h < 24 && mi >= 0 && mi < 60) m.minute = static_cast<int16_t>(h * 60 + mi);
  }
  return m;
}

// Whether an appointment is over at `today`, `now_min`. An all-day event ends on the day after its last day.
static bool ended(const Event &e, int32_t today, int now_min) {
  if (e.start_min < 0) return e.end_day <= today;
  if (e.end_day > today) return false;
  if (e.end_day < today) return true;
  return e.end_min <= now_min;
}

// The day an appointment shows on: its own, or today when it began earlier and is still going.
static int32_t shown_on(const Event &e, int32_t today) { return std::max(e.start_day, today); }

// The colour of a calendar: the four colours of the Tessera mark, the same in light and dark.
static theme::Role role_of(int calendar) {
  switch (calendar) {
    case 0: return theme::MARK_BLUE;
    case 1: return theme::MARK_GREEN;
    case 2: return theme::MARK_AMBER;
    default: return theme::MARK_PURPLE;
  }
}

static int dot_size() { return ui::px(ui::large() ? 10 : 7); }
static int gap() { return ui::px(ui::large() ? 8 : 5); }

// ---- The tile ----

AgendaTile::AgendaTile(MultiCalendar *plugin) : plugin_(plugin) { plugin_->add(this); }
AgendaTile::~AgendaTile() { plugin_->remove(this); }

// A calendar of the tile: its entity id as typed or chosen, once, at most MAX_CALENDARS.
void AgendaTile::add_calendar(const char *id) {
  std::string text = id ? id : "";
  const auto first = text.find_first_not_of(" \t");
  if (first == std::string::npos) return;
  text = text.substr(first, text.find_last_not_of(" \t") - first + 1);
  if (count_ >= MAX_CALENDARS || text.rfind("calendar.", 0) != 0) return;
  for (int i = 0; i < count_; ++i)
    if (roomy::plain(calendar_[i]) == text) return;
  calendar_[count_++] = roomy::text(text);
}

void AgendaTile::create(const tessera::TileContext &c) {
  width_ = c.width;
  height_ = c.height;
  days_ahead_ = std::max(1, std::min(7, static_cast<int>(c.options["days_ahead"] | 3)));

  // The tile's own calendar first, then the extra ones of the inspector.
  add_calendar(c.entity);
  for (JsonVariantConst more : c.options["more_calendars"].as<JsonArrayConst>()) add_calendar(more.as<const char *>());

  title_font_ = ui::large() ? Font::BODY_LARGE : Font::BODY;
  head_font_ = ui::large() ? Font::TITLE : Font::BODY_LARGE;
  head_ = ui::line_height(head_font_) + ui::px(ui::large() ? 8 : 4);
  row_ = ui::line_height(title_font_) + ui::px(ui::large() ? 10 : 6);
  slot_count_ = std::max(1, std::min(MAX_ROWS, (height_ - head_) / std::max(1, row_)));

  // The time column holds "09:00", "All day" or "Now", at most two fifths of the tile.
  int widest = ui::text_width(std::string("00:00"), Font::BODY);
  for (const char *key : {"all_day", "ongoing"})
    widest = std::max(widest, ui::text_width(std::string(plugin_->text(key)), Font::BODY));
  time_w_ = std::min(width_ * 2 / 5, widest + ui::px(2));

  day_ = ui::label(c.parent, head_font_, theme::INK);
  lv_obj_set_pos(day_, 0, 0);
  date_ = ui::label(c.parent, Font::BODY, theme::MUTED);
  lv_obj_set_pos(date_, 0, std::max(0, ui::line_height(head_font_) - ui::line_height(Font::BODY)));

  for (int i = 0; i < slot_count_; ++i) {
    Slot &s = slots_[i];
    s.dot = ui::block(c.parent, theme::MUTED);
    lv_obj_set_size(s.dot, dot_size(), dot_size());
    lv_obj_set_style_radius(s.dot, LV_RADIUS_CIRCLE, 0);
    s.time = ui::label(c.parent, Font::BODY, theme::MUTED);
    s.title = ui::label(c.parent, title_font_, theme::INK);
  }

  note_ = ui::text(c.parent, Font::BODY, theme::MUTED);
  lv_obj_set_size(note_, width_, std::min(height_, 2 * ui::line_height(Font::BODY)));
  lv_obj_set_style_text_align(note_, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(note_, LV_ALIGN_CENTER, 0, 0);
  more_ = ui::label(c.parent, Font::BODY, theme::MUTED);
  lv_obj_set_width(more_, std::max(1, width_ - dot_size() - gap()));

  hide_rows();
  ask();
  layout();
}

// One question per calendar: its events of the next `days_ahead_` days. The answers come in on_message, one by one.
void AgendaTile::ask() {
  const uint32_t now = tessera::epoch();
  for (int i = 0; i < count_; ++i) {
    if (asked_[i] && now - asked_at_ < ANSWER_WAIT) continue;  // one question at a time
    JsonDocument request;
    request["ask"] = "call_service:calendar.get_events";
    request["data"]["entity_id"] = roomy::plain(calendar_[i]);
    request["data"]["duration"]["days"] = days_ahead_;
    asked_[i] = tessera::send(plugin_, request.as<JsonObjectConst>());
  }
  asked_at_ = now;
}

// The events of an answer {"re", "ok", "result": {"calendar.x": {"events": [{"start", "end", "summary"}, ...]}}} that
// are not over yet, the first MAX_PER_CALENDAR of them.
void AgendaTile::read_events(JsonObjectConst message, int calendar) {
  int32_t today;
  int now_min;
  now_local(today, now_min);
  JsonObjectConst result = message["result"].as<JsonObjectConst>();
  JsonArrayConst items = result[roomy::plain(calendar_[calendar])]["events"].as<JsonArrayConst>();
  Events &list = events_[calendar];
  list.clear();
  for (JsonObjectConst item : items) {
    if (static_cast<int>(list.size()) >= MAX_PER_CALENDAR) break;
    const Moment start = parse(item["start"] | ""), end = parse(item["end"] | "");
    if (!start.ok) continue;
    Event e;
    std::snprintf(e.title, sizeof(e.title), "%s", item["summary"] | "");
    e.start_day = start.day;
    e.start_min = start.minute;
    if (end.ok) {
      e.end_day = end.day;
      e.end_min = end.minute;
    } else {
      e.end_day = start.minute < 0 ? start.day + 1 : start.day;
      e.end_min = start.minute;
    }
    if (today != INT32_MIN && ended(e, today, now_min)) continue;
    list.push_back(e);
  }
}

bool AgendaTile::answer(JsonObjectConst message) {
  const uint32_t re = message["re"] | 0u;
  if (!re) return false;
  for (int i = 0; i < count_; ++i) {
    if (asked_[i] != re) continue;
    asked_[i] = 0;
    failed_[i] = !(message["ok"] | false);
    if (!failed_[i]) {
      read_events(message, i);
      loaded_[i] = true;
    }
    layout();
    return true;
  }
  return false;
}

void AgendaTile::on_state(JsonObjectConst) {
  // The tile's own calendar changed (an appointment began or ended): ask again.
  ask();
}

void AgendaTile::on_tick(uint32_t epoch) {
  if (epoch > 0 && epoch % TILE_EVERY == 0) ask();
  // Appointments that are over go, and the day words follow the clock; set_text changes only what differs.
  if (epoch % 60 == 0) layout();
}

void AgendaTile::on_tap() { ask(); }

void AgendaTile::on_theme() {
  ui::set_color(day_, theme::INK);
  ui::set_color(date_, theme::MUTED);
  ui::set_color(note_, theme::MUTED);
  ui::set_color(more_, theme::MUTED);
  for (int i = 0; i < slot_count_; ++i) {
    ui::set_color(slots_[i].time, theme::MUTED);
    ui::set_color(slots_[i].title, theme::INK);
  }
  layout();
}

void AgendaTile::hide_rows() {
  for (auto *o : {day_, date_, more_})
    if (o) lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
  for (int i = 0; i < slot_count_; ++i)
    for (auto *o : {slots_[i].dot, slots_[i].time, slots_[i].title})
      if (o) lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
}

// "Today", "Tomorrow", or the day as the top bar writes it ("Fri 9 Oct").
std::string AgendaTile::day_label(int32_t day, int32_t today) const {
  const int days = day - today;
  if (days == 0) return plugin_->text("today");
  if (days == 1) return tessera::days_text(1);
  return tessera::date_text(in_days(days));
}

// Draw the coming appointments of the first day that has any: that day's name, then a row for each, in time order,
// all-day events first.
void AgendaTile::layout() {
  int32_t today;
  int now_min;
  now_local(today, now_min);

  bool any_loaded = false, any_failed = false;
  for (int i = 0; i < count_; ++i) {
    any_loaded = any_loaded || loaded_[i];
    any_failed = any_failed || failed_[i];
  }

  const char *note = nullptr;
  if (count_ == 0) note = "choose_calendar";
  else if (!any_loaded) note = any_failed ? "failed" : "loading";
  else if (today == INT32_MIN) note = "loading";

  // The first day with an appointment that is not over, from today on.
  int32_t day = INT32_MAX;
  if (!note) {
    for (int i = 0; i < count_; ++i)
      for (int j = 0; j < static_cast<int>(events_[i].size()); ++j)
        if (!ended(events_[i][j], today, now_min)) day = std::min(day, shown_on(events_[i][j], today));
    if (day == INT32_MAX) note = "nothing";
  }
  if (note) {
    hide_rows();
    ui::set_text(note_, plugin_->text(note));
    lv_obj_remove_flag(note_, LV_OBJ_FLAG_HIDDEN);
    return;
  }
  lv_obj_add_flag(note_, LV_OBJ_FLAG_HIDDEN);

  // That day's appointments, in time order: all-day events first, then those already going, then by start.
  struct Ref {
    uint8_t calendar, index;
    int16_t key;
  };
  Ref refs[MAX_CALENDARS * MAX_PER_CALENDAR];
  int n = 0;
  for (int i = 0; i < count_; ++i) {
    for (int j = 0; j < static_cast<int>(events_[i].size()); ++j) {
      const Event &e = events_[i][j];
      if (ended(e, today, now_min) || shown_on(e, today) != day) continue;
      const int16_t key = e.start_min < 0 ? -2 : (e.start_day < today ? -1 : e.start_min);
      Ref r{static_cast<uint8_t>(i), static_cast<uint8_t>(j), key};
      int k = n++;
      while (k > 0 && (refs[k - 1].key > r.key || (refs[k - 1].key == r.key && refs[k - 1].calendar > r.calendar))) {
        refs[k] = refs[k - 1];
        --k;
      }
      refs[k] = r;
    }
  }

  // The head: the day's name, and for today and tomorrow its date at the right.
  const int days = day - today;
  const std::string date = days == 0 || days == 1 ? std::string(tessera::date_text(in_days(days))) : std::string();
  const int date_w = date.empty() ? 0 : ui::text_width(date, Font::BODY) + ui::px(2);
  ui::set_text(day_, day_label(day, today));
  lv_obj_set_width(day_, std::max(1, width_ - date_w - (date_w ? gap() : 0)));
  lv_obj_remove_flag(day_, LV_OBJ_FLAG_HIDDEN);
  if (date_w) {
    ui::set_text(date_, date);
    lv_obj_set_width(date_, date_w);
    lv_obj_set_pos(date_, width_ - date_w, std::max(0, ui::line_height(head_font_) - ui::line_height(Font::BODY)));
    lv_obj_remove_flag(date_, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_add_flag(date_, LV_OBJ_FLAG_HIDDEN);
  }

  // The rows: as many as fit; when there are more, the last place says how many are left.
  const int shown = n > slot_count_ ? (slot_count_ >= 2 ? slot_count_ - 1 : slot_count_) : n;
  const int dot = dot_size(), space = gap();
  const int title_x = dot + space + time_w_ + space;
  for (int i = 0; i < slot_count_; ++i) {
    Slot &s = slots_[i];
    if (i >= shown) {
      for (auto *o : {s.dot, s.time, s.title}) lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
      continue;
    }
    const Event &e = events_[refs[i].calendar][refs[i].index];
    const int y = head_ + i * row_;
    lv_obj_set_pos(s.dot, 0, y + (row_ - dot) / 2);
    ui::set_fill(s.dot, Tone::NORMAL, role_of(refs[i].calendar));
    lv_obj_set_pos(s.time, dot + space, y + (row_ - ui::line_height(Font::BODY)) / 2);
    lv_obj_set_width(s.time, std::max(1, time_w_));
    std::string when;
    if (e.start_min < 0) {
      when = plugin_->text("all_day");
    } else if (e.start_day < today) {
      when = plugin_->text("ongoing");
    } else {
      char buffer[8];
      std::snprintf(buffer, sizeof(buffer), "%02d:%02d", e.start_min / 60, e.start_min % 60);
      when = buffer;
    }
    ui::set_text(s.time, when);
    lv_obj_set_pos(s.title, title_x, y + (row_ - ui::line_height(title_font_)) / 2);
    lv_obj_set_width(s.title, std::max(1, width_ - title_x));
    ui::set_text(s.title, e.title);
    for (auto *o : {s.dot, s.time, s.title}) lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
  }
  if (n > shown) {
    lv_obj_set_pos(more_, dot + space, head_ + shown * row_ + (row_ - ui::line_height(Font::BODY)) / 2);
    ui::set_text(more_, tessera::format(plugin_->text("more"), n - shown));
    lv_obj_remove_flag(more_, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_add_flag(more_, LV_OBJ_FLAG_HIDDEN);
  }
}

// ---- The plugin ----

void MultiCalendar::setup() {
  // The id is the tile's id in the manifest; the core makes an AgendaTile for every card that shows one.
  add_tile("agenda", [this]() { return new AgendaTile(this); });
}

void MultiCalendar::add(AgendaTile *tile) { tiles_.push_back(tile); }

void MultiCalendar::remove(AgendaTile *tile) {
  tiles_.erase(std::remove(tiles_.begin(), tiles_.end(), tile), tiles_.end());
}

void MultiCalendar::on_message(JsonObjectConst message) {
  for (auto *tile : tiles_)
    if (tile->answer(message)) return;
}

}  // namespace esphome::multi_calendar
