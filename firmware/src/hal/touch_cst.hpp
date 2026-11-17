#pragma once

#include <stdint.h>

namespace kmj {

struct TouchSample {
  bool ok = false;
  bool down = false;
  bool home = false;
  int16_t x = 0;
  int16_t y = 0;
};

bool touch_init();
bool touch_ready();
TouchSample touch_read();

}  // namespace kmj
