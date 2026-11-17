#pragma once

#include <stdint.h>

#include "hal/touch_cst.hpp"
#include "ui/model.hpp"

namespace kmj {

void ui_init(Host& host);
void ui_tick(uint32_t now_ms);
void ui_set_sample(const TouchSample& sample);
bool ui_pointer_live(uint32_t now_ms);

}  // namespace kmj
