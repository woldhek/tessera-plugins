# Changelog

## 0.2.0
- What a media player plays now shows on the tile: the picture comes through Home Assistant's own proxy for the
  player, as the screen's media card has it (before, a player's picture address was refused). An image entity and a
  person show their own picture the same way (plugin API 0.17). Where a picture does not come, "No picture" stands in
  the middle of the tile. Needs a screen with plugin API 0.17.
- A camera too: its picture on any screen with pictures, and live on the screens that stream (the ESP32-P4 boards),
  with the option Live camera (on by default). The name is not drawn over a live camera.

## 0.1.0
- First version: a tile that shows one picture, an entity's own or one at an address, cover or contain, with the
  tile's name over it. Needs a screen with plugin API 0.10.
