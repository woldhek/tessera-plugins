#pragma once

#include "esphome/core/component.h"
#include "esphome/components/smart_display/plugin_api.h"

namespace esphome::ha_task_list {

class HaTaskList;

// One item of a to-do list, as todo.get_items gives it. Fixed fields, so a whole list is one block in PSRAM. The app
// cuts every text of an answer to 48 bytes, so these hold what can come.
struct TaskItem {
  char uid[49]{};
  char text[49]{};
  char note[49]{};                 // the item's description, "" for none
  int32_t due{INT32_MIN};          // the day it is due, as days since 1970-01-01; INT32_MIN for none
  bool completed{false};
};
using Tasks = tessera::List<TaskItem>;

// The most items a tile or the card keeps. The app's answer holds about 25 to 30 items of a list (3 KB); this is room
// to spare for short ones.
static constexpr int MAX_TASKS = 64;

// Rows of tasks, shared by the tile and the card: a round checkbox, the item's name, and under it when it has one the
// day it is due and its description. A row is one line high, or two with that second line.
class TaskRows {
 public:
  static constexpr int MAX_SLOTS = 16;  // the card's most; a tile makes at most TILE_SLOTS
  static constexpr int TILE_SLOTS = 12;

  // Make `slots` rows (at most MAX_SLOTS) in `parent`, `width` wide, in `title` font; `subs`: a task's second line.
  void make(HaTaskList *plugin, lv_obj_t *parent, int width, int slots, tessera::Font title, bool subs);
  // How many tasks from `first` on fit in `height` pixels, without drawing them.
  int fit(const Tasks &tasks, int first, int height) const;
  // Lay out the tasks from `first` on in `height` pixels from the top; returns how many it shows. Hides the rest.
  int show(const Tasks &tasks, int first, int height);
  // The index in `tasks` of the row under `y`, -1 for none.
  int task_at(int y) const;
  // Paint every row again (the screen went light or dark).
  void on_theme(const Tasks &tasks);
  // Hide every row.
  void hide();
  // The height of a row of one line.
  int one_line() const;

 private:
  struct Slot {
    lv_obj_t *box{}, *check{}, *title{}, *sub{}, *line{};
    int y{0}, h{0}, task{-1};
    int8_t done{-1};               // what the box shows now: -1 unknown, 0 open, 1 done
  };
  int height_of(const TaskItem &t) const;
  bool two_lines(const TaskItem &t) const;
  std::string sub_text(const TaskItem &t, tessera::Tone &tone) const;
  void paint(Slot &s, const TaskItem &t, bool force);

  HaTaskList *plugin_{nullptr};
  Slot slots_[MAX_SLOTS];
  int count_{0}, width_{0};
  tessera::Font title_font_{tessera::Font::BODY};
  bool subs_{true};
};

class TaskTile : public tessera::Tile {
 public:
  explicit TaskTile(HaTaskList *plugin);
  ~TaskTile() override;

  void create(const tessera::TileContext &c) override;
  void on_state(JsonObjectConst data) override;
  void on_tick(uint32_t epoch) override;
  void on_theme() override;
  void on_tap() override;

  void answer(JsonObjectConst message);
  uint32_t asked() const { return asked_; }

 private:
  void ask();
  void layout();

  HaTaskList *plugin_{nullptr};
  TaskRows rows_;
  Tasks tasks_;
  tessera::Text entity_, name_, list_name_;
  lv_obj_t *note_{nullptr}, *more_{nullptr};
  int width_{0}, height_{0}, tile_{-1};
  bool show_completed_{false}, loaded_{false}, failed_{false};
  uint32_t asked_{0}, asked_at_{0}, ask_at_{0};
};

class ListCard : public tessera::Card {
 public:
  explicit ListCard(HaTaskList *plugin);
  ~ListCard() override;

  void open(const tessera::CardContext &c) override;
  void on_state(JsonObjectConst data) override;
  void on_tick(uint32_t epoch) override;
  void on_theme() override;
  void on_tap() override;

  void answer(JsonObjectConst message);
  uint32_t asked() const { return asked_; }

 private:
  void ask();
  void layout();
  void paginate();

  HaTaskList *plugin_{nullptr};
  TaskRows rows_;
  Tasks tasks_;
  tessera::Text entity_;
  tessera::List<uint8_t> starts_;  // the first task of each page
  lv_obj_t *note_{nullptr}, *prev_{nullptr}, *next_{nullptr}, *page_{nullptr}, *rule_{nullptr};
  int width_{0}, height_{0}, foot_{0}, page_index_{0};
  bool loaded_{false}, failed_{false};
  uint32_t asked_{0}, asked_at_{0}, ask_at_{0};
};

class HaTaskList : public Component, public tessera::Plugin {
 public:
  void setup() override;
  float get_setup_priority() const override { return setup_priority::DATA; }
  void on_message(JsonObjectConst message) override;

  void add(TaskTile *tile);
  void remove(TaskTile *tile);
  ListCard *card_{nullptr};  // the card that is open, to hand it its answer

 private:
  tessera::List<TaskTile *> tiles_;
};

}  // namespace esphome::ha_task_list
