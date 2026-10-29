#include "picture_frame.h"
#include <string>

namespace esphome::picture_frame {

using tessera::Font;
namespace ui = tessera::ui;

// The tile: one place for a picture over the whole card, the name in its corner. The address comes from the option, or
// from the entity itself when the option is empty (plugin API 0.17, docs/PICTURES.md "Sources"): what a player plays
// (`cover:`, through Home Assistant's own proxy for it, as the screen's media card has it), a camera's or an image
// entity's picture (`camera:`), a person's photo (`entity:`). A camera set to Live moves on a screen that streams (the
// ESP32-P4 boards, ui::live) and has no name over it: anything drawn over a live picture stops its stream. On a board
// that shows no pictures the tile says so, and stays a plain card otherwise; where a picture does not come, the same
// words stand on its place.
class PictureTile : public tessera::Tile {
 public:
  explicit PictureTile(const tessera::Plugin *plugin) : plugin_(plugin) {}

  void create(const tessera::TileContext &c) override {
    url_ = c.options["url"] | "";
    contain_ = std::string(c.options["fit"] | "cover") == "contain";
    caption_ = c.options["caption"] | true;
    live_ = c.options["live"] | true;
    name_ = c.name;
    entity_ = c.entity;
    height_ = c.height;
    if (ui::pictures()) {
      place_ = ui::picture(c.parent);
      lv_obj_set_size(place_, c.width, c.height);
      lv_obj_set_pos(place_, 0, 0);
    }
    // The name in the corner, or, without pictures, the word that says why there is none.
    label_ = ui::label(c.parent, Font::BODY, place_ ? theme::INK : theme::MUTED);
    lv_obj_set_width(label_, c.width);
    lv_obj_set_pos(label_, 0, place_ ? c.height - ui::line_height(Font::BODY) : (c.height - ui::line_height(Font::BODY)) / 2);
    if (!place_) lv_obj_set_style_text_align(label_, LV_TEXT_ALIGN_CENTER, 0);
    ui::set_text(label_, place_ ? (named() ? roomy::plain(name_) : "") : plugin_->text("no_picture"));
    if (place_ && !named()) lv_obj_add_flag(label_, LV_OBJ_FLAG_HIDDEN);
  }

  void on_state(JsonObjectConst data) override {
    if (!place_) return;
    ui::set_picture(place_, address(), contain_ ? tessera::Fit::CONTAIN : tessera::Fit::COVER);
    ui::live(place_, streams());
  }

  // Where the picture did not come, the words say so in the middle of the card; once it is here, the name again.
  void on_picture(lv_obj_t *place, tessera::PictureState state) override {
    if (place != place_) return;
    missing_ = state != tessera::PictureState::SHOWN;
    lv_obj_set_style_text_align(label_, missing_ ? LV_TEXT_ALIGN_CENTER : LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_set_y(label_, missing_ ? (height_ - ui::line_height(Font::BODY)) / 2 : height_ - ui::line_height(Font::BODY));
    ui::set_text(label_, missing_ ? plugin_->text("no_picture") : (named() ? roomy::plain(name_) : ""));
    // Nothing over a live camera's picture, not even an empty label: it would stop the stream.
    if (missing_ || named()) lv_obj_remove_flag(label_, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(label_, LV_OBJ_FLAG_HIDDEN);
    on_theme();
  }

  void on_theme() override { ui::set_color(label_, place_ && !missing_ ? theme::INK : theme::MUTED); }

 private:
  // The option's address, else the entity's own picture by its kind.
  std::string address() const {
    if (!url_.empty()) return roomy::plain(url_);
    const std::string entity = roomy::plain(entity_);
    if (entity.rfind("media_player.", 0) == 0) return "cover:" + entity;
    if (entity.rfind("image.", 0) == 0 || entity.rfind("camera.", 0) == 0) return "camera:" + entity;
    return entity.empty() ? std::string() : "entity:" + entity;
  }

  // A camera set to Live streams where the screen can (ui::live); a still picture never does.
  bool streams() const { return live_ && url_.empty() && roomy::plain(entity_).rfind("camera.", 0) == 0; }
  // The name over the picture, unless it would lie over a live camera.
  bool named() const { return caption_ && !streams(); }

  const tessera::Plugin *plugin_;
  tessera::Text url_, name_, entity_;   // kept as long as the card: PSRAM first (docs/FIRMWARE_API.md, "Memory")
  bool contain_ = false, caption_ = true, live_ = true, missing_ = false;
  int height_ = 0;
  lv_obj_t *place_{}, *label_{};
};

void PictureFrame::setup() {
  add_tile("picture", [this]() { return new PictureTile(this); });
}

}  // namespace esphome::picture_frame
