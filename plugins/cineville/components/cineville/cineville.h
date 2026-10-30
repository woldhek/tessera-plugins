#pragma once

#include "esphome/core/component.h"
#include "esphome/components/smart_display/plugin_api.h"

namespace esphome::cineville {

class Cineville;

// One film of a list of the Cineville integration's sensor: "Now showing" (when and where it shows next) or "Coming
// soon" (when it opens). Fixed fields, so a whole list is one block (in PSRAM on a board that has it). The app cuts
// every text to 48 bytes; a theater's name is cut shorter here, it is the last part of a line.
struct Film {
  char id[40]{};           // Cineville's id of the film, for the detail page and the actions
  char title[49]{};
  char director[49]{};
  char venue[25]{};
  uint32_t next{0};        // the next showtime, seconds since 1970; 0 for none
  uint32_t premiere{0};    // the day it opens (noon of that day), for a film of "Coming soon"; 0 for none
  uint16_t shows{0};       // its showtimes in the chosen days (for "Coming soon": its previews)
  uint16_t today{0};       // of those, today
  uint8_t pick{0};         // Cineville's pick: 0 none, PICK_WEEK, PICK_TIP
};
using Films = tessera::List<Film>;

static constexpr uint8_t PICK_WEEK = 1, PICK_TIP = 2;
// The integration's short lists hold at most sixteen films.
static constexpr int MAX_FILMS = 16;

// What a list shows: everything that still shows, only what still shows today, only Cineville's picks.
struct Filter {
  bool today{false};
  bool picks{false};
};

// The films of a sensor, as the tile and the card read them from its attributes.
struct FilmList {
  Films films;
  bool soon{false};        // "Coming soon" rather than "Now showing"
  bool loaded{false};      // the sensor's lists have come
  bool unavailable{false}; // the sensor has no value (the integration could not reach Cineville)
  bool wrong{false};       // the entity is not one of the Cineville integration
  void read(JsonObjectConst data);
  // The indexes of the films that pass `filter` now, in the sensor's order.
  void pick(const Filter &filter, tessera::List<uint8_t> &out) const;
};

// Rows of films, shared by the tile and the card: a bar at the left that is coloured for Cineville's picks, the title,
// on the card the director, and (when there is room) the pick with when and where it shows next, or when it opens.
class FilmRows {
 public:
  static constexpr int MAX_SLOTS = 12;   // the card's most
  static constexpr int TILE_SLOTS = 8;   // a tile's most: what the manifest's memory counts (three parts a row)

  void make(Cineville *plugin, lv_obj_t *parent, int width, int slots, tessera::Font title, bool subs, bool director);
  int content() const;  // the height of a row's lines
  int row() const;      // the height of a row
  // Lay out the films `order[first..]` of `list` in `height` pixels; returns how many it shows. Hides the rest.
  int show(const FilmList &list, const tessera::List<uint8_t> &order, int first, int height, bool spread);
  void repaint(const FilmList &list, const tessera::List<uint8_t> &order);
  void hide();
  // The index in the list of the film whose row is under `y`, -1 for none.
  int film_at(int y) const;

 private:
  struct Slot {
    lv_obj_t *bar{}, *title{}, *sub{}, *info{};   // info: the director, on the card
    int film{-1}, y{0}, h{0};
  };
  std::string sub_text(const FilmList &list, const Film &f, bool pick) const;
  void paint(Slot &s, const FilmList &list, const Film &f);

  Cineville *plugin_{nullptr};
  Slot slots_[MAX_SLOTS];
  int count_{0}, width_{0};
  tessera::Font title_font_{tessera::Font::BODY};
  bool subs_{true}, director_{false};
};

class FilmTile : public tessera::Tile {
 public:
  explicit FilmTile(Cineville *plugin) : plugin_(plugin) {}
  void create(const tessera::TileContext &c) override;
  void on_state(JsonObjectConst data) override;
  void on_tick(uint32_t epoch) override;
  void on_theme() override;
  void on_tap() override;

 private:
  void layout();

  Cineville *plugin_{nullptr};
  FilmRows rows_;
  FilmList list_;
  tessera::List<uint8_t> order_;
  Filter filter_;
  tessera::Text entity_, name_, sensor_name_;
  lv_obj_t *note_{nullptr}, *more_{nullptr};
  int width_{0}, height_{0}, tile_{-1};
};

// One film on the card's detail page, as the action cineville.get_film answers (integration 0.2).
struct Detail {
  char id[40]{};
  char title[49]{};
  char meta[100]{};        // director · year · duration
  char player[49]{};       // the media player a trailer plays on, "" for none
  tessera::Text blurb;
  uint32_t shows[4]{};     // the next showtimes, seconds since 1970
  char venues[4][25]{};
  uint32_t premiere{0};
  uint8_t pick{0};
  bool trailer{false}, loaded{false}, failed{false};
};

class FilmCard : public tessera::Card {
 public:
  explicit FilmCard(Cineville *plugin);
  ~FilmCard() override;
  void open(const tessera::CardContext &c) override;
  void on_state(JsonObjectConst data) override;
  void on_tick(uint32_t epoch) override;
  void on_theme() override;
  void on_tap() override;
  bool on_back() override;

  void answer(JsonObjectConst message);
  uint32_t asked() const { return asked_; }

 private:
  void layout();
  void show_detail(int index);
  void layout_detail();
  void paint_detail();
  void hide_detail();
  void hide_list();

  Cineville *plugin_{nullptr};
  FilmRows rows_;
  FilmList list_;
  tessera::List<uint8_t> order_;
  Filter filter_;
  tessera::Text entity_;
  lv_obj_t *note_{nullptr}, *prev_{nullptr}, *next_{nullptr}, *page_{nullptr}, *rule_{nullptr};
  int width_{0}, height_{0}, foot_{0}, per_page_{1}, page_index_{0};
  // The detail page: a film's title, the facts, its pick, the blurb, the next showtimes, and two keys.
  bool detail_open_{false};
  Detail detail_;
  lv_obj_t *d_title_{nullptr}, *d_meta_{nullptr}, *d_pick_{nullptr}, *d_blurb_{nullptr}, *d_shows_[4]{},
      *d_note_{nullptr}, *k_seen_{nullptr}, *k_seen_text_{nullptr}, *k_play_{nullptr}, *k_play_text_{nullptr};
  uint32_t asked_{0}, asked_at_{0}, note_until_{0};
};

class Cineville : public Component, public tessera::Plugin {
 public:
  void setup() override;
  float get_setup_priority() const override { return setup_priority::DATA; }
  void on_message(JsonObjectConst message) override;
  // What the tile that opens the card shows (today only, picks only), for the card to show the same.
  Filter opening;
  FilmCard *card{nullptr};  // the card that is open, to hand it its answer
};

}  // namespace esphome::cineville
