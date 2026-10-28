#include "ha_task_list.h"
#include "esphome/core/log.h"
#include <cstring>

namespace esphome::ha_task_list {

static const char *const TAG = "ha_task_list";

// --- Plugin Implementation ---

void HaTaskList::setup() {
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
    ESP_LOGW(TAG, "on_message ontvangen voor req_id: %u", req_id);
    for (auto *tile : active_tiles_) {
      if (tile->current_req_id() == req_id) {
        tile->handle_response(req_id, data);
        break;
      }
    }
  }
}

// --- Tile Implementation ---

TaskTile::TaskTile(HaTaskList *plugin) : plugin_(plugin) {
  if (plugin_) {
    plugin_->register_tile(this);
  }
}

TaskTile::~TaskTile() {
  if (plugin_) {
    plugin_->unregister_tile(this);
  }
}

void TaskTile::create(const tessera::TileContext &c) {
  width_ = c.width;
  height_ = c.height;
  entity_ = c.entity;
  name_ = c.name;

  ESP_LOGW(TAG, "TaskTile::create - entity: '%s', name: '%s'", entity_.c_str(), name_.c_str());

  row_h_ = height_ / MAX_ROWS;

  for (int i = 0; i < MAX_ROWS; ++i) {
    int y_pos = i * row_h_ + (row_h_ - 20) / 2;

    rows_[i].checkbox = tessera::ui::label(c.parent, tessera::Font::BODY, theme::MUTED);
    lv_obj_set_pos(rows_[i].checkbox, 6, y_pos);
    tessera::ui::set_text(rows_[i].checkbox, "");

    rows_[i].label = tessera::ui::label(c.parent, tessera::Font::BODY, theme::INK);
    lv_obj_set_pos(rows_[i].label, 32, y_pos);
    lv_obj_set_width(rows_[i].label, width_ - 38);
    lv_label_set_long_mode(rows_[i].label, LV_LABEL_LONG_MODE_DOTS);
    tessera::ui::set_text(rows_[i].label, "");
  }

  if (entity_.empty()) {
    ESP_LOGW(TAG, "TaskTile::create - entity is leeg!");
    tessera::ui::set_text(rows_[0].label, "Geen entiteit gekoppeld");
  }
}

void TaskTile::ask_items() {
  if (entity_.empty() || !plugin_) {
    ESP_LOGW(TAG, "ask_items overgeslagen: entity is leeg of plugin pointer is null");
    return;
  }

  DynamicJsonDocument doc(512);
  doc["ask"] = "call_service:todo.get_items";
  JsonObject d = doc.createNestedObject("data");
  d["entity_id"] = entity_;

  asked_req_id_ = tessera::send(plugin_, doc.as<JsonObjectConst>());
  ESP_LOGW(TAG, "ask_items: verzoek verzonden naar app (req_id: %u, entity: '%s')", asked_req_id_, entity_.c_str());
}

void TaskTile::on_state(JsonObjectConst data) {
  ESP_LOGW(TAG, "on_state getriggerd for entity: '%s'", entity_.c_str());
  ask_items();
}

void TaskTile::handle_response(uint32_t req_id, JsonObjectConst data) {
  if (req_id != asked_req_id_) return;

  bool ok = data["ok"] | false;
  ESP_LOGW(TAG, "handle_response ontvangen (req_id: %u, ok: %s)", req_id, ok ? "true" : "false");

  if (!ok) {
    ESP_LOGW(TAG, "Verzoek %u geweigerd of mislukt door app", req_id);
    return;
  }

  tasks_.clear();

  JsonObjectConst result = data["result"].as<JsonObjectConst>();
  JsonArrayConst items;

  if (!result.isNull()) {
    if (result[entity_.c_str()]["items"].is<JsonArrayConst>()) {
      items = result[entity_.c_str()]["items"].as<JsonArrayConst>();
    } else if (result["items"].is<JsonArrayConst>()) {
      items = result["items"].as<JsonArrayConst>();
    }
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

  ESP_LOGW(TAG, "handle_response: %d taken geladen in vector", static_cast<int>(tasks_.size()));
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
      tessera::ui::set_text(rows_[i].label, (i == 0 && tasks_.empty()) ? "Geen taken" : "");
    }
  }
}

void TaskTile::toggle_item(size_t index) {
  if (index >= tasks_.size()) return;

  tasks_[index].completed = !tasks_[index].completed;
  update_rows();

  DynamicJsonDocument doc(256);
  doc["item"] = tasks_[index].uid.empty() ? tasks_[index].text : tasks_[index].uid;
  doc["status"] = tasks_[index].completed ? "completed" : "needs_action";

  ESP_LOGW(TAG, "toggle_item: actie verzonden voor item '%s'", tasks_[index].text.c_str());
  tessera::action("todo.update_item", entity_.c_str(), doc.as<JsonObjectConst>());
}

void TaskTile::on_tap() {
  auto p = tessera::tap_point();
  if (p.y < 0 || row_h_ <= 0) return;

  size_t clicked_row = static_cast<size_t>(p.y / row_h_);
  ESP_LOGW(TAG, "on_tap getriggerd op y=%d (rij %d)", p.y, static_cast<int>(clicked_row));

  if (clicked_row < tasks_.size()) {
    toggle_item(clicked_row);
  } else {
    ask_items();
  }
}

void TaskTile::on_tick(uint32_t epoch) {
  if (epoch > 0 && epoch % 60 == 0) {
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
