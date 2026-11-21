#include "port/lvgl_port.hpp"

#include <Arduino.h>
#include <esp_heap_caps.h>
#include <lvgl.h>

#include "hal/display_lgfx.hpp"
#include "hal/touch_cst.hpp"
#include "pins.hpp"
#include "ui/ui.hpp"

namespace kmj {
namespace {

lv_display_t* g_disp = nullptr;
void* g_buf1 = nullptr;
size_t g_buf_bytes = 0;

void flush_cb(lv_display_t* disp, const lv_area_t* area, uint8_t* px) {
  const uint32_t w = lv_area_get_width(area);
  const uint32_t h = lv_area_get_height(area);
  lv_draw_sw_rgb565_swap(px, w * h);
  LGFX& tft = display();
  const bool started = tft.getStartCount() == 0;
  if (started) {
    tft.startWrite();
  }
#if defined(KMJ_BOARD_WS_185B)
  // The QSPI bus is configured with DMA off (see display_lgfx.cpp). pushImageDMA
  // still calls waitDMA(), which spins forever when no DMA transfer was queued.
  tft.pushImage(area->x1, area->y1, w, h, reinterpret_cast<uint16_t*>(px));
#else
  // LovyanGFX 1.3 does not expose a DMA-done interrupt, so the transfer is
  // finished here before the buffer is released. Two buffers are still allocated
  // so a later callback can overlap render and SPI without another change.
  tft.pushImageDMA(area->x1, area->y1, w, h, reinterpret_cast<uint16_t*>(px));
  tft.waitDMA();
#endif
  if (started) {
    tft.endWrite();
  }
  lv_display_flush_ready(disp);
}

void read_cb(lv_indev_t*, lv_indev_data_t* data) {
  const TouchSample sample = touch_read();
  ui_set_sample(sample);
  if (!ui_pointer_live(millis()) || !sample.down) {
    data->state = LV_INDEV_STATE_RELEASED;
    return;
  }
  data->state = LV_INDEV_STATE_PRESSED;
  data->point.x = sample.x;
  data->point.y = sample.y;
}

uint32_t tick_cb() { return millis(); }

}  // namespace

void lvgl_port_init() {
  lv_init();
  lv_tick_set_cb(tick_cb);
  g_disp = lv_display_create(kScreenW, kScreenH);
  // One buffer: flush_cb waits for the transfer, so a second one buys nothing and
  // would take 13 KB of internal RAM that Bluetooth needs.
  g_buf_bytes = static_cast<size_t>(kScreenW) * 30u * 2u;
  g_buf1 = heap_caps_malloc(g_buf_bytes, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
  if (g_buf1 == nullptr) {
    g_buf1 = heap_caps_malloc(g_buf_bytes, MALLOC_CAP_INTERNAL);
  }
  lv_display_set_color_format(g_disp, LV_COLOR_FORMAT_RGB565);
  lv_display_set_buffers(g_disp, g_buf1, nullptr, g_buf_bytes, LV_DISPLAY_RENDER_MODE_PARTIAL);
  lv_display_set_flush_cb(g_disp, flush_cb);
  lv_indev_t* indev = lv_indev_create();
  lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(indev, read_cb);
  lv_indev_set_display(indev, g_disp);
}

}  // namespace kmj
