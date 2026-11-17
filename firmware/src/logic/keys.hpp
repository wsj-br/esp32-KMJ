#pragma once

#include <stdint.h>

namespace kmj {

// 6KRO keyboard report plus modifiers (HID usages 0xE0-0xE7).
struct KeyReport6 {
  uint8_t mods = 0;
  uint8_t reserved = 0;
  uint8_t keys[6] = {};

  void clear();
  // False when a non-modifier key does not fit.
  bool set(uint8_t usage, bool down);
};

struct MouseAccum {
  int dx = 0;
  int dy = 0;
  int wheel = 0;
  int pan = 0;
  uint8_t buttons = 0;
  bool dirty = false;

  void add_move(int x, int y);
  void add_wheel(int v, int h);
  void set_buttons(uint8_t mask);
  // Pulls one HID-sized report. False when nothing is queued.
  bool take(int8_t& x, int8_t& y, int8_t& w, int8_t& p, uint8_t& buttons_out);
};

}  // namespace kmj
