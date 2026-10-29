#include "vinyl_deck.h"
#include <algorithm>
#include <cstdio>
#include <string>

namespace esphome::vinyl_deck {

using tessera::Font;
namespace ui = tessera::ui;

// Home Assistant's MediaPlayerEntityFeature: the bits of the music step's `features` that decide the keys.
constexpr uint32_t PAUSE = 1, VOLUME_SET = 4, VOLUME_MUTE = 8, VOLUME_STEP = 1024, PLAY = 16384;
// Icons of Tessera's set.
constexpr uint32_t ICON_PLAY = 0xF040A, ICON_PAUSE = 0xF03E4, ICON_VOLUME_UP = 0xF075D, ICON_VOLUME_DOWN = 0xF075E,
                   ICON_MUTED = 0xF0581;
// The keys, left to right.
enum Key : uint8_t { DOWN, PLAY_PAUSE, UP, KEYS, NONE = KEYS };

// A face of the screensaver's music (plugin API 0.14): the cover as a record sleeve with the record peeking out at its
// right, the title and the artist beside it (under it on square and standing glass), and three keys.
//
// The cover is the step's own picture: a place with the address "step:", which the app answers with the cover
// of the player on the glass, cut to the place. The keys are made with ui::takes_taps, so a tap on them comes to
// on_tap() and the screen stays in standby; a tap anywhere else wakes it. Play, pause and the mute go through
// tessera::wish(): the key shows its other face at once (on_state hears the wish) and Home Assistant's answer squares
// it. A volume step has nothing to show and goes as an action.
class DeckSaver : public tessera::Saver {
 public:
  void show(const tessera::SaverContext &c) override {
    keys_on_ = c.options["keys"] | true;
    const int m = ui::mm(5), gap = ui::mm(3), key = ui::mm(11);
    const int title_h = ui::line_height(Font::HEADLINE), artist_h = ui::line_height(Font::BODY_LARGE);
    const bool wide = c.width * 10 >= c.height * 13;
    int side, x, y, text_x, text_y, text_w, lines;
    if (wide) {
      // The sleeve at the left, as high as the glass allows; the words and the keys in the column beside the record.
      side = std::min(c.height - 2 * m, c.width * 45 / 100);
      x = m;
      y = (c.height - side) / 2;
      text_x = x + side * 122 / 100 + ui::mm(4);
      text_w = c.width - text_x - m;
      lines = 2;
      const int block = lines * title_h + artist_h + 2 * gap + key;
      text_y = std::max(m, y + (side - block) / 2);
    } else {
      // The sleeve on top, one line of title, the artist and the keys under it, all in the middle.
      lines = 1;
      text_x = m;
      text_w = c.width - 2 * m;
      text_y = c.height - m - key - 2 * gap - artist_h - title_h;
      side = std::min(text_y - gap - m, (c.width - 2 * m) * 100 / 122);
      x = (c.width - side * 122 / 100) / 2;
      y = m + (text_y - gap - m - side) / 2;
    }
    record(c.parent, x + side * 30 / 100, y + side * 4 / 100, side * 92 / 100);
    // The sleeve, made after the record so it lies over it. A board without pictures never shows the music step.
    if (ui::pictures()) {
      sleeve_ = ui::picture(c.parent);
      lv_obj_set_size(sleeve_, side, side);
      lv_obj_set_pos(sleeve_, x, y);
    }
    const lv_text_align_t align = wide ? LV_TEXT_ALIGN_LEFT : LV_TEXT_ALIGN_CENTER;
    title_ = ui::text(c.parent, Font::HEADLINE, theme::INK);
    lv_obj_set_size(title_, text_w, lines * title_h);
    lv_obj_set_pos(title_, text_x, text_y);
    lv_obj_set_style_text_align(title_, align, 0);
    artist_ = ui::label(c.parent, Font::BODY_LARGE, theme::MUTED);
    lv_obj_set_width(artist_, text_w);
    lv_obj_set_pos(artist_, text_x, text_y + lines * title_h);
    lv_obj_set_style_text_align(artist_, align, 0);
    // The keys: round, an icon in each, laid out in on_state once the player says which it has.
    keys_x_ = text_x;
    keys_w_ = wide ? 0 : text_w;  // 0: from the left of the column; else in the middle of this width
    keys_y_ = text_y + lines * title_h + artist_h + 2 * gap;
    for (auto &k : keys_) {
      k = ui::block(c.parent, theme::RAISED);
      lv_obj_set_style_radius(k, LV_RADIUS_CIRCLE, 0);
      lv_obj_set_size(k, key, key);
      lv_obj_t *icon = ui::label(k, Font::ICON, theme::INK);
      lv_obj_set_width(icon, key);
      lv_obj_set_style_text_align(icon, LV_TEXT_ALIGN_CENTER, 0);
      lv_obj_set_pos(icon, 0, (key - ui::line_height(Font::ICON)) / 2);
      lv_obj_add_flag(k, LV_OBJ_FLAG_HIDDEN);
      if (keys_on_) ui::takes_taps(k);
    }
    key_ = key;
    gap_ = gap;
  }

  // The music step's contract: the player (`entity`), what it plays, its state, its features, whether it is muted, and
  // `picture` ("step:" while there is a cover). A wish of a finger comes here at once, in `state` and `muted`.
  void on_state(JsonObjectConst data) override {
    std::snprintf(entity_, sizeof(entity_), "%s", data["entity"] | "");
    playing_ = std::string(data["state"] | "") == "playing";
    muted_ = data["muted"] | false;
    features_ = data["features"] | 0u;
    ui::set_text(title_, data["title"] | "");
    ui::set_text(artist_, data["artist"] | "");
    if (sleeve_) ui::set_picture(sleeve_, data["picture"] | "", tessera::Fit::COVER);
    paint_keys();
  }

  void on_tap() override {
    if (!entity_[0]) return;
    const Key key = key_at(tessera::tap_point());
    if (key == PLAY_PAUSE) {
      tessera::wish(entity_, tessera::Wish::PLAYING, playing_ ? "0" : "1");
    } else if (key != NONE && muted_ && (features_ & VOLUME_MUTE)) {
      // A muted player: the first tap on a volume key takes the mute off and changes nothing else.
      tessera::wish(entity_, tessera::Wish::MUTED, "0");
    } else if (key != NONE) {
      tessera::action(key == UP ? "media_player.volume_up" : "media_player.volume_down", entity_);
    }
  }

  // Volume down held for a second and a half mutes the player, or takes the mute off.
  void on_hold() override {
    if (entity_[0] && key_at(tessera::tap_point()) == DOWN && (features_ & VOLUME_MUTE))
      tessera::wish(entity_, tessera::Wish::MUTED, muted_ ? "0" : "1");
  }

 private:
  // The record: a grey disc with a groove and a label in the accent, in the middle a hole.
  void record(lv_obj_t *parent, int x, int y, int d) {
    lv_obj_t *disc = ui::block(parent, theme::TRACK);
    lv_obj_set_style_radius(disc, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_size(disc, d, d);
    lv_obj_set_pos(disc, x, y);
    const int groove = d * 72 / 100, label = d * 34 / 100, hole = std::max(2, d * 4 / 100);
    lv_obj_t *ring = ui::block(disc, theme::TRACK);
    lv_obj_set_style_radius(ring, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(ring, std::max(1, ui::px(1)), 0);
    lv_obj_set_style_border_color(ring, ui::color(theme::LINE), 0);
    lv_obj_set_size(ring, groove, groove);
    lv_obj_set_pos(ring, (d - groove) / 2, (d - groove) / 2);
    lv_obj_t *middle = ui::block(disc, theme::ACCENT);
    lv_obj_set_style_radius(middle, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_size(middle, label, label);
    lv_obj_set_pos(middle, (d - label) / 2, (d - label) / 2);
    lv_obj_t *dot = ui::block(middle, theme::CAMERA_PAGE);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_size(dot, hole, hole);
    lv_obj_set_pos(dot, (label - hole) / 2, (label - hole) / 2);
  }

  // Which keys the player has, where they stand, and the face of each: only positions and texts change here.
  void paint_keys() {
    const bool play = keys_on_ && (features_ & (PAUSE | PLAY));
    const bool volume = keys_on_ && (features_ & (VOLUME_SET | VOLUME_STEP));
    const bool shown[KEYS] = {volume, play, volume};
    const uint32_t icons[KEYS] = {muted_ ? ICON_MUTED : ICON_VOLUME_DOWN, playing_ ? ICON_PAUSE : ICON_PLAY,
                                  ICON_VOLUME_UP};
    const int count = (play ? 1 : 0) + (volume ? 2 : 0);
    const int row = count * key_ + (count > 1 ? (count - 1) * gap_ : 0);
    int x = keys_w_ ? keys_x_ + (keys_w_ - row) / 2 : keys_x_;
    for (int i = 0; i < KEYS; ++i) {
      if (!shown[i]) {
        lv_obj_add_flag(keys_[i], LV_OBJ_FLAG_HIDDEN);
        continue;
      }
      lv_obj_set_pos(keys_[i], x, keys_y_);
      lv_obj_remove_flag(keys_[i], LV_OBJ_FLAG_HIDDEN);
      ui::set_text(lv_obj_get_child(keys_[i], 0), ui::icon(icons[i]));
      x += key_ + gap_;
    }
  }

  // The key under the finger (tap_point() is in the glass's pixels), with the bit of room around it a key takes.
  Key key_at(tessera::Point p) const {
    const int slack = ui::px(6);
    for (int i = 0; i < KEYS; ++i) {
      if (lv_obj_has_flag(keys_[i], LV_OBJ_FLAG_HIDDEN)) continue;
      lv_area_t a;
      lv_obj_get_coords(keys_[i], &a);
      if (p.x >= a.x1 - slack && p.x <= a.x2 + slack && p.y >= a.y1 - slack && p.y <= a.y2 + slack)
        return static_cast<Key>(i);
    }
    return NONE;
  }

  bool keys_on_ = true, playing_ = false, muted_ = false;
  uint32_t features_ = 0;
  int key_ = 0, gap_ = 0, keys_x_ = 0, keys_w_ = 0, keys_y_ = 0;
  char entity_[128] = "";
  lv_obj_t *sleeve_{}, *title_{}, *artist_{}, *keys_[KEYS]{};
};

void VinylDeck::setup() {
  // The id is the saver's id in the manifest, and its step must be the manifest's (media).
  add_saver("deck", tessera::SaverStep::MEDIA, []() { return new DeckSaver(); });
}

}  // namespace esphome::vinyl_deck
