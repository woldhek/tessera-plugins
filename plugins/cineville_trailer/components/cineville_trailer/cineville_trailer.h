#pragma once
#include "esphome/core/component.h"
#include "esphome/components/smart_display/plugin_api.h"

namespace esphome::cineville_trailer {

// EXPERIMENTAL. The trailer camera of the Cineville integration (option "Trailer camera (experimental)"), live on a
// tile: the trailer of the film chosen there, with its sound, on a screen that streams (the ESP32-P4 boards).
class CinevilleTrailer : public Component, public tessera::Plugin {
 public:
  void setup() override;
  float get_setup_priority() const override { return setup_priority::DATA; }
};

}  // namespace esphome::cineville_trailer
