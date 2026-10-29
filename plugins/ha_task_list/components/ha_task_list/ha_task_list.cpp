#include "ha_task_list.h"
#include "esphome/core/log.h"
#include <algorithm>
#include <cstdio>
#include <cstring>

namespace esphome::ha_task_list {

namespace ui = tessera::ui;
using tessera::Font;
using tessera::Tone;

static const char *const TAG = "ha_task_list";

static constexpr uint32_t ICON_CHECK = 0xF012C, ICON_LEFT = 0xF0141, ICON_RIGHT = 0xF0142;
// Ask the list again this often while it is on the glass; the entity's own change asks at once.
static constexpr uint32_t TILE_EVERY = 60, CARD_EVERY = 30;
// An answer that has not come after this long no longer holds up the next question.
static constexpr uint32_t ANSWER_WAIT = 10;
// After a tap, ask again this soon: Home Assistant has the change by then, and a done item moves down or goes.
static constexpr uint32_t AFTER_TAP = 2;

// ---- Days ----

// Days since 1970-01-01 of a day of the civil calendar (Howard Hinnant's days_from_civil).
static int32_t days_from_civil(int y, int m, int d) {
  y -= m <= 2;
  const int era = (y >= 0 ? y : y - 399) / 400;
  const int yoe = y - era * 400;
  const int doy = (153 * (m > 2 ? m - 3 : m + 9) + 2) / 5 + d - 1;
  const int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + doe - 719468;
}

// Today on the screen's calendar, INT32_MIN while its clock is not set.
static int32_t today() {
  const uint32_t now = tessera::epoch();
  if (now == 0) return INT32_MIN;
  const auto t = tessera::local_time(now);
  return days_from_civil(t.year, t.month, t.day);
}

// The day of "2026-10-30" or "2026-10-30T14:00:00+01:00" (the day as Home Assistant wrote it), INT32_MIN for none.
static int32_t day_of(const char *text) {
  int y = 0, m = 0, d = 0;
  if (!text || std::sscanf(text, "%4d-%2d-%2d", &y, &m, &d) != 3 || m < 1 || m > 12 || d < 1 || d > 31)
    return INT32_MIN;
  return days_from_civil(y, m, d);
}

static void copy(char *to, size_t size, const char *from) {
  std::snprintf(to, size, "%s", from ? from : "");
}

// ---- The list: todo.get_items through the app, and todo.update_item ----

// Ask for the items of `entity`; with `completed` the done ones too (todo.get_items gives only open items without a
// status). Returns the number its answer will carry, 0 when it could not be sent.
static uint32_t ask_items(HaTaskList *plugin, const tessera::Text &entity, bool completed) {
  if (entity.empty()) return 0;
  JsonDocument request;
  request["ask"] = "call_service:todo.get_items";
  request["data"]["entity_id"] = roomy::plain(entity);
  JsonArray status = request["data"]["status"].to<JsonArray>();
  status.add("needs_action");
  if (completed) status.add("completed");
  return tessera::send(plugin, request.as<JsonObjectConst>());
}

// The items of an answer {"re", "ok", "result": {"todo.x": {"items": [...]}}}: open ones first in the list's own
// order, then the done ones (only with `completed`). False when the answer says it failed.
static bool read_items(JsonObjectConst message, const tessera::Text &entity, bool completed, Tasks &out) {
  if (!(message["ok"] | false)) return false;
  JsonObjectConst result = message["result"].as<JsonObjectConst>();
  JsonArrayConst items = result[roomy::plain(entity)]["items"].as<JsonArrayConst>();
  if (items.isNull()) items = result["items"].as<JsonArrayConst>();
  out.clear();
  Tasks done;
  for (JsonObjectConst item : items) {
    const char *text = item["summary"] | "";
    if (!*text) continue;
    TaskItem t;
    copy(t.text, sizeof(t.text), text);
    copy(t.uid, sizeof(t.uid), item["uid"] | "");
    copy(t.note, sizeof(t.note), item["description"] | "");
    t.due = day_of(item["due"] | "");
    t.completed = std::strcmp(item["status"] | "", "completed") == 0;
    if (t.completed) {
      if (completed && static_cast<int>(done.size()) < MAX_TASKS) done.push_back(t);
    } else if (static_cast<int>(out.size()) < MAX_TASKS) {
      out.push_back(t);
    }
  }
  for (const auto &t : done) {
    if (static_cast<int>(out.size()) >= MAX_TASKS) break;
    out.push_back(t);
  }
  return true;
}

// Tick or untick a task: it shows at once, and goes back when the action could not be sent. Home Assistant's own
// answer comes with the next question (AFTER_TAP).
static void toggle(const tessera::Text &entity, TaskItem &t) {
  t.completed = !t.completed;
  JsonDocument fields;
  fields["item"] = t.uid[0] ? t.uid : t.text;
  fields["status"] = t.completed ? "completed" : "needs_action";
  if (!tessera::action("todo.update_item", roomy::plain(entity), fields.as<JsonVariantConst>())) {
    ESP_LOGW(TAG, "todo.update_item on %s was not sent", entity.c_str());
    t.completed = !t.completed;
  }
}

// ---- Rows ----

static int pad() { return ui::px(ui::large() ? 8 : 5); }
static int box_size() { return ui::px(ui::large() ? 22 : 16); }
static int text_x() { return box_size() + ui::px(ui::large() ? 12 : 8); }
static int sub_line() { return ui::line_height(Font::BODY); }

// A row is at least 7 mm high, the least a finger needs (the screen's own touch_min).
static int row_least() { return ui::mm(7); }
// The height of a row of one line in `title`, and of one with a second line.
static int row_height(Font title) { return std::max(row_least(), ui::line_height(title) + 2 * pad()); }
static int two_line_height(Font title) {
  return std::max(row_least(), ui::line_height(title) + sub_line() + 2 * pad());
}

int TaskRows::one_line() const { return row_height(title_font_); }

static bool has_sub(const TaskItem &t) { return t.due != INT32_MIN || t.note[0]; }

bool TaskRows::two_lines(const TaskItem &t) const { return subs_ && has_sub(t); }

int TaskRows::height_of(const TaskItem &t) const {
  return two_lines(t) ? two_line_height(title_font_) : one_line();
}

void TaskRows::make(HaTaskList *plugin, lv_obj_t *parent, int width, int slots, Font title, bool subs) {
  plugin_ = plugin;
  width_ = width;
  title_font_ = title;
  subs_ = subs;
  count_ = std::max(0, std::min(slots, MAX_SLOTS));
  const int d = box_size(), x = text_x();
  for (int i = 0; i < count_; ++i) {
    Slot &s = slots_[i];
    s.line = ui::block(parent, theme::LINE);
    lv_obj_set_style_radius(s.line, 0, 0);
    lv_obj_set_size(s.line, std::max(1, width - x), std::max(1, ui::px(1)));
    s.box = ui::block(parent, theme::ACCENT);
    lv_obj_set_size(s.box, d, d);
    lv_obj_set_style_radius(s.box, LV_RADIUS_CIRCLE, 0);
    lv_obj_add_flag(s.box, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
    s.check = ui::label(s.box, Font::ICON_SMALL, theme::ON_ACCENT);
    ui::set_text(s.check, ui::icon(ICON_CHECK));
    lv_obj_center(s.check);
    s.title = ui::label(parent, title, theme::INK);
    lv_obj_set_width(s.title, std::max(1, width - x));
    s.sub = ui::label(parent, Font::BODY, theme::MUTED);
    lv_obj_set_width(s.sub, std::max(1, width - x));
    for (auto *o : {s.line, s.box, s.title, s.sub}) lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
  }
}

int TaskRows::fit(const Tasks &tasks, int first, int height) const {
  int y = 0, n = 0;
  for (int i = first; i < static_cast<int>(tasks.size()) && n < count_; ++i) {
    const int h = height_of(tasks[i]);
    if (y + h > height && n > 0) break;
    y += h;
    ++n;
  }
  return n;
}

std::string TaskRows::sub_text(const TaskItem &t, Tone &tone) const {
  tone = Tone::NORMAL;
  std::string out;
  const int32_t now = today();
  if (t.due != INT32_MIN && now != INT32_MIN) {
    const int32_t days = t.due - now;
    if (days == 0) {
      out = plugin_->text("today");
      if (!t.completed) tone = Tone::BUSY;
    } else if (days < 0 && !t.completed) {
      out = plugin_->text("overdue");
      tone = Tone::ALERT;
    } else if (days > 0 && days < 7) {
      out = tessera::days_text(days);
    } else {
      out = tessera::date_text(static_cast<uint32_t>(static_cast<int64_t>(tessera::epoch()) + int64_t(days) * 86400));
    }
  }
  if (t.note[0]) {
    if (!out.empty()) out += " · ";
    out += t.note;
  }
  return out;
}

void TaskRows::paint(Slot &s, const TaskItem &t, bool force) {
  const int8_t done = t.completed ? 1 : 0;
  if (force || s.done != done) {
    if (t.completed) {
      lv_obj_set_style_bg_opa(s.box, LV_OPA_COVER, 0);
      lv_obj_set_style_border_width(s.box, 0, 0);
      lv_obj_remove_flag(s.check, LV_OBJ_FLAG_HIDDEN);
    } else {
      lv_obj_set_style_bg_opa(s.box, LV_OPA_TRANSP, 0);
      lv_obj_set_style_border_width(s.box, std::max(1, ui::px(2)), 0);
      lv_obj_set_style_border_color(s.box, ui::color(theme::MUTED), 0);
      lv_obj_add_flag(s.check, LV_OBJ_FLAG_HIDDEN);
    }
    s.done = done;
  }
  ui::set_fill(s.box, Tone::ACCENT, theme::ACCENT);
  ui::set_color(s.check, theme::ON_ACCENT);
  ui::set_text(s.title, t.text);
  ui::set_color(s.title, t.completed ? theme::MUTED : theme::INK);
  Tone tone;
  const std::string sub = sub_text(t, tone);
  ui::set_text(s.sub, sub);
  ui::set_tone(s.sub, tone, theme::MUTED);
  ui::set_fill(s.line, Tone::NORMAL, theme::LINE);
}

int TaskRows::show(const Tasks &tasks, int first, int height) {
  const int n = fit(tasks, first, height);
  const int d = box_size(), x = text_x(), lt = ui::line_height(title_font_);
  int y = 0;
  for (int i = 0; i < count_; ++i) {
    Slot &s = slots_[i];
    if (i >= n) {
      s.task = -1;
      for (auto *o : {s.line, s.box, s.title, s.sub}) lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
      continue;
    }
    const TaskItem &t = tasks[first + i];
    const int h = height_of(t);
    const bool two = two_lines(t);
    const int content = lt + (two ? sub_line() : 0);
    const int top = y + (h - content) / 2;
    s.y = y;
    s.h = h;
    s.task = first + i;
    lv_obj_set_pos(s.box, 0, top + (lt - d) / 2);
    lv_obj_set_pos(s.title, x, top);
    lv_obj_set_pos(s.sub, x, top + lt);
    lv_obj_set_pos(s.line, x, y + h - std::max(1, ui::px(1)));
    paint(s, t, false);
    lv_obj_remove_flag(s.box, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(s.title, LV_OBJ_FLAG_HIDDEN);
    if (two) lv_obj_remove_flag(s.sub, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(s.sub, LV_OBJ_FLAG_HIDDEN);
    // A line between rows, not under the last.
    if (i + 1 < n) lv_obj_remove_flag(s.line, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(s.line, LV_OBJ_FLAG_HIDDEN);
    y += h;
  }
  return n;
}

int TaskRows::task_at(int y) const {
  for (int i = 0; i < count_; ++i) {
    const Slot &s = slots_[i];
    if (s.task >= 0 && y >= s.y && y < s.y + s.h) return s.task;
  }
  return -1;
}

void TaskRows::on_theme(const Tasks &tasks) {
  for (int i = 0; i < count_; ++i) {
    Slot &s = slots_[i];
    if (s.task >= 0 && s.task < static_cast<int>(tasks.size())) paint(s, tasks[s.task], true);
  }
}

void TaskRows::hide() {
  for (int i = 0; i < count_; ++i) {
    slots_[i].task = -1;
    for (auto *o : {slots_[i].line, slots_[i].box, slots_[i].title, slots_[i].sub}) lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
  }
}

// ---- The tile: the first items of the list, a tap ticks one, a tap below them opens the whole list ----

TaskTile::TaskTile(HaTaskList *plugin) : plugin_(plugin) { plugin_->add(this); }
TaskTile::~TaskTile() { plugin_->remove(this); }

void TaskTile::create(const tessera::TileContext &c) {
  width_ = c.width;
  height_ = c.height;
  tile_ = c.tile;
  entity_ = roomy::text(std::string(c.entity ? c.entity : ""));
  name_ = roomy::text(std::string(c.name ? c.name : ""));
  show_completed_ = c.options["show_completed"] | false;

  const Font title = ui::large() ? Font::BODY_LARGE : Font::BODY;
  // As many rows of one line as fit, up to TILE_SLOTS: what the manifest's memory counts.
  const int slots = std::min(TaskRows::TILE_SLOTS, height_ / std::max(1, row_height(title)) + 1);
  // A short tile keeps every task to one line, so it shows more of them; from three rows of two lines, the second
  // line (when it is due, its description) shows.
  rows_.make(plugin_, c.parent, width_, slots, title, height_ >= 3 * two_line_height(title));

  note_ = ui::text(c.parent, Font::BODY, theme::MUTED);
  lv_obj_set_size(note_, width_, std::min(height_, 2 * ui::line_height(Font::BODY)));
  lv_obj_set_style_text_align(note_, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(note_, LV_ALIGN_CENTER, 0, 0);
  more_ = ui::label(c.parent, Font::BODY, theme::ACCENT);
  lv_obj_set_pos(more_, text_x(), height_ - ui::line_height(Font::BODY));
  lv_obj_set_width(more_, std::max(1, width_ - text_x()));
  lv_obj_add_flag(more_, LV_OBJ_FLAG_HIDDEN);

  ask();
  layout();
}

void TaskTile::ask() {
  const uint32_t now = tessera::epoch();
  if (asked_ && now - asked_at_ < ANSWER_WAIT) return;  // one question at a time
  asked_ = ask_items(plugin_, entity_, show_completed_);
  asked_at_ = now;
  ask_at_ = 0;
}

void TaskTile::on_state(JsonObjectConst data) {
  // The list's own entity changed (its state counts the open items): ask for the items again.
  const char *name = data["name"] | "";
  if (*name) list_name_ = roomy::text(std::string(name));
  ask();
}

void TaskTile::answer(JsonObjectConst message) {
  asked_ = 0;
  failed_ = !read_items(message, entity_, show_completed_, tasks_);
  if (failed_) ESP_LOGW(TAG, "todo.get_items for %s: %s", entity_.c_str(), message["error"] | "failed");
  loaded_ = loaded_ || !failed_;
  layout();
}

void TaskTile::layout() {
  const char *note = nullptr;
  if (entity_.empty()) note = "choose_list";
  else if (!loaded_) note = failed_ ? "failed" : "loading";
  else if (tasks_.empty()) note = "no_tasks";
  if (note) {
    rows_.hide();
    lv_obj_add_flag(more_, LV_OBJ_FLAG_HIDDEN);
    ui::set_text(note_, plugin_->text(note));
    lv_obj_remove_flag(note_, LV_OBJ_FLAG_HIDDEN);
    return;
  }
  lv_obj_add_flag(note_, LV_OBJ_FLAG_HIDDEN);
  const int total = static_cast<int>(tasks_.size());
  if (rows_.fit(tasks_, 0, height_) >= total) {
    rows_.show(tasks_, 0, height_);
    lv_obj_add_flag(more_, LV_OBJ_FLAG_HIDDEN);
    return;
  }
  // Not all fit: room at the bottom for "3 more", which opens the whole list.
  const int shown = rows_.show(tasks_, 0, height_ - ui::line_height(Font::BODY));
  ui::set_text(more_, tessera::format(plugin_->text("more"), total - shown));
  lv_obj_remove_flag(more_, LV_OBJ_FLAG_HIDDEN);
}

void TaskTile::on_tap() {
  const auto p = tessera::tap_point();
  const int index = p.y >= 0 ? rows_.task_at(p.y) : -1;
  if (index >= 0 && index < static_cast<int>(tasks_.size())) {
    toggle(entity_, tasks_[index]);
    layout();
    ask_at_ = tessera::epoch() + AFTER_TAP;
    return;
  }
  if (entity_.empty()) return;
  // Anywhere else: the whole list on a card, under the tile's name or else the list's own.
  const std::string title = roomy::plain(name_.empty() ? list_name_ : name_);
  tessera::open_card(plugin_->plugin_id(), "list", roomy::plain(entity_), tile_, title);
}

void TaskTile::on_tick(uint32_t epoch) {
  if ((ask_at_ && epoch >= ask_at_) || (epoch > 0 && epoch % TILE_EVERY == 0)) ask();
  // The day words ("Today", "Tomorrow") follow the clock; set_text changes only what differs.
  if (epoch % 60 == 0 && loaded_) layout();
}

void TaskTile::on_theme() {
  rows_.on_theme(tasks_);
  ui::set_color(note_, theme::MUTED);
  ui::set_color(more_, theme::ACCENT);
}

// ---- The card: the whole list, a page at a time, with keys to the next and the previous page ----

ListCard::ListCard(HaTaskList *plugin) : plugin_(plugin) {}
ListCard::~ListCard() {
  if (plugin_->card_ == this) plugin_->card_ = nullptr;
}

void ListCard::open(const tessera::CardContext &c) {
  width_ = c.width;
  height_ = c.height;
  entity_ = roomy::text(std::string(c.entity ? c.entity : ""));
  plugin_->card_ = this;

  const Font title = Font::BODY_LARGE;
  foot_ = std::max(ui::mm(10), ui::line_height(Font::BODY) + 2 * pad());
  const int slots = (height_ - foot_) / std::max(1, row_height(title)) + 1;
  rows_.make(plugin_, c.parent, width_, slots, title, true);

  note_ = ui::text(c.parent, Font::BODY_LARGE, theme::MUTED);
  lv_obj_set_size(note_, width_, 2 * ui::line_height(Font::BODY_LARGE));
  lv_obj_set_style_text_align(note_, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(note_, LV_ALIGN_CENTER, 0, 0);

  // The foot: a line, then the previous page at the left, "2 / 3" in the middle, the next page at the right. Each key
  // is a third of the width, as wide as a finger needs.
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

  ask();
  layout();
}

void ListCard::ask() {
  const uint32_t now = tessera::epoch();
  if (asked_ && now - asked_at_ < ANSWER_WAIT) return;
  asked_ = ask_items(plugin_, entity_, true);
  asked_at_ = now;
  ask_at_ = 0;
}

void ListCard::on_state(JsonObjectConst data) { ask(); }

void ListCard::answer(JsonObjectConst message) {
  asked_ = 0;
  failed_ = !read_items(message, entity_, true, tasks_);
  loaded_ = loaded_ || !failed_;
  layout();
}

void ListCard::paginate() {
  starts_.clear();
  const int room = height_ - foot_, total = static_cast<int>(tasks_.size());
  for (int first = 0; first < total && starts_.size() < 255;) {
    starts_.push_back(static_cast<uint8_t>(first));
    first += std::max(1, rows_.fit(tasks_, first, room));
  }
}

void ListCard::layout() {
  const char *note = nullptr;
  if (entity_.empty() || !loaded_) note = failed_ ? "failed" : "loading";
  else if (tasks_.empty()) note = "no_tasks";
  for (auto *o : {rule_, prev_, next_, page_}) lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
  if (note) {
    rows_.hide();
    ui::set_text(note_, plugin_->text(note));
    lv_obj_remove_flag(note_, LV_OBJ_FLAG_HIDDEN);
    return;
  }
  lv_obj_add_flag(note_, LV_OBJ_FLAG_HIDDEN);
  paginate();
  const int pages = static_cast<int>(starts_.size());
  page_index_ = std::max(0, std::min(page_index_, pages - 1));
  rows_.show(tasks_, starts_[page_index_], height_ - foot_);
  if (pages > 1) {
    lv_obj_remove_flag(rule_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(page_, LV_OBJ_FLAG_HIDDEN);
    ui::set_text(page_, tessera::fill(tessera::fill(plugin_->text("page"), "page", std::to_string(page_index_ + 1)).c_str(),
                                      "pages", std::to_string(pages)));
    if (page_index_ > 0) lv_obj_remove_flag(prev_, LV_OBJ_FLAG_HIDDEN);
    if (page_index_ + 1 < pages) lv_obj_remove_flag(next_, LV_OBJ_FLAG_HIDDEN);
  }
}

void ListCard::on_tap() {
  const auto p = tessera::tap_point();
  if (p.y < 0) return;
  if (p.y >= height_ - foot_) {
    const int pages = static_cast<int>(starts_.size());
    if (p.x < width_ / 3 && page_index_ > 0) --page_index_;
    else if (p.x >= width_ * 2 / 3 && page_index_ + 1 < pages) ++page_index_;
    else return;
    layout();
    return;
  }
  const int index = rows_.task_at(p.y);
  if (index < 0 || index >= static_cast<int>(tasks_.size())) return;
  toggle(entity_, tasks_[index]);
  rows_.show(tasks_, starts_.empty() ? 0 : starts_[page_index_], height_ - foot_);  // in place: nothing moves yet
  ask_at_ = tessera::epoch() + AFTER_TAP;
}

void ListCard::on_tick(uint32_t epoch) {
  if ((ask_at_ && epoch >= ask_at_) || (epoch > 0 && epoch % CARD_EVERY == 0)) ask();
}

void ListCard::on_theme() {
  rows_.on_theme(tasks_);
  ui::set_color(note_, theme::MUTED);
  ui::set_color(prev_, theme::ACCENT);
  ui::set_color(next_, theme::ACCENT);
  ui::set_color(page_, theme::MUTED);
  ui::set_fill(rule_, Tone::NORMAL, theme::LINE);
}

// ---- The plugin ----

void HaTaskList::setup() {
  add_tile("tasks", [this]() { return new TaskTile(this); });
  add_card("list", [this]() { return new ListCard(this); });
}

void HaTaskList::add(TaskTile *tile) { tiles_.push_back(tile); }

void HaTaskList::remove(TaskTile *tile) {
  tiles_.erase(std::remove(tiles_.begin(), tiles_.end(), tile), tiles_.end());
}

void HaTaskList::on_message(JsonObjectConst message) {
  const uint32_t re = message["re"] | 0u;
  if (!re) return;
  if (card_ && card_->asked() == re) {
    card_->answer(message);
    return;
  }
  for (auto *tile : tiles_) {
    if (tile->asked() == re) {
      tile->answer(message);
      return;
    }
  }
}

}  // namespace esphome::ha_task_list
