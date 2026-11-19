#include "logic/keys.hpp"

#include <string.h>

namespace kmj {

void KeyReport6::clear() {
  mods = 0;
  reserved = 0;
  memset(keys, 0, sizeof(keys));
}

bool KeyReport6::set(uint8_t usage, bool down) {
  if (usage >= 0xE0 && usage < 0xE8) {
    const uint8_t bit = static_cast<uint8_t>(1u << (usage - 0xE0));
    if (down) {
      mods = static_cast<uint8_t>(mods | bit);
    } else {
      mods = static_cast<uint8_t>(mods & static_cast<uint8_t>(~bit));
    }
    return true;
  }
  if (usage == 0 || usage >= 0xA5) {
    return false;
  }
  if (down) {
    for (uint8_t key : keys) {
      if (key == usage) {
        return true;
      }
    }
    for (uint8_t& key : keys) {
      if (key == 0) {
        key = usage;
        return true;
      }
    }
    return false;
  }
  for (uint8_t& key : keys) {
    if (key == usage) {
      key = 0;
    }
  }
  // Hosts stop at the first empty slot. A release in the middle would hide
  // every key after it, so the array stays packed.
  uint8_t packed[6] = {};
  uint8_t count = 0;
  for (uint8_t key : keys) {
    if (key != 0) {
      packed[count++] = key;
    }
  }
  memcpy(keys, packed, sizeof(keys));
  return true;
}

void MouseAccum::add_move(int x, int y) {
  dx += x;
  dy += y;
  if (dx != 0 || dy != 0) {
    dirty = true;
  }
}

void MouseAccum::add_wheel(int v, int h) {
  wheel += v;
  pan += h;
  if (wheel != 0 || pan != 0) {
    dirty = true;
  }
}

void MouseAccum::set_buttons(uint8_t mask) {
  if (mask != buttons) {
    buttons = mask;
    dirty = true;
  }
}

bool MouseAccum::take(int8_t& x, int8_t& y, int8_t& w, int8_t& p, uint8_t& buttons_out) {
  if (!dirty && dx == 0 && dy == 0 && wheel == 0 && pan == 0) {
    return false;
  }
  auto chunk = [](int& remain) -> int8_t {
    if (remain > 127) {
      remain -= 127;
      return 127;
    }
    if (remain < -127) {
      remain += 127;
      return -127;
    }
    const int8_t v = static_cast<int8_t>(remain);
    remain = 0;
    return v;
  };
  x = chunk(dx);
  y = chunk(dy);
  w = chunk(wheel);
  p = chunk(pan);
  buttons_out = buttons;
  dirty = dx != 0 || dy != 0 || wheel != 0 || pan != 0;
  return true;
}

}  // namespace kmj
