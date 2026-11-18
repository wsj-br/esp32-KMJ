#include <Arduino.h>
#include <esp32-hal-tinyusb.h>

#include "app/debug_log.hpp"
#include "app/hid_device.hpp"
#include "app/improv_ble.hpp"
#include "app/net_wifi.hpp"
#include "app/runtime.hpp"
#include "app/setup_mode.hpp"
#include "app/settings.hpp"
#include "hal/backlight.hpp"
#include "hal/display_lgfx.hpp"
#include "hal/pmu_sy6970.hpp"
#include "hal/touch_cst.hpp"

void setup() {
  KMJ_LOG_BEGIN();
  KMJ_LOG_HEAP("boot");
  kmj::improv_install_fault_hook();
  if (kmj::setup_mode_boot()) {
    kmj::setup_mode_run();
  }
  kmj::backlight_init();
  KMJ_LOG_HEAP("backlight_init");
  kmj::display_init();
  KMJ_LOG_HEAP("display_init");
  kmj::touch_init();
  KMJ_LOG_HEAP("touch_init");
  kmj::pmu_init();
  KMJ_LOG_HEAP("pmu_init");
  kmj::settings_load();
  KMJ_LOG_HEAP("settings");
  kmj::backlight_set(static_cast<uint8_t>(kmj::settings().backlight));
  kmj::hid_begin(kmj::settings().vid, kmj::settings().pid);
  KMJ_LOG_HEAP("hid_begin");
  kmj::net_begin(kmj::settings().hostname);
  KMJ_LOG_HEAP("net_begin");
  if (kmj::settings().ssid[0] != '\0') {
    kmj::net_connect(kmj::settings().ssid, kmj::settings().password);
  }
  kmj::runtime_start();
  KMJ_LOG_HEAP("runtime");
}

void loop() { vTaskDelay(portMAX_DELAY); }

#if SOC_USB_OTG_SUPPORTED && CONFIG_TINYUSB_ENABLED
extern "C" {
void __real_usb_persist_restart(restart_type_t mode);
void __wrap_usb_persist_restart(restart_type_t mode) {
  if (mode == RESTART_BOOTLOADER || mode == RESTART_BOOTLOADER_DFU) {
    kmj::runtime_show_bootloader_and_halt();
  }
  __real_usb_persist_restart(mode);
}
}
#endif
