#pragma once

#include <stdint.h>

namespace kmj {

// Debounced push-button sampler for an active-low GPIO (pressed reads low).
// Feed the raw pin level on every poll; the debounced level is what callers
// should treat as the button state, and update() returns true exactly once
// per confirmed press (falling edge).
class Button {
 public:
  // Seed the state so a button held during boot does not fire a phantom press.
  void begin(bool pressed_now, uint32_t now_ms);

  // pressed_raw: true when the pin reads low. now_ms: millis().
  // Returns true on the poll that confirms a new press.
  bool update(bool pressed_raw, uint32_t now_ms);

  bool down() const { return down_; }

 private:
  bool down_ = false;
  bool last_raw_ = false;
  uint32_t raw_changed_ms_ = 0;
};

}  // namespace kmj
