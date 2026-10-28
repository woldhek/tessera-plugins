#include "ha_task_list.h"
#include "esphome/core/log.h"
#include <cstring>

namespace esphome::ha_task_list {

static const char *const TAG = "ha_task_list";
static HaTaskList *g_plugin_instance = nullptr;

// --- Plugin Implementation ---

void HaTaskList::setup() {
  g_plugin_instance = this;
  add_tile("tasks", [this]() { return new TaskTile(this); });
}

void HaTaskList::register_tile(TaskTile *tile) {
  active_tiles_.push_back(tile);
}

void HaTaskList::unregister_tile(TaskTile *tile) {
  for (auto it = active_tiles_.begin(); it != active_tiles_.end(); ++it) {
    if (*it == tile) {
      active_tiles_.erase(it);
      break;
    }
  }
}

void HaTaskList::on_message(JsonObjectConst data) {
  if (data["re"].is<uint32_t>()) {
    uint32_t req_id = data["re"].as<uint32_t>();
    for (auto *tile : active_tiles_) {
      if (tile->current_req_id() == req_id) {
        tile->handle_response(req_id, data);
        break;
      }
    }
  }
}

// --- Tile Implementation ---

TaskTile::TaskTile(const tessera::Plugin *plugin) : plugin_(plugin) {
  if (g_plugin_instance) {
    g_plugin_instance->register_tile(this);
  }
}

TaskTile::~TaskTile() {
  if (g_plugin_instance) {
    g_plugin_instance->unregister_tile(this);
  }
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

  ask_items();
}

void TaskTile::ask_items() {
  if (entity_.empty() || !plugin_) return;

  DynamicJsonDocument doc(512);
  doc["ask"] = "call_service:todo.get_items";
  JsonObject d = doc.createNestedObject("data");
  d["entity_id"] = entity_;

  asked_req_id_ = tessera::send(plugin_, doc.as<JsonObjectConst>());
}

void TaskTile::on_state(JsonObjectConst data) {
  ask_items();
}

void TaskTile::handle_response(uint32_t req_id, JsonObjectConst data) {
  if (req_id != asked_req_id_) return;

  tasks_.clear();

  JsonArrayConst items = data["response"][entity_.c_str()]["items"].as<JsonArrayConst>();
  if (items.isNull()) {
    items = data["items"].as<JsonArrayConst>();
  }

  if (!items.isNull()) {
    for (JsonObjectConst item : items) {
      if (static_cast<int>(tasks_.size()) >= MAX_ROWS) break;

      TaskItem t;
      t.uid = item["uid"] | item["summary"] | "";
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
  for (size_t i = 0; i < tasks_.size(); ++i) {
    if (!tasks_[i].completed) {
      tasks_[i].completed = true;
      update_rows();

      DynamicJsonDocument doc(256);
      doc["item"] = tasks_[i].text;
      doc["status"] = "completed";

      tessera::action("todo.update_item", entity_.c_str(), doc.as<JsonObjectConst>());
      return;
    }
  }

  ask_items();
}

void TaskTile::on_tick(uint32_t epoch) {
  if (epoch % 60 == 0) {
    ask_items();
  }
}

void TaskTile::on_theme() {
  for (int i = 0; i < MAX_ROWS; ++i) {
    if (rows_[i].checkbox) tessera::ui::set_color(rows_[i].checkbox, theme::MUTED);
    if (rows_[i].label) tessera::ui::set_color(rows_[i].label, theme::INK);
  }
}

}  // namespace esphome::ha_task_list
