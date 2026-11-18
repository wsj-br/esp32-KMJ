#pragma once

#include <LovyanGFX.hpp>

namespace kmj {

class LGFX : public lgfx::LGFX_Device {
 public:
  LGFX();

 private:
  lgfx::Bus_SPI bus_;
  lgfx::Panel_ST7796 panel_;
};

LGFX& display();
void display_init();
bool display_ready();
void display_show_bootloader();

}  // namespace kmj
