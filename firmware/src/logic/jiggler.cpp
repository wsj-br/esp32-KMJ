#include "logic/jiggler.hpp"

#include <math.h>
#include <string.h>

namespace kmj {
namespace {

constexpr int kJiggleMinPx = 10;
constexpr int kJiggleMaxPx = 20;
constexpr int kJiggleMinSteps = 6;
constexpr int kJiggleMaxSteps = 12;
constexpr int kJiggleReverseDelayMs = 30;
constexpr int kMinPauseMs = 30000;
constexpr int kMaxPauseMs = 90000;
constexpr int kStepInterval = 10;
constexpr float kHoursToRun = 8.0f;
constexpr int kNudgeDx[] = {0, -20, 0, 40, 0, -20, 0};
constexpr int kNudgeDy[] = {-20, 0, 40, 0, -40, 0, 20};

float ease_cosine(float t) { return 0.5f * (1.0f - cosf(3.14159265f * t)); }

long fallback_rand(void*, long lo, long hi) { return lo; }

}  // namespace

void Jiggler::set_random(RandFn fn, void* ctx) {
  rand_ = fn != nullptr ? fn : fallback_rand;
  rand_ctx_ = ctx;
}

void Jiggler::start(uint32_t now_ms) {
  if (rand_ == nullptr) {
    rand_ = fallback_rand;
  }
  enabled_ = true;
  time_up_ = false;
  moving_ = false;
  have_delta_ = false;
  start_ms_ = now_ms;
  schedule(now_ms);
}

void Jiggler::stop() {
  enabled_ = false;
  moving_ = false;
  have_delta_ = false;
}

void Jiggler::schedule(uint32_t now_ms) {
  const long pause = rand_(rand_ctx_, kMinPauseMs, kMaxPauseMs);
  next_ms_ = now_ms + static_cast<uint32_t>(pause);
}

void Jiggler::load_leg(int index) {
  total_dx_ = leg_dx_[index];
  total_dy_ = leg_dy_[index];
  steps_ = leg_steps_[index];
  prev_x_ = 0;
  prev_y_ = 0;
  i_ = 0;
  last_step_ms_ = 0;
  waiting_gap_ = false;
}

void Jiggler::begin_legs(const int* dx, const int* dy, const int* steps, int count, int gap_ms, bool nudge) {
  if (count > kMaxLegs) {
    count = kMaxLegs;
  }
  leg_count_ = count;
  leg_index_ = 0;
  gap_ms_ = gap_ms;
  nudge_ = nudge;
  for (int i = 0; i < count; i++) {
    leg_dx_[i] = dx[i];
    leg_dy_[i] = dy[i];
    leg_steps_[i] = steps[i];
  }
  load_leg(0);
  moving_ = true;
  have_delta_ = false;
}

void Jiggler::begin_random() {
  const int distance = static_cast<int>(rand_(rand_ctx_, kJiggleMinPx, kJiggleMaxPx + 1));
  static constexpr int kAxis[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
  const int axis = static_cast<int>(rand_(rand_ctx_, 0, 4));
  const int dx = kAxis[axis][0] * distance;
  const int dy = kAxis[axis][1] * distance;
  const int span = kJiggleMaxPx - kJiggleMinPx;
  const int steps = kJiggleMinSteps + (distance - kJiggleMinPx) * (kJiggleMaxSteps - kJiggleMinSteps) / span;
  const int leg_dx[2] = {dx, -dx};
  const int leg_dy[2] = {dy, -dy};
  const int leg_steps[2] = {steps, steps};
  begin_legs(leg_dx, leg_dy, leg_steps, 2, kJiggleReverseDelayMs, false);
}

void Jiggler::begin_nudge() {
  int steps[7];
  for (int i = 0; i < 7; i++) {
    const int mag = (kNudgeDx[i] < 0 ? -kNudgeDx[i] : kNudgeDx[i]) + (kNudgeDy[i] < 0 ? -kNudgeDy[i] : kNudgeDy[i]);
    steps[i] = mag / 2;
  }
  begin_legs(kNudgeDx, kNudgeDy, steps, 7, 0, true);
}

void Jiggler::trigger_jiggle() { begin_random(); }

void Jiggler::trigger_nudge() { begin_nudge(); }

void Jiggler::enforce_limit(uint32_t now_ms) {
  if (!enabled_ || time_up_) {
    return;
  }
  const uint32_t elapsed = now_ms - start_ms_;
  const uint32_t limit = static_cast<uint32_t>(kHoursToRun * 60.0f * 60.0f * 1000.0f);
  if (elapsed < limit) {
    return;
  }
  time_up_ = true;
  enabled_ = false;
  moving_ = false;
  have_delta_ = false;
}

bool Jiggler::step(uint32_t now_ms, int& dx, int& dy) {
  if (waiting_gap_) {
    if ((now_ms - leg_done_ms_) < static_cast<uint32_t>(gap_ms_)) {
      return false;
    }
    leg_index_++;
    load_leg(leg_index_);
  }
  if (i_ != 0 && (now_ms - last_step_ms_) < static_cast<uint32_t>(kStepInterval)) {
    return false;
  }
  const int next_index = i_ + 1;
  const float t = float(next_index) / float(steps_);
  const float e = ease_cosine(t);
  const int x_pos = static_cast<int>(lroundf(total_dx_ * e));
  const int y_pos = static_cast<int>(lroundf(total_dy_ * e));
  dx = x_pos - prev_x_;
  dy = y_pos - prev_y_;
  delta_x_ = dx;
  delta_y_ = dy;
  pending_now_ = now_ms;
  have_delta_ = true;
  return true;
}

bool Jiggler::poll(uint32_t now_ms, int& dx, int& dy) {
  enforce_limit(now_ms);
  if (have_delta_) {
    dx = delta_x_;
    dy = delta_y_;
    return true;
  }
  if (!moving_) {
    if (enabled_ && now_ms >= next_ms_) {
      begin_random();
    } else {
      return false;
    }
  }
  if (!step(now_ms, dx, dy)) {
    return false;
  }
  return true;
}

void Jiggler::ack() {
  if (!have_delta_) {
    return;
  }
  have_delta_ = false;
  prev_x_ += delta_x_;
  prev_y_ += delta_y_;
  i_ += 1;
  last_step_ms_ = pending_now_;
  if (i_ >= steps_) {
    if (leg_index_ + 1 < leg_count_) {
      if (gap_ms_ > 0) {
        waiting_gap_ = true;
        leg_done_ms_ = pending_now_;
        return;
      }
      leg_index_++;
      load_leg(leg_index_);
      return;
    }
    jiggle_count_++;
    moving_ = false;
    schedule(pending_now_);
  }
}

int Jiggler::countdown_bucket_s(uint32_t now_ms) const {
  if (!enabled_ || moving_) {
    return -1;
  }
  if (now_ms >= next_ms_) {
    return -1;
  }
  const uint32_t remain_s = (next_ms_ - now_ms + 999u) / 1000u;
  const int bucket = static_cast<int>(((remain_s + 4u) / 5u) * 5u);
  return bucket > 0 ? bucket : -1;
}

uint32_t Jiggler::run_left_s(uint32_t now_ms) const {
  const uint32_t limit = static_cast<uint32_t>(kHoursToRun * 60.0f * 60.0f * 1000.0f);
  const uint32_t elapsed = now_ms - start_ms_;
  if (elapsed >= limit) {
    return 0;
  }
  // Same 5 s step as the next-jiggle countdown. An 8 h readout does not need a 1 s tick.
  const uint32_t remain_s = (limit - elapsed + 999u) / 1000u;
  return ((remain_s + 4u) / 5u) * 5u;
}

}  // namespace kmj
