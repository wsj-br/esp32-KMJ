#pragma once

#include <stdint.h>

namespace kmj {

struct PowerSample {
  bool ok = false;
  int battery_mv = 0;
  int battery_pct = -1;
  bool charging = false;
  bool vbus = false;
};

// Same open-circuit-voltage curve the SY6970 driver used; the BQ27220 gauge
// SOC is not trusted until its design capacity has been configured.
inline int percent_from_mv(int mv) {
  constexpr int kEmpty = 3300;
  constexpr int kFull = 4200;
  if (mv <= 0) {
    return -1;
  }
  if (mv <= kEmpty) {
    return 0;
  }
  if (mv >= kFull) {
    return 100;
  }
  return (mv - kEmpty) * 100 / (kFull - kEmpty);
}

// Implemented per board in pmu_sy6970.cpp / pmu_bq27220.cpp.
void pmu_init();
PowerSample pmu_read();

}  // namespace kmj
