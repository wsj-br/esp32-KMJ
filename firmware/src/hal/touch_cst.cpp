#include "hal/touch_cst.hpp"

#include <Arduino.h>
#include <TouchDrvCST.hpp>
#include <Wire.h>

#include "app/debug_log.hpp"
#include "hal/i2c_lock.hpp"
#include "logic/button.hpp"
#include "pins.hpp"

namespace kmj {
namespace {

TouchDrvCSTXXX g_touch;
bool g_ok = false;
bool g_home = false;
Button g_home_button;

void on_home(void*) { g_home = true; }

}  // namespace

bool touch_init() {
  pinMode(kTouchResPin, OUTPUT);
  digitalWrite(kTouchResPin, LOW);
  delay(10);
  digitalWrite(kTouchResPin, HIGH);
  delay(50);
  pinMode(kTouchIrqPin, INPUT);
  i2c_lock_init();
  Wire.begin(kTouchSdaPin, kTouchSclPin);
  // SensorLib only pulses the reset pin when it has been told the GPIO. Without
  // that it writes a software reset and then reads the check code before the
  // controller is ready, so begin() rejects a controller that answers at 0x5A.
  g_touch.setPins(kTouchResPin, kTouchIrqPin);
  // Address order comes from the board header: the LilyGO has a CST226SE, the
  // Waveshare 1.85B a CST816S.
  g_ok = g_touch.begin(Wire, kTouchAddrFirst, kTouchSdaPin, kTouchSclPin);
  if (!g_ok) {
    g_ok = g_touch.begin(Wire, kTouchAddrSecond, kTouchSdaPin, kTouchSclPin);
  }
  if (!g_ok) {
    KMJ_LOG("touch begin failed");
    return false;
  }
  g_touch.setMaxCoordinates(kScreenW, kScreenH);
  g_touch.setSwapXY(kTouchSwapXY);
  g_touch.setMirrorXY(kTouchMirrorX, kTouchMirrorY);
  // Boards with a physical home button (kHomeButtonPin >= 0) do not use the
  // touch controller's home key; they poll the button in touch_read().
  if (kHomeButtonPin >= 0) {
    pinMode(kHomeButtonPin, INPUT_PULLUP);
    g_home_button.begin(digitalRead(kHomeButtonPin) == LOW, millis());
  } else {
    g_touch.setHomeButtonCallback(on_home, nullptr);
  }
  KMJ_LOG("touch model %s", g_touch.getModelName());
  return true;
}

bool touch_ready() { return g_ok; }

TouchSample touch_read() {
  TouchSample sample;
  sample.ok = g_ok;
  // Physical home button (Waveshare BOOT): debounced level, polled even when
  // the touch controller is absent so wake-from-screen-off still works.
  if (kHomeButtonPin >= 0) {
    g_home_button.update(digitalRead(kHomeButtonPin) == LOW, millis());
    sample.home = g_home_button.down();
  }
  if (!g_ok) {
    return sample;
  }
  g_home = false;
  int16_t x = -1;
  int16_t y = -1;
  i2c_lock("touch");
  const TouchPoints& points = g_touch.getTouchPoints();
  sample.down = points.hasPoints();
  if (sample.down) {
    const TouchPoint& point = points.getPoint(0);
    x = static_cast<int16_t>(point.x);
    y = static_cast<int16_t>(point.y);
  }
  sample.home = sample.home || g_home;
  i2c_unlock();
  sample.x = x;
  sample.y = y;
  return sample;
}

}  // namespace kmj
