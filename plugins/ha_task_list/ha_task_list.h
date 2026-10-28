#pragma once

#include "esphome/core/component.h"
#include "esphome/components/smart_display/plugin_api.h"
#include <vector>
#include <string>

namespace esphome::ha_task_list {

struct TaskItem {
  std::string text;
  bool completed{false};
};

class HATaskList : public Component, public tessera::Plugin {
 public:
  void setup() override;
  float get_setup_priority() const override { return setup_priority::DATA; }
};

class TaskTile : public tessera::Tile {
 public:
  explicit TaskTile(const tessera::Plugin *plugin) : plugin_(plugin) {}
  void create(const tessera::TileContext &c) override;
  void on_state(JsonObjectConst data) override;
  void on_tick(uint32_t epoch) override;
  void on_theme() override;

 private:
  const tessera::Plugin *plugin_;
  int width_ = 0;
  int height_ = 0;
  std::string entity_;
  std::string name_;
  std::vector<TaskItem> tasks_;

  struct Row {
    lv_obj_t *checkbox{nullptr};
    lv_obj_t *label{nullptr};
  };

  static constexpr int MAX_ROWS = 5;
  Row rows_[MAX_ROWS];

  void update_rows();
};

}  // namespace esphome::ha_task_list
