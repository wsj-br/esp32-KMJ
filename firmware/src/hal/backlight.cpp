#include "hal/backlight.hpp"

#include <Arduino.h>
#include <math.h>

#include "pins.hpp"

namespace kmj {
namespace {

uint8_t g_level = 0;
bool g_ready = false;

void busy_wait_ms(uint32_t ms) {
  const uint32_t start = micros();
  while (static_cast<uint32_t>(micros() - start) < ms * 1000u) {
  }
}

#if defined(KMJ_BOARD_WS_185B)
constexpr uint32_t kPwmFreq = 5000;
uint8_t g_duty[kBacklightMax + 1];
bool g_pwm = false;
#endif

}  // namespace

void backlight_init() {
#if defined(KMJ_BOARD_TDISPLAY_S3)
  // V1.1 uses a 16-step constant-current driver driven with pulse counting.
  pinMode(kBacklightPin, OUTPUT);
  backlight_set(kBacklightMax);
  g_ready = true;
#elif defined(KMJ_BOARD_WS_185B)
  // Plain PWM backlight; map the shared 0..16 level scale onto 8-bit duty
  // with a gamma curve so low steps stay visible.
  for (int i = 0; i <= kBacklightMax; i++) {
    const float t = static_cast<float>(i) / static_cast<float>(kBacklightMax);
    int duty = static_cast<int>(powf(t, 1.6f) * 255.0f + 0.5f);
    if (i > 0 && duty < 8) {
      duty = 8;
    }
    if (duty > 255) {
      duty = 255;
    }
    g_duty[i] = static_cast<uint8_t>(duty);
  }
  g_pwm = ledcAttach(kBacklightPin, kPwmFreq, 8);
  if (!g_pwm) {
    pinMode(kBacklightPin, OUTPUT);
  }
  backlight_set(kBacklightMax);
  g_ready = true;
#endif
}

bool backlight_ready() { return g_ready; }
uint8_t backlight_level() { return g_level; }

void backlight_set(uint8_t value) {
  if (value > kBacklightMax) {
    value = kBacklightMax;
  }
#if defined(KMJ_BOARD_TDISPLAY_S3)
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
#elif defined(KMJ_BOARD_WS_185B)
  g_level = value;
  if (g_pwm) {
    ledcWrite(kBacklightPin, g_duty[value]);
  } else {
    digitalWrite(kBacklightPin, value > 0 ? HIGH : LOW);
  }
#endif
}

}  // namespace kmj
