#include "ha_task_list.h"
#include <cstring>

namespace esphome::ha_task_list {

void HaTaskList::setup() {
  add_tile("tasks", [this]() { return new TaskTile(this); });
}

void TaskTile::create(const tessera::TileContext &c) {
  width_ = c.width;
  height_ = c.height;
  entity_ = c.entity;
  name_ = c.name;

  int row_h = height_ / MAX_ROWS;

  for (int i = 0; i < MAX_ROWS; ++i) {
    int y_pos = i * row_h + 4;

    rows_[i].checkbox = tessera::ui::label(c.parent, tessera::Font::BODY, theme::MUTED);
    lv_obj_set_pos(rows_[i].checkbox, 8, y_pos);
    tessera::ui::set_text(rows_[i].checkbox, "[ ]");

    rows_[i].label = tessera::ui::label(c.parent, tessera::Font::BODY, theme::INK);
    lv_obj_set_pos(rows_[i].label, 40, y_pos);
    lv_obj_set_width(rows_[i].label, width_ - 48);
    tessera::ui::set_text(rows_[i].label, "");
  }
}

void TaskTile::on_state(JsonObjectConst data) {
  tasks_.clear();
  JsonArrayConst items = data["items"].as<JsonArrayConst>();
  if (!items.isNull()) {
    for (const auto &item : items) {
      if (static_cast<int>(tasks_.size()) >= MAX_ROWS) break;
      TaskItem t;
      t.text = item["summary"] | item["text"] | "";
      const char *status = item["status"] | "";
      t.completed = (strcmp(status, "completed") == 0) || (item["completed"] | false);
      tasks_.push_back(t);
    }
  }
  update_rows();
}

void TaskTile::update_rows() {
  for (int i = 0; i < MAX_ROWS; ++i) {
    if (i < static_cast<int>(tasks_.size())) {
      tessera::ui::set_text(rows_[i].checkbox, tasks_[i].completed ? "[x]" : "[ ]");
      tessera::ui::set_text(rows_[i].label, tasks_[i].text.c_str());
    } else {
      tessera::ui::set_text(rows_[i].checkbox, "");
      tessera::ui::set_text(rows_[i].label, "");
    }
  }
}

void TaskTile::on_tick(uint32_t epoch) {}

void TaskTile::on_theme() {
  for (int i = 0; i < MAX_ROWS; ++i) {
    if (rows_[i].checkbox) tessera::ui::set_color(rows_[i].checkbox, theme::MUTED);
    if (rows_[i].label) tessera::ui::set_color(rows_[i].label, theme::INK);
  }
}

}  // namespace esphome::ha_task_list
