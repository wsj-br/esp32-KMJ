#include <Arduino.h>

#include "app/hid_device.hpp"
#include "app/net_wifi.hpp"
#include "app/runtime.hpp"
#include "app/settings.hpp"
#include "hal/backlight.hpp"
#include "hal/display_lgfx.hpp"
#include "hal/pmu_sy6970.hpp"
#include "hal/touch_cst.hpp"

void setup() {
  kmj::backlight_init();
  kmj::display_init();
  kmj::touch_init();
  kmj::pmu_init();
  kmj::settings_load();
  kmj::backlight_set(static_cast<uint8_t>(kmj::settings().backlight));
  kmj::hid_begin(kmj::settings().vid, kmj::settings().pid);
  kmj::net_begin(kmj::settings().hostname);
  if (kmj::settings().ssid[0] != '\0') {
    kmj::net_connect(kmj::settings().ssid, kmj::settings().password);
  }
  kmj::runtime_start();
}

void loop() { vTaskDelay(portMAX_DELAY); }
