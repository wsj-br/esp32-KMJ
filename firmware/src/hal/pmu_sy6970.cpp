#include "hal/pmu_sy6970.hpp"

#include <Wire.h>

#include "hal/i2c_lock.hpp"
#include "pins.hpp"

namespace kmj {
namespace {

constexpr uint8_t kReg07 = 0x07;
constexpr uint8_t kReg02 = 0x02;
constexpr uint8_t kReg0B = 0x0B;
constexpr uint8_t kReg0E = 0x0E;
constexpr uint8_t kStatDis = 1 << 6;
constexpr uint8_t kWatchdogMask = 0x30;

bool write_reg(uint8_t reg, uint8_t value) {
  Wire.beginTransmission(kPmuAddr);
  Wire.write(reg);
  Wire.write(value);
  return Wire.endTransmission() == 0;
}

int read_reg(uint8_t reg) {
  Wire.beginTransmission(kPmuAddr);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) {
    return -1;
  }
  if (Wire.requestFrom(kPmuAddr, static_cast<uint8_t>(1)) != 1) {
    return -1;
  }
  return Wire.read();
}

int percent_from_mv(int mv) {
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

}  // namespace

void pmu_init() {
  const int reg07 = read_reg(kReg07);
  if (reg07 >= 0) {
    const uint8_t value = static_cast<uint8_t>((reg07 | kStatDis) & ~kWatchdogMask);
    write_reg(kReg07, value);
  }
  const int reg02 = read_reg(kReg02);
  if (reg02 >= 0) {
    // Continuous ADC (CONV_RATE) plus a conversion start. SY6970 follows the BQ25895 map.
    write_reg(kReg02, static_cast<uint8_t>(reg02 | 0xC0));
  }
}

PowerSample pmu_read() {
  PowerSample sample;
  i2c_lock();
  const int status = read_reg(kReg0B);
  const int bat = read_reg(kReg0E);
  i2c_unlock();
  if (status < 0 && bat < 0) {
    return sample;
  }
  sample.ok = true;
  if (status >= 0) {
    const int vbus_stat = (status >> 5) & 0x7;
    const int chrg_stat = (status >> 3) & 0x3;
    sample.vbus = vbus_stat != 0;
    sample.charging = chrg_stat == 1 || chrg_stat == 2;
  }
  if (bat >= 0) {
    const int raw = bat & 0x7f;
    sample.battery_mv = raw == 0 ? 0 : raw * 20 + 2304;
    sample.battery_pct = percent_from_mv(sample.battery_mv);
  }
  return sample;
}

}  // namespace kmj
