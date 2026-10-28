#pragma once

#include "esphome/core/component.h"
#include "esphome/components/smart_display/plugin_api.h"
#include <vector>
#include <string>

namespace esphome::ha_task_list {

class HaTaskList;

struct TaskItem {
  std::string uid;
  std::string text;
  bool completed{false};
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

  void ask_items();
  void handle_response(uint32_t req_id, JsonObjectConst data);
  uint32_t current_req_id() const { return asked_req_id_; }

 private:
  void update_rows();
  void toggle_item(size_t index);

  HaTaskList *plugin_{nullptr};
  int width_{0};
  int height_{0};
  int row_h_{24};
  std::string entity_;
  std::string name_;

  uint32_t asked_req_id_{0};
  std::vector<TaskItem> tasks_;

  struct Row {
    lv_obj_t *checkbox{nullptr};
    lv_obj_t *label{nullptr};
  };

  static constexpr int MAX_ROWS = 5;
  Row rows_[MAX_ROWS];
};

class HaTaskList : public Component, public tessera::Plugin {
 public:
  void setup() override;
  float get_setup_priority() const override { return setup_priority::DATA; }

  void register_tile(TaskTile *tile);
  void unregister_tile(TaskTile *tile);

  void on_message(JsonObjectConst data) override;

 private:
  std::vector<TaskTile *> active_tiles_;
};

}  // namespace esphome::ha_task_list
