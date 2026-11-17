#include "hal/backlight.hpp"

#include <Arduino.h>

#include "pins.hpp"

namespace kmj {
namespace {

uint8_t g_level = 0;

void busy_wait_ms(uint32_t ms) {
  const uint32_t start = micros();
  while (static_cast<uint32_t>(micros() - start) < ms * 1000u) {
  }
}

}  // namespace

void backlight_init() {
  pinMode(kBacklightPin, OUTPUT);
  backlight_set(kBacklightMax);
}

void backlight_set(uint8_t value) {
  if (value > kBacklightMax) {
    value = kBacklightMax;
  }
  if (value == 0) {
    digitalWrite(kBacklightPin, LOW);
    busy_wait_ms(3);
    g_level = 0;
    return;
  }
  if (g_level == 0) {
    digitalWrite(kBacklightPin, HIGH);
    g_level = kBacklightMax;
    delayMicroseconds(30);
  }
  const int from = kBacklightMax - g_level;
  const int to = kBacklightMax - value;
  const int pulses = (kBacklightMax + to - from) % kBacklightMax;
  for (int i = 0; i < pulses; i++) {
    digitalWrite(kBacklightPin, LOW);
    digitalWrite(kBacklightPin, HIGH);
  }
  g_level = value;
}

}  // namespace kmj
