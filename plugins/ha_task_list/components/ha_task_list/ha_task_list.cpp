#include "ha_task_list.h"
#include "esphome/core/log.h"
#include <cstring>

namespace esphome::ha_task_list {

static const char *const TAG = "ha_task_list";

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

    rows_[i].checkbox = tessera::ui::label(c.parent, tessera::Font::BODY, theme::MUTED);
    lv_obj_set_pos(rows_[i].checkbox, 6, y_pos);
    tessera::ui::set_text(rows_[i].checkbox, "");

    rows_[i].label = tessera::ui::label(c.parent, tessera::Font::BODY, theme::INK);
    lv_obj_set_pos(rows_[i].label, 32, y_pos);
    lv_obj_set_width(rows_[i].label, width_ - 38);
    lv_label_set_long_mode(rows_[i].label, LV_LABEL_LONG_MODE_DOTS);
    tessera::ui::set_text(rows_[i].label, "");
  }

  fetch_todo_items();
}

void TaskTile::toggle_next_item() {
  for (size_t i = 0; i < tasks_.size(); ++i) {
    if (!tasks_[i].completed) {
      tasks_[i].completed = true;
      update_rows();

      std::map<std::string, std::string> data;
      data["entity_id"] = entity_;
      data["item"] = tasks_[i].text;
      data["status"] = "completed";

      ESP_LOGI(TAG, "Toggling task '%s' to completed", tasks_[i].text.c_str());
      call_homeassistant_service("todo.update_item", data);
      return;
    }
  }

  fetch_todo_items();
}

void TaskTile::fetch_todo_items() {
  if (entity_.empty()) return;

  std::map<std::string, std::string> data;
  data["entity_id"] = entity_;
  call_homeassistant_service("todo.get_items", data);
}

void TaskTile::on_state(JsonObjectConst data) {
  tasks_.clear();

  JsonObjectConst entity_obj = data;
  if (data["entities"].is<JsonObjectConst>()) {
    JsonObjectConst entities = data["entities"].as<JsonObjectConst>();
    for (JsonPairConst kv : entities) {
      if (kv.value().is<JsonObjectConst>()) {
        entity_obj = kv.value().as<JsonObjectConst>();
        break;
      }
    }
  }

  JsonArrayConst items = data["items"].as<JsonArrayConst>();
  if (items.isNull() && entity_obj["items"].is<JsonArrayConst>()) {
    items = entity_obj["items"].as<JsonArrayConst>();
  }
  if (items.isNull() && entity_obj["attributes"].is<JsonObjectConst>()) {
    items = entity_obj["attributes"]["items"].as<JsonArrayConst>();
  }

  if (!items.isNull()) {
    for (JsonObjectConst item : items) {
      if (static_cast<int>(tasks_.size()) >= MAX_ROWS) break;

      TaskItem t;
      t.text = item["summary"] | item["text"] | "";
      const char *status = item["status"] | "";
      t.completed = (strcmp(status, "completed") == 0) || (item["completed"] | false);

      if (!t.text.empty()) {
        tasks_.push_back(t);
      }
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

void TaskTile::on_tap() {
  toggle_next_item();
}

void TaskTile::on_tick(uint32_t epoch) {}

void TaskTile::on_theme() {
  for (int i = 0; i < MAX_ROWS; ++i) {
    if (rows_[i].checkbox) tessera::ui::set_color(rows_[i].checkbox, theme::MUTED);
    if (rows_[i].label) tessera::ui::set_color(rows_[i].label, theme::INK);
  }
}

}  // namespace esphome::ha_task_list
