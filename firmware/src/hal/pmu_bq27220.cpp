#include "hal/pmu.hpp"

#include <Wire.h>

#include "gauge/ti/GaugeBQ27220.hpp"

#include "hal/i2c_lock.hpp"
#include "pins.hpp"

#if defined(KMJ_BOARD_WS_185B)

namespace kmj {
namespace {

// Speaker amplifier enable: hold low so the unused audio path stays silent.
// pmu_init() runs on both boot paths (normal and Wi-Fi setup), which makes it
// the board's power-housekeeping hook.
void mute_amplifier() {
  if (kPaPin < 0) {
    return;
  }
  pinMode(kPaPin, OUTPUT);
  digitalWrite(kPaPin, LOW);
}

GaugeBQ27220 g_gauge;

}  // namespace

void pmu_init() {
  mute_amplifier();
  i2c_lock();
  // Wire is already running (touch_init came first); pass it without pins.
  const bool ok = g_gauge.begin(Wire);
  i2c_unlock();
  if (ok) {
    // One refresh so the first pmu_read() sees real data.
    i2c_lock();
    g_gauge.refresh();
    i2c_unlock();
  }
}

PowerSample pmu_read() {
  PowerSample sample;
  i2c_lock();
  const bool ok = g_gauge.refresh();
  const int mv = ok ? static_cast<int>(g_gauge.getVoltage()) : 0;
  const int ma = ok ? static_cast<int>(g_gauge.getCurrent()) : 0;
  i2c_unlock();
  if (!ok) {
    return sample;
  }
  sample.ok = true;
  sample.battery_mv = mv;
  sample.battery_pct = percent_from_mv(mv);
  sample.charging = ma > 1;
  // The gauge has no VBUS pin. Treat charging as USB present, and a missing
  // or dead battery (near-zero voltage) as running from USB only.
  sample.vbus = sample.charging || mv < 200;
  return sample;
}

}  // namespace kmj

#endif  // KMJ_BOARD_WS_185B
