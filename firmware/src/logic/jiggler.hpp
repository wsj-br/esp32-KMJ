#pragma once

#include <stdint.h>

namespace kmj {

// Arduino-style random: returns a value in [lo, hi).
using RandFn = long (*)(void* ctx, long lo, long hi_exclusive);

// UI-independent port of the original mouse-jiggler motion.
class Jiggler {
 public:
  void set_random(RandFn fn, void* ctx);
  void start(uint32_t now_ms);
  void stop();
  void trigger_jiggle();
  void trigger_nudge();

  // True when a relative delta is waiting. The same delta is returned until ack().
  bool poll(uint32_t now_ms, int& dx, int& dy);
  void ack();

  bool enabled() const { return enabled_; }
  bool time_up() const { return time_up_; }
  bool moving() const { return moving_; }
  bool nudging() const { return moving_ && nudge_; }
  uint32_t jiggle_count() const { return jiggle_count_; }
  int leg() const { return leg_index_; }
  int leg_count() const { return leg_count_; }
  int leg_dx() const { return total_dx_; }
  int leg_dy() const { return total_dy_; }
  // -1 when there is no countdown to show.
  int countdown_bucket_s(uint32_t now_ms) const;
  uint32_t run_left_s(uint32_t now_ms) const;

 private:
  void schedule(uint32_t now_ms);
  void begin_legs(const int* dx, const int* dy, const int* steps, int count, int gap_ms, bool nudge);
  void load_leg(int index);
  void begin_random();
  void begin_nudge();
  void enforce_limit(uint32_t now_ms);
  bool step(uint32_t now_ms, int& dx, int& dy);

  RandFn rand_ = nullptr;
  void* rand_ctx_ = nullptr;
  bool enabled_ = false;
  bool time_up_ = false;
  bool moving_ = false;
  bool nudge_ = false;
  bool have_delta_ = false;
  int delta_x_ = 0;
  int delta_y_ = 0;
  uint32_t pending_now_ = 0;
  uint32_t start_ms_ = 0;
  uint32_t next_ms_ = 0;
  uint32_t jiggle_count_ = 0;

  static constexpr int kMaxLegs = 8;
  int leg_dx_[kMaxLegs]{};
  int leg_dy_[kMaxLegs]{};
  int leg_steps_[kMaxLegs]{};
  int leg_count_ = 0;
  int leg_index_ = 0;
  int gap_ms_ = 0;
  int total_dx_ = 0;
  int total_dy_ = 0;
  int steps_ = 0;
  int prev_x_ = 0;
  int prev_y_ = 0;
  int i_ = 0;
  uint32_t last_step_ms_ = 0;
  bool waiting_gap_ = false;
  uint32_t leg_done_ms_ = 0;
};

}  // namespace kmj
