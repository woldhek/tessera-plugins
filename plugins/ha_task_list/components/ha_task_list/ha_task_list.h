#pragma once

#include "esphome/core/component.h"
#include "esphome/components/api/custom_api_device.h"
#include "tessera.h"
#include <vector>
#include <string>

namespace esphome::ha_task_list {

struct TaskItem {
  std::string uid;
  std::string text;
  bool completed{false};
};

class TaskTile : public tessera::Tile, public api::CustomAPIDevice {
 public:
  explicit TaskTile(const tessera::Plugin *plugin) : plugin_(plugin) {}

  void create(const tessera::TileContext &c) override;
  void on_state(JsonObjectConst data) override;
  void on_tick(uint32_t epoch) override;
  void on_theme() override;
  void on_tap() override;

  void fetch_todo_items();
  void toggle_item(size_t index);

 private:
  void update_rows();
  static void on_row_click(lv_event_t *e);

  const tessera::Plugin *plugin_{nullptr};
  int width_{0};
  int height_{0};
  std::string entity_;
  std::string name_;

  std::vector<TaskItem> tasks_;

  struct Row {
    lv_obj_t *container{nullptr};
    lv_obj_t *checkbox{nullptr};
    lv_obj_t *label{nullptr};
  };

  static constexpr int MAX_ROWS = 5;
  Row rows_[MAX_ROWS];
};

class HaTaskList : public tessera::Plugin {
 public:
  void setup() override;
};

}  // namespace esphome::ha_task_list
