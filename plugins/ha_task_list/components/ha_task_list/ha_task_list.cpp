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

  ESP_LOGI(TAG, "Tile create() gestart voor entiteit: %s", entity_.c_str());

  int row_h = height_ / MAX_ROWS;

  for (int i = 0; i < MAX_ROWS; ++i) {
    int y_pos = i * row_h + (row_h - 20) / 2;

    // Checkbox label
    rows_[i].checkbox = tessera::ui::label(c.parent, tessera::Font::BODY, theme::MUTED);
    lv_obj_set_pos(rows_[i].checkbox, 6, y_pos);
    lv_label_set_long_mode(rows_[i].checkbox, LV_LABEL_LONG_MODE_CLIP);
    tessera::ui::set_text(rows_[i].checkbox, i == 0 ? "[?]" : "[ ]");

    // Taakomschrijving label (start met 'Laden...' op de eerste rij)
    rows_[i].label = tessera::ui::label(c.parent, tessera::Font::BODY, theme::INK);
    lv_obj_set_pos(rows_[i].label, 32, y_pos);
    lv_obj_set_width(rows_[i].label, width_ - 38);
    lv_label_set_long_mode(rows_[i].label, LV_LABEL_LONG_MODE_DOTS);
    tessera::ui::set_text(rows_[i].label, i == 0 ? "Wachten op HA data..." : "");
  }
}

void TaskTile::on_state(JsonObjectConst data) {
  tasks_.clear();

  ESP_LOGI(TAG, "on_state() ontvangen voor %s!", entity_.c_str());

  if (data.isNull()) {
    ESP_LOGW(TAG, "on_state payload is NULL!");
    update_rows();
    return;
  }

  // Haal items op uit de root of uit attributes
  JsonArrayConst items = data["items"].as<JsonArrayConst>();
  if (items.isNull() && data["attributes"].is<JsonObjectConst>()) {
    items = data["attributes"]["items"].as<JsonArrayConst>();
  }

  if (items.isNull()) {
    ESP_LOGW(TAG, "Geen 'items' array gevonden in JSON payload!");
  } else {
    ESP_LOGI(TAG, "Aantal items gevonden in JSON: %d", static_cast<int>(items.size()));
    for (JsonObjectConst item : items) {
      if (static_cast<int>(tasks_.size()) >= MAX_ROWS) break;

      TaskItem t;
      if (item["summary"].is<const char *>()) {
        t.text = item["summary"].as<const char *>();
      } else if (item["text"].is<const char *>()) {
        t.text = item["text"].as<const char *>();
      } else {
        t.text = "Geen tekst";
      }

      const char *status = item["status"] | "";
      t.completed = (strcmp(status, "completed") == 0) || (item["completed"] | false);
      tasks_.push_back(t);
    }
  }

  update_rows();
}

void TaskTile::update_rows() {
  ESP_LOGD(TAG, "update_rows() getekend met %d taken", static_cast<int>(tasks_.size()));
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
