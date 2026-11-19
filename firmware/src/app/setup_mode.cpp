#include "app/setup_mode.hpp"

#include <Arduino.h>
#include <esp_attr.h>
#include <esp_system.h>
#include <string.h>

#include "app/debug_log.hpp"
#include "app/improv_ble.hpp"
#include "app/net_wifi.hpp"
#include "app/settings.hpp"
#include "hal/backlight.hpp"
#include "hal/display_lgfx.hpp"
#include "hal/pmu.hpp"
#include "hal/touch_cst.hpp"

namespace kmj {
namespace {

constexpr uint32_t kFlagMagic = 0x4B53544D;
constexpr uint32_t kTotalMs = 5u * 60u * 1000u;
constexpr uint32_t kFailHoldMs = 30000;
constexpr uint32_t kDoneHoldMs = 2500;
constexpr uint32_t kIdentifyMs = 800;

RTC_NOINIT_ATTR uint32_t g_flag;

enum class Phase : uint8_t { Running, Failed, Done };
enum class Buttons : uint8_t { None, Cancel, AllowDeny };

struct Rect {
  int x;
  int y;
  int w;
  int h;
  bool hit(int px, int py) const { return px >= x && px < x + w && py >= y && py < y + h; }
};

#if defined(KMJ_BOARD_TDISPLAY_S3)
constexpr Rect kAllow{24, 146, 200, 58};
constexpr Rect kDeny{256, 146, 200, 58};
constexpr Rect kCancel{140, 146, 200, 58};
#else
// Waveshare 1.85B: round 360x360 panel; compact buttons inside the bezel.
constexpr Rect kAllow{56, 210, 120, 40};
constexpr Rect kDeny{184, 210, 120, 40};
constexpr Rect kCancel{120, 210, 120, 40};
#endif

volatile bool g_identify = false;

void button(LGFX& tft, const Rect& r, uint16_t fill, const char* text) {
  tft.fillRoundRect(r.x, r.y, r.w, r.h, 8, fill);
  tft.setTextColor(TFT_WHITE, fill);
  tft.setTextDatum(lgfx::middle_center);
  tft.drawString(text, r.x + r.w / 2, r.y + r.h / 2);
}

void paint(LGFX& tft, const char* line, const char* hint, Buttons buttons) {
  const uint16_t amber = tft.color565(0xF0, 0xA2, 0x02);
  const uint16_t dim = tft.color565(0x8A, 0x96, 0xA3);
  tft.fillScreen(TFT_BLACK);
#if defined(KMJ_BOARD_TDISPLAY_S3)
  tft.setTextDatum(lgfx::top_left);
  tft.setFont(&lgfx::fonts::DejaVu24);
  tft.setTextColor(tft.color565(0xC9, 0xD2, 0xDC), TFT_BLACK);
  tft.drawString("Wi-Fi setup", 20, 16);
  tft.setFont(&lgfx::fonts::DejaVu18);
  tft.setTextDatum(lgfx::middle_center);
  tft.setTextColor(amber, TFT_BLACK);
  tft.drawString(line, tft.width() / 2, 78);
  tft.setTextColor(dim, TFT_BLACK);
  tft.drawString(hint, tft.width() / 2, 112);
#else
  // Round panel: centered text down the vertical axis.
  tft.setTextDatum(lgfx::middle_center);
  tft.setFont(&lgfx::fonts::DejaVu24);
  tft.setTextColor(tft.color565(0xC9, 0xD2, 0xDC), TFT_BLACK);
  tft.drawString("Wi-Fi setup", tft.width() / 2, 70);
  tft.setFont(&lgfx::fonts::DejaVu18);
  tft.setTextColor(amber, TFT_BLACK);
  tft.drawString(line, tft.width() / 2, 150);
  tft.setTextColor(dim, TFT_BLACK);
  tft.drawString(hint, tft.width() / 2, 190);
#endif
  if (buttons == Buttons::AllowDeny) {
    button(tft, kAllow, tft.color565(0x2E, 0x8B, 0x57), "Allow");
    button(tft, kDeny, tft.color565(0xB0, 0x3A, 0x3A), "Deny");
  } else if (buttons == Buttons::Cancel) {
    button(tft, kCancel, tft.color565(0x3A, 0x44, 0x50), "Cancel");
  }
}

[[noreturn]] void restart_normal() {
  KMJ_LOG("setup: back to normal firmware");
  delay(100);
  ESP.restart();
  while (true) {
    delay(1000);
  }
}

}  // namespace

bool setup_mode_flagged() { return g_flag == kFlagMagic && esp_reset_reason() == ESP_RST_SW; }

bool setup_mode_boot() {
  const bool flagged = setup_mode_flagged();
  g_flag = 0;
  return flagged;
}

void setup_mode_restart() {
  g_flag = kFlagMagic;
  ESP.restart();
}

void setup_mode_run() {
  KMJ_LOG("setup: Wi-Fi setup boot");
  KMJ_LOG_HEAP("setup boot");
  backlight_init();
  display_init();
  touch_init();
  pmu_init();
  settings_load();
  backlight_set(static_cast<uint8_t>(settings().backlight));
  // The Wi-Fi driver runs for scans and the join, but never reconnects on its own.
  net_begin(settings().hostname);
  net_hold(true);
  improv_set_identify([]() { g_identify = true; });
  KMJ_LOG_HEAP("setup net");

  LGFX& tft = display();
  paint(tft, "Starting Bluetooth", "", Buttons::None);

  Phase phase = improv_start(true) ? Phase::Running : Phase::Failed;
  KMJ_LOG("setup: improv_start -> %s", phase == Phase::Running ? "ok" : "failed");
  KMJ_LOG_HEAP("setup ble");
  const uint32_t began = millis();
  uint32_t phase_at = began;
  uint32_t identify_until = 0;
  bool was_down = false;
  bool was_boot_down = false;
  char drawn[96] = {};

  while (true) {
    const uint32_t now = millis();
    char ssid[33];
    char pass[65];
    if (improv_take_wifi(ssid, sizeof(ssid), pass, sizeof(pass))) {
      settings_save_wifi(ssid, pass);
      net_connect(ssid, pass);
    }
    net_poll();
    improv_poll(now);

    const TouchSample sample = touch_read();
    // The BOOT button (home button) also dismisses the failed screen; the
    // Allow/Deny choice still needs the touch panel.
    const bool boot_down = kHomeButtonPin >= 0 && digitalRead(kHomeButtonPin) == LOW;
    const bool boot_pressed = boot_down && !was_boot_down;
    was_boot_down = boot_down;
    const bool pressed = (sample.ok && sample.down && !was_down) || boot_pressed;
    was_down = sample.ok && sample.down;

    if (g_identify) {
      g_identify = false;
      identify_until = now + kIdentifyMs;
      backlight_set(4);
    } else if (identify_until != 0 && static_cast<int32_t>(now - identify_until) >= 0) {
      identify_until = 0;
      backlight_set(static_cast<uint8_t>(settings().backlight));
    }

    const char* line = "";
    const char* hint = "";
    Buttons buttons = Buttons::None;
    if (phase == Phase::Running) {
      if (!improv_active()) {
        phase = Phase::Done;
        phase_at = now;
      } else if (now - began > kTotalMs) {
        improv_stop();
        phase = Phase::Done;
        phase_at = now;
      } else if (pressed) {
        if (improv_needs_auth()) {
          if (kAllow.hit(sample.x, sample.y)) {
            improv_authorize(true);
          } else if (kDeny.hit(sample.x, sample.y)) {
            improv_authorize(false);
          }
        } else if (kCancel.hit(sample.x, sample.y)) {
          improv_stop();
        }
      }
    } else if (phase == Phase::Failed) {
      if (pressed || now - phase_at > kFailHoldMs) {
        restart_normal();
      }
    } else if (now - phase_at > kDoneHoldMs) {
      restart_normal();
    }

    if (phase == Phase::Running) {
      const char* status = improv_status_line();
      line = status != nullptr ? status : "Starting Bluetooth";
      if (improv_needs_auth()) {
        hint = "Tap Allow, then use the KMJ app";
        buttons = Buttons::AllowDeny;
      } else {
        buttons = Buttons::Cancel;
      }
    } else if (phase == Phase::Failed) {
      line = improv_fail_text()[0] != '\0' ? improv_fail_text() : "Bluetooth did not start";
      hint = "Tap to go back";
    } else {
      const char* status = improv_status_line();
      line = status != nullptr ? status : "Wi-Fi setup finished";
      hint = "Returning to KMJ";
    }

    char signature[96];
    snprintf(signature, sizeof(signature), "%s|%s|%d", line, hint, static_cast<int>(buttons));
    if (strcmp(signature, drawn) != 0) {
      snprintf(drawn, sizeof(drawn), "%s", signature);
      paint(tft, line, hint, buttons);
    }
    delay(20);
  }
}

}  // namespace kmj

// Arduino releases the Bluetooth memory at boot unless this returns true.
// Only the Wi-Fi setup boot needs it; the normal firmware gets that RAM back.
extern "C" bool bleInUse(void) { return kmj::setup_mode_flagged(); }
