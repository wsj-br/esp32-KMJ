#include "hal/display_lgfx.hpp"

#include <Arduino.h>

#include "pins.hpp"

namespace kmj {

LGFX::LGFX() {
  {
    auto cfg = bus_.config();
    cfg.spi_host = SPI2_HOST;
    cfg.spi_mode = 0;
    cfg.freq_write = 40000000;
    cfg.freq_read = 16000000;
    cfg.spi_3wire = false;
    cfg.use_lock = true;
    cfg.pin_sclk = 18;
    cfg.pin_mosi = 17;
    cfg.pin_miso = 8;
    cfg.pin_dc = 9;
    bus_.config(cfg);
    panel_.setBus(&bus_);
  }
  {
    auto cfg = panel_.config();
    cfg.pin_cs = 39;
    cfg.pin_rst = 47;
    cfg.pin_busy = -1;
    cfg.memory_width = 320;
    cfg.memory_height = 480;
    cfg.panel_width = 222;
    cfg.panel_height = 480;
    cfg.offset_x = 49;
    cfg.offset_y = 0;
    cfg.offset_rotation = 0;
    cfg.dummy_read_pixel = 8;
    cfg.dummy_read_bits = 1;
    cfg.readable = false;
    cfg.invert = true;
    cfg.rgb_order = false;
    cfg.dlen_16bit = false;
    cfg.bus_shared = false;
    panel_.config(cfg);
  }
  setPanel(&panel_);
}

static LGFX g_display;

LGFX& display() { return g_display; }

void display_init() {
  pinMode(kSdCsPin, OUTPUT);
  digitalWrite(kSdCsPin, HIGH);
  g_display.init();
  g_display.setRotation(1);
  g_display.fillScreen(TFT_BLACK);
}

}  // namespace kmj
