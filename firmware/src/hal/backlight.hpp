#pragma once

#include <stdint.h>

namespace kmj {

void backlight_init();
void backlight_set(uint8_t level);
bool backlight_ready();
uint8_t backlight_level();

}  // namespace kmj
