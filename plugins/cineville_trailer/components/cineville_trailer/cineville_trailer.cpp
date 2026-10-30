#include "cineville_trailer.h"
#include <string>

namespace esphome::cineville_trailer {

using tessera::Font;
namespace ui = tessera::ui;

// The tile: the camera's picture over the card, the film's title on a line of its own under it (never over the
// picture: anything drawn over a live place stops its stream, docs/FIRMWARE_API.md "ui::live"). The camera streams
// while the film plays and shows the film's poster otherwise; on a screen that does not stream it shows the poster,
// and on one without pictures the title alone.
class TrailerTile : public tessera::Tile {
 public:
  explicit TrailerTile(const tessera::Plugin *plugin) : plugin_(plugin) {}

  void create(const tessera::TileContext &c) override {
    entity_ = roomy::text(std::string(c.entity ? c.entity : ""));
    const int line = ui::line_height(Font::BODY);
    if (ui::pictures() && c.height > 2 * line) {
      place_ = ui::picture(c.parent);
      lv_obj_set_size(place_, c.width, c.height - line);
      lv_obj_set_pos(place_, 0, 0);
    }
    title_ = ui::label(c.parent, Font::BODY, theme::INK);
    lv_obj_set_width(title_, c.width);
    lv_obj_set_pos(title_, 0, place_ ? c.height - line : (c.height - line) / 2);
    lv_obj_set_style_text_align(title_, place_ ? LV_TEXT_ALIGN_LEFT : LV_TEXT_ALIGN_CENTER, 0);
    ui::set_text(title_, plugin_->text(entity_.empty() ? "choose_camera" : "no_film"));
  }

  void on_state(JsonObjectConst data) override {
    const char *film = data["attributes"]["film_title"] | "";
    const bool wrong = std::string(data["wait"] | "") == "wrong_entity";
    if (wrong) ui::set_text(title_, plugin_->text("wrong_camera"));
    else if (entity_.empty()) ui::set_text(title_, plugin_->text("choose_camera"));
    else ui::set_text(title_, *film ? std::string(film) : std::string(plugin_->text("no_film")));
    ui::set_color(title_, *film ? theme::INK : theme::MUTED);
    if (!place_ || entity_.empty()) return;
    ui::set_picture(place_, "camera:" + roomy::plain(entity_), tessera::Fit::CONTAIN);
    ui::live(place_, *film != 0);
  }

  void on_theme() override { ui::set_color(title_, theme::INK); }

 private:
  const tessera::Plugin *plugin_;
  tessera::Text entity_;
  lv_obj_t *place_{}, *title_{};
};

void CinevilleTrailer::setup() {
  add_tile("trailer", [this]() { return new TrailerTile(this); });
}

}  // namespace esphome::cineville_trailer
