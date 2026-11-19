#pragma once

#include <LovyanGFX.hpp>

#include "pins.hpp"

namespace kmj {

class LGFX : public lgfx::LGFX_Device {
 public:
  LGFX();

 private:
  lgfx::Bus_SPI bus_;
#if defined(KMJ_BOARD_TDISPLAY_S3)
  lgfx::Panel_ST7796 panel_;
#elif defined(KMJ_BOARD_WS_185B)
  lgfx::Panel_ST77916 panel_;
#endif
};

LGFX& display();
void display_init();
bool display_ready();
void display_show_bootloader();

}  // namespace kmj
