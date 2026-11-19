#include "app/hid_device.hpp"

#include <USB.h>
#include <USBHIDKeyboard.h>
#include <USBHIDMouse.h>
#include <string.h>

#include "tusb.h"

namespace kmj {
namespace {

ESPUSB g_usb;
USBHIDKeyboard g_keyboard;
USBHIDRelativeMouse g_mouse;
KeyReport6 g_keys;
MouseAccum g_mouse_acc;
bool g_key_dirty = false;
bool g_wanted = true;
uint8_t g_leds = 0;
uint32_t g_tx = 0;

void on_led(void*, esp_event_base_t, int32_t, void* data) {
  g_leds = static_cast<arduino_usb_hid_keyboard_event_data_t*>(data)->leds;
}

bool bus_ready(uint8_t instance) {
  if (!g_wanted) {
    return false;
  }
  if (!tud_mounted()) {
    return false;
  }
  if (tud_suspended()) {
    tud_remote_wakeup();
    return false;
  }
  return tud_hid_n_ready(instance);
}

}  // namespace

void hid_begin(uint16_t vid, uint16_t pid) {
  g_usb.VID(vid);
  g_usb.PID(pid);
  g_usb.productName("KMJ Composite");
  g_usb.manufacturerName("KMJ");
  g_usb.usbAttributes(static_cast<uint8_t>(TUSB_DESC_CONFIG_ATT_SELF_POWERED | TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP));
  g_keyboard.begin();
  g_mouse.begin();
  g_usb.begin();
  g_keyboard.onEvent(ARDUINO_USB_HID_KEYBOARD_LED_EVENT, on_led);
}

void hid_set_usb_wanted(bool wanted) {
  if (wanted == g_wanted) {
    return;
  }
  if (wanted) {
    if (!tud_connect()) {
      return;
    }
  } else if (!tud_disconnect()) {
    return;
  }
  g_wanted = wanted;
  if (!wanted) {
    hid_release_all();
  }
}

bool hid_usb_wanted() { return g_wanted; }
bool hid_mounted() { return tud_mounted(); }
uint8_t hid_leds() { return g_leds; }
uint32_t hid_tx() { return g_tx; }

bool write_keys() {
  if (!g_key_dirty || !bus_ready(0)) {
    return false;
  }
  KeyReport report{};
  report.modifiers = g_keys.mods;
  memcpy(report.keys, g_keys.keys, 6);
  g_keyboard.sendReport(&report);
  g_key_dirty = false;
  g_tx++;
  return true;
}

void hid_key(uint8_t usage, bool down) {
  if (!g_keys.set(usage, down)) {
    return;
  }
  // km_poll applies every key in the socket buffer before the periodic HID
  // service. Sending here keeps a press on the bus when its release is in
  // the same read; the panel would otherwise show a key the host never saw.
  g_key_dirty = true;
  write_keys();
}

void hid_mouse(int dx, int dy) { g_mouse_acc.add_move(dx, dy); }
void hid_buttons(uint8_t mask) { g_mouse_acc.set_buttons(mask); }
void hid_wheel(int vertical, int horizontal) { g_mouse_acc.add_wheel(vertical, horizontal); }

void hid_release_all() {
  g_keys.clear();
  g_key_dirty = true;
  g_mouse_acc.dx = 0;
  g_mouse_acc.dy = 0;
  g_mouse_acc.wheel = 0;
  g_mouse_acc.pan = 0;
  g_mouse_acc.set_buttons(0);
}

void hid_service() {
  if (!g_wanted) {
    g_key_dirty = false;
    g_mouse_acc = MouseAccum{};
    return;
  }
  // Keyboard and mouse share HID interface 0 (report IDs). Interface 1 is never ready.
  if (!bus_ready(0)) {
    return;
  }
  if (write_keys()) {
    return;
  }
  if (g_key_dirty) {
    return;
  }
  int8_t x = 0;
  int8_t y = 0;
  int8_t w = 0;
  int8_t p = 0;
  uint8_t buttons = 0;
  if (!g_mouse_acc.take(x, y, w, p, buttons)) {
    return;
  }
  if (!tud_hid_mouse_report(HID_REPORT_ID_MOUSE, buttons, x, y, w, p)) {
    g_mouse_acc.add_move(x, y);
    g_mouse_acc.add_wheel(w, p);
    g_mouse_acc.dirty = true;
    return;
  }
  g_tx++;
}

}  // namespace kmj
