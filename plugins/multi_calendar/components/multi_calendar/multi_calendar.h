#pragma once

#include "esphome/core/component.h"
#include "esphome/components/smart_display/plugin_api.h"

namespace esphome::multi_calendar {

class MultiCalendar;

// A tile combines at most this many calendars: its own entity and up to three more, one colour each.
static constexpr int MAX_CALENDARS = 4;
// The rows a tile makes at most (what the manifest's memory counts: three parts a row).
static constexpr int MAX_ROWS = 8;
// The appointments kept per calendar. The app's answer holds about 25 to 30 events of one calendar (3 KB); only the
// first of the coming days are needed.
static constexpr int MAX_PER_CALENDAR = 12;

// One appointment, as calendar.get_events gives it. Fixed fields, so a whole list is one block in PSRAM. The app cuts
// every text of an answer to 48 bytes.
struct Event {
  char title[49]{};
  int32_t start_day{INT32_MIN};  // days since 1970-01-01 of the day it starts, in the calendar's own words
  int32_t end_day{INT32_MIN};    // the day it ends; for an all-day event the day after its last day
  int16_t start_min{-1};         // minutes since midnight when it starts, -1 for an all-day event
  int16_t end_min{-1};           // minutes since midnight on end_day when it ends
};
using Events = tessera::List<Event>;

// The tile: the coming appointments of one day, from several calendars, each with a dot in its own colour.
class AgendaTile : public tessera::Tile {
 public:
  explicit AgendaTile(MultiCalendar *plugin);
  ~AgendaTile() override;

  void create(const tessera::TileContext &c) override;
  void on_state(JsonObjectConst data) override;
  void on_tick(uint32_t epoch) override;
  void on_theme() override;
  void on_tap() override;

  // An answer of Home Assistant: true when it was to one of this tile's questions.
  bool answer(JsonObjectConst message);

 private:
  struct Slot {
    lv_obj_t *dot{}, *time{}, *title{};
  };

  void add_calendar(const char *id);
  void ask();
  void read_events(JsonObjectConst message, int calendar);
  void hide_rows();
  void layout();
  std::string day_label(int32_t day, int32_t today) const;

  MultiCalendar *plugin_{nullptr};
  tessera::Text calendar_[MAX_CALENDARS];
  Events events_[MAX_CALENDARS];
  bool loaded_[MAX_CALENDARS]{};  // an answer has come for it
  bool failed_[MAX_CALENDARS]{};  // the last question had no good answer
  uint32_t asked_[MAX_CALENDARS]{};
  uint32_t asked_at_{0};
  int count_{0}, days_ahead_{3};

  Slot slots_[MAX_ROWS];
  lv_obj_t *day_{nullptr}, *date_{nullptr}, *note_{nullptr}, *more_{nullptr};
  int width_{0}, height_{0}, head_{0}, row_{0}, slot_count_{0}, time_w_{0};
  tessera::Font title_font_{tessera::Font::BODY};
  tessera::Font head_font_{tessera::Font::TITLE};
};

// The plugin: an ESPHome component and a tessera::Plugin. It registers its tile type in setup() and hands the answers
// of Home Assistant to the tile that asked.
class MultiCalendar : public Component, public tessera::Plugin {
 public:
  void setup() override;
  float get_setup_priority() const override { return setup_priority::DATA; }
  void on_message(JsonObjectConst message) override;

  void add(AgendaTile *tile);
  void remove(AgendaTile *tile);

 private:
  tessera::List<AgendaTile *> tiles_;
};

}  // namespace esphome::multi_calendar
