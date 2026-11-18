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
static bool g_ready = false;

LGFX& display() { return g_display; }

void display_init() {
  pinMode(kSdCsPin, OUTPUT);
  digitalWrite(kSdCsPin, HIGH);
  g_display.init();
  g_display.setRotation(1);
  g_display.fillScreen(TFT_BLACK);
  g_ready = true;
}

bool display_ready() { return g_ready; }

void display_show_bootloader() {
  if (!g_ready) {
    return;
  }
  const uint16_t bg = g_display.color565(0x12, 0x14, 0x18);
  const uint16_t amber = g_display.color565(0xF0, 0xA2, 0x02);
  const uint16_t text = g_display.color565(0xC9, 0xD2, 0xDC);
  g_display.startWrite();
  g_display.fillScreen(bg);
  g_display.setTextDatum(lgfx::middle_center);
  g_display.setFont(&lgfx::fonts::DejaVu24);
  g_display.setTextColor(amber, bg);
  g_display.drawString("Bootloader mode", g_display.width() / 2, g_display.height() / 2 - 18);
  g_display.setFont(&lgfx::fonts::DejaVu18);
  g_display.setTextColor(text, bg);
  g_display.drawString("Waiting for new firmware...", g_display.width() / 2, g_display.height() / 2 + 18);
  g_display.endWrite();
}

}  // namespace kmj
