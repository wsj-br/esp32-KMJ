#include "hal/touch_cst.hpp"

#include <Arduino.h>
#include <TouchDrvCST.hpp>
#include <Wire.h>

#include "app/debug_log.hpp"
#include "hal/i2c_lock.hpp"
#include "pins.hpp"

namespace kmj {
namespace {

TouchDrvCSTXXX g_touch;
bool g_ok = false;
bool g_home = false;

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
  // CST226 is ready, so begin() rejects a controller that answers at 0x5A.
  g_touch.setPins(kTouchResPin, kTouchIrqPin);
  g_ok = g_touch.begin(Wire, CST226SE_SLAVE_ADDRESS, kTouchSdaPin, kTouchSclPin);
  if (!g_ok) {
    g_ok = g_touch.begin(Wire, CST816_SLAVE_ADDRESS, kTouchSdaPin, kTouchSclPin);
  }
  if (!g_ok) {
    KMJ_LOG("touch begin failed");
    return false;
  }
  g_touch.setMaxCoordinates(kScreenW, kScreenH);
  g_touch.setSwapXY(true);
  g_touch.setMirrorXY(false, true);
  g_touch.setHomeButtonCallback(on_home, nullptr);
  KMJ_LOG("touch model %s", g_touch.getModelName());
  return true;
}

bool touch_ready() { return g_ok; }

TouchSample touch_read() {
  TouchSample sample;
  sample.ok = g_ok;
  if (!g_ok) {
    return sample;
  }
  g_home = false;
  int16_t x = -1;
  int16_t y = -1;
  i2c_lock();
  const TouchPoints& points = g_touch.getTouchPoints();
  sample.down = points.hasPoints();
  if (sample.down) {
    const TouchPoint& point = points.getPoint(0);
    x = static_cast<int16_t>(point.x);
    y = static_cast<int16_t>(point.y);
  }
  sample.home = g_home;
  i2c_unlock();
  sample.x = x;
  sample.y = y;
  return sample;
}

}  // namespace kmj
