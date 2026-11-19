#pragma once

#include <stddef.h>
#include <stdint.h>

// Widget geometry for the Waveshare 1.85B round 360x360 panel. Header-only
// and host-testable: the Unity test in firmware/test/test_logic checks every
// rect against the visible circle and the minimum touch sizes, which is the
// only layout verification possible without a UI simulator.
//
// Coordinates are LVGL screen coordinates for a 360x360 panel. Rects are
// inclusive-exclusive (x + w is one past the right edge), matching
// lv_obj_set_pos/lv_obj_set_size.

namespace kmj {
namespace layout_round {

struct RoundRect {
  int x;
  int y;
  int w;
  int h;
};

// Panel geometry and the keep-out margin that stays clear of the bezel.
constexpr int kRoundSize = 360;
constexpr int kRoundCenter = kRoundSize / 2;                       // 180
constexpr int kRoundRadius = kRoundSize / 2;                       // panel radius
constexpr int kRoundMargin = 6;                                    // keep-out from the bezel
constexpr int kRoundUsableRadius = kRoundRadius - kRoundMargin;    // 174

// Minimum interactive sizes enforced by the test. Full 44x44 targets do not
// fit the round bezel for every control; the larger dimension must reach 44
// and the smaller one 28, and LVGL's click area covers the rest.
constexpr int kMinTouchW = 44;
constexpr int kMinTouchH = 28;

constexpr int isqrt(int v) {
  if (v <= 0) {
    return 0;
  }
  int r = 1;
  while (r * r <= v) {
    r++;
  }
  return r - 1;
}

// Half width of the usable circle at row y; 0 outside the circle.
constexpr int chord_half_width(int y) {
  const int dy = y <= kRoundCenter ? kRoundCenter - y : y - kRoundCenter;
  if (dy > kRoundUsableRadius) {
    return 0;
  }
  return isqrt(kRoundUsableRadius * kRoundUsableRadius - dy * dy);
}

// True when all four corners of the rect are inside the usable circle.
constexpr bool rect_inside_circle(RoundRect r, int margin = 0) {
  const int lim = kRoundRadius - margin;
  const long long lim2 = static_cast<long long>(lim) * lim;
  const long long x1 = r.x - kRoundCenter;
  const long long x2 = r.x + r.w - kRoundCenter;
  const long long y1 = r.y - kRoundCenter;
  const long long y2 = r.y + r.h - kRoundCenter;
  const long long ax1 = x1 < 0 ? -x1 : x1;
  const long long ax2 = x2 < 0 ? -x2 : x2;
  const long long ay1 = y1 < 0 ? -y1 : y1;
  const long long ay2 = y2 < 0 ? -y2 : y2;
  const long long dx = ax1 > ax2 ? ax1 : ax2;
  const long long dy = ay1 > ay2 ? ay1 : ay2;
  return dx * dx + dy * dy <= lim2;
}

struct RoundWidget {
  const char* name;
  RoundRect rect;
  bool interactive;  // takes touches: enforced minimum size
};

// Status (built once on the top layer; visible on every screen).
constexpr RoundRect kWifi{77, 40, 144, 20};
constexpr RoundRect kBat{225, 40, 58, 20};
constexpr RoundRect kIp{77, 300, 206, 20};

// Home: big KM tile in the middle, Jiggler and Config side by side below.
constexpr RoundRect kHomeKm{60, 76, 240, 164};
constexpr RoundRect kHomeJig{72, 250, 104, 62};
constexpr RoundRect kHomeCfg{184, 250, 104, 62};

// KM Emulator screen. No back button: the BOOT button and the swipe-right
// gesture both go home.
constexpr RoundRect kKmTitle{80, 44, 200, 22};
constexpr RoundRect kKmState{50, 96, 160, 24};
constexpr RoundRect kKmMeta{50, 124, 160, 20};
constexpr RoundRect kKmCounts{50, 148, 160, 20};
constexpr RoundRect kKmTrace{50, 176, 192, 20};  // line origin
constexpr RoundRect kKmPaired{220, 88, 110, 22};
constexpr RoundRect kKmKey{220, 116, 100, 20};
constexpr RoundRect kKmPairBtn{86, 252, 100, 36};
constexpr RoundRect kKmDropBtn{194, 252, 100, 36};

// Mouse Jiggler screen.
constexpr RoundRect kJigTitle{80, 44, 200, 22};
constexpr RoundRect kJigState{60, 100, 180, 28};
constexpr RoundRect kJigCounts{60, 136, 240, 20};
constexpr RoundRect kJigRunBtn{56, 240, 108, 40};
constexpr RoundRect kJigNowBtn{172, 240, 84, 40};
constexpr RoundRect kJigNudgeBtn{264, 240, 48, 40};

// Configuration screen.
constexpr RoundRect kCfgTitle{70, 44, 220, 22};
constexpr RoundRect kCfgSetup{56, 96, 140, 34};
constexpr RoundRect kCfgTimeout{204, 96, 100, 34};
constexpr RoundRect kCfgReboot{56, 138, 112, 34};
constexpr RoundRect kCfgSlider{176, 142, 124, 24};  // drag control, not a tap target
constexpr RoundRect kCfgInfo{56, 180, 248, 88};
constexpr RoundRect kCfgStamp{80, 276, 200, 18};

constexpr RoundWidget kRoundWidgets[] = {
    {"status.wifi", kWifi, false},
    {"status.bat", kBat, false},
    {"status.ip", kIp, false},
    {"home.km", kHomeKm, true},
    {"home.jiggler", kHomeJig, true},
    {"home.config", kHomeCfg, true},
    {"km.title", kKmTitle, false},
    {"km.state", kKmState, false},
    {"km.meta", kKmMeta, false},
    {"km.counts", kKmCounts, false},
    {"km.trace", kKmTrace, false},
    {"km.paired", kKmPaired, false},
    {"km.last_key", kKmKey, false},
    {"km.pair_btn", kKmPairBtn, true},
    {"km.drop_btn", kKmDropBtn, true},
    {"jiggler.title", kJigTitle, false},
    {"jiggler.state", kJigState, false},
    {"jiggler.counts", kJigCounts, false},
    {"jiggler.run_btn", kJigRunBtn, true},
    {"jiggler.jiggle_btn", kJigNowBtn, true},
    {"jiggler.nudge_btn", kJigNudgeBtn, true},
    {"config.title", kCfgTitle, false},
    {"config.wifi_btn", kCfgSetup, true},
    {"config.timeout_btn", kCfgTimeout, true},
    {"config.reboot_btn", kCfgReboot, true},
    {"config.backlight_slider", kCfgSlider, false},
    {"config.info", kCfgInfo, false},
    {"config.stamp", kCfgStamp, false},
};

constexpr size_t kRoundWidgetCount = sizeof(kRoundWidgets) / sizeof(kRoundWidgets[0]);

}  // namespace layout_round
}  // namespace kmj
