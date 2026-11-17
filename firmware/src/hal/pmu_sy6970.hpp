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

void pmu_init();
PowerSample pmu_read();

}  // namespace kmj
