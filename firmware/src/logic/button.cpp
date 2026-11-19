#include "logic/button.hpp"

namespace kmj {

namespace {
constexpr uint32_t kDebounceMs = 30;
}  // namespace

void Button::begin(bool pressed_now, uint32_t now_ms) {
  down_ = pressed_now;
  last_raw_ = pressed_now;
  raw_changed_ms_ = now_ms;
}

bool Button::update(bool pressed_raw, uint32_t now_ms) {
  if (pressed_raw != last_raw_) {
    last_raw_ = pressed_raw;
    raw_changed_ms_ = now_ms;
  } else if (pressed_raw != down_ && now_ms - raw_changed_ms_ >= kDebounceMs) {
    down_ = pressed_raw;
    if (down_) {
      return true;
    }
  }
  return false;
}

}  // namespace kmj
