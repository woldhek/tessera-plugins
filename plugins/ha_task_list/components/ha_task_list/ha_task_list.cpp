#include "ha_task_list.h"
#include "esphome/core/log.h"
#include "esphome/components/network/util.h"
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
    int y_pos = i * row_h;

    // Klikbare container voor de hele rij
    rows_[i].container = lv_obj_create(c.parent);
    lv_obj_set_pos(rows_[i].container, 0, y_pos);
    lv_obj_set_size(rows_[i].container, width_, row_h);
    lv_obj_set_style_bg_opa(rows_[i].container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(rows_[i].container, 0, 0);
    lv_obj_clear_flag(rows_[i].container, LV_OBJ_FLAG_SCROLLABLE);
    
    // Koppel rij-index en click callback
    lv_obj_set_user_data(rows_[i].container, reinterpret_cast<void *>(uintptr_t(i)));
    lv_obj_add_event_cb(rows_[i].container, TaskTile::on_row_click, LV_EVENT_CLICKED, this);

    // Checkbox indicator
    rows_[i].checkbox = tessera::ui::label(rows_[i].container, tessera::Font::BODY, theme::MUTED);
    lv_obj_set_pos(rows_[i].checkbox, 6, (row_h - 20) / 2);
    tessera::ui::set_text(rows_[i].checkbox, "");

    // Taaktekst
    rows_[i].label = tessera::ui::label(rows_[i].container, tessera::Font::BODY, theme::INK);
    lv_obj_set_pos(rows_[i].label, 32, (row_h - 20) / 2);
    lv_obj_set_width(rows_[i].label, width_ - 38);
    lv_label_set_long_mode(rows_[i].label, LV_LABEL_LONG_MODE_DOTS);
    tessera::ui::set_text(rows_[i].label, "");
  }

  fetch_todo_items();
}

void TaskTile::on_row_click(lv_event_t *e) {
  TaskTile *tile = static_cast<TaskTile *>(lv_event_get_user_data(e));
  lv_obj_t *target = lv_event_get_target(e);
  uintptr_t index = reinterpret_cast<uintptr_t>(lv_obj_get_user_data(target));

  if (tile) {
    tile->toggle_item(static_cast<size_t>(index));
  }
}

void TaskTile::toggle_item(size_t index) {
  if (index >= tasks_.size()) return;

  // 1. Direct visuele toggle toepassen op scherm voor snelle feedback
  tasks_[index].completed = !tasks_[index].completed;
  update_rows();

  // 2. Direct HA Service Call sturen om status in Home Assistant aan te passen
  std::map<std::string, std::string> data;
  data["entity_id"] = entity_;
  data["item"] = tasks_[index].text;
  data["status"] = tasks_[index].completed ? "completed" : "needs_action";

  ESP_LOGI(TAG, "Toggling task '%s' to %s", tasks_[index].text.c_str(), data["status"].c_str());
  call_homeassistant_service("todo.update_item", data);
}

void TaskTile::fetch_todo_items() {
  if (entity_.empty()) return;

  // Stuur service call om items te verversen (HA vuurt direct een state-event af)
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

  // Uitlezen van items direct uit state update
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
  fetch_todo_items();
}

void TaskTile::on_tick(uint32_t epoch) {}

void TaskTile::on_theme() {
  for (int i = 0; i < MAX_ROWS; ++i) {
    if (rows_[i].checkbox) tessera::ui::set_color(rows_[i].checkbox, theme::MUTED);
    if (rows_[i].label) tessera::ui::set_color(rows_[i].label, theme::INK);
  }
}

}  // namespace esphome::ha_task_list
