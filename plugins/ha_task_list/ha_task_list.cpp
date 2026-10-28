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
    int y_pos = i * row_h + (row_h - 20) / 2;

    // Checkbox: fixed clip mode to prevent vertical line wraps
    rows_[i].checkbox = tessera::ui::label(c.parent, tessera::Font::BODY, theme::MUTED);
    lv_obj_set_pos(rows_[i].checkbox, 6, y_pos);
    lv_label_set_long_mode(rows_[i].checkbox, LV_LABEL_LONG_MODE_CLIP);
    tessera::ui::set_text(rows_[i].checkbox, "[ ]");

    // Task text: truncate with "..." on overflow instead of multi-line wrapping
    rows_[i].label = tessera::ui::label(c.parent, tessera::Font::BODY, theme::INK);
    lv_obj_set_pos(rows_[i].label, 32, y_pos);
    lv_obj_set_width(rows_[i].label, width_ - 38);
    lv_label_set_long_mode(rows_[i].label, LV_LABEL_LONG_MODE_DOTS);
    tessera::ui::set_text(rows_[i].label, "");
  }

  update_rows();
}

void TaskTile::on_state(JsonObjectConst data) {
  tasks_.clear();

  JsonArrayConst items = data["items"];
  if (!items.isNull()) {
    for (JsonObjectConst item : items) {
      if (static_cast<int>(tasks_.size()) >= MAX_ROWS) break;

      TaskItem t;
      const char *sum = item["summary"] | static_cast<const char *>(item["text"] | "");
      t.text = sum ? sum : "";

      const char *status = item["status"] | "";
      t.completed = (strcmp(status, "completed") == 0) || (item["completed"] | false);
      tasks_.push_back(t);
    }
  }

  update_rows();
}

void TaskTile::update_rows() {
  for (int i = 0; i < MAX_ROWS; ++i) {
    if (!rows_[i].checkbox || !rows_[i].label) continue;

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
