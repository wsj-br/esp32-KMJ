#include "ui/ui.hpp"

#include <stdio.h>
#include <string.h>

#include <lvgl.h>

#include "pins.hpp"
#include "app/debug_log.hpp"
#include "ui/ui_internal.hpp"

namespace kmj {

namespace {

Mode g_mode = Mode::Home;
bool g_nav_pending = false;
Mode g_nav_mode = Mode::Home;
bool g_screen_on = true;
uint32_t g_activity = 0;
uint32_t g_suppress_until = 0;
TouchSample g_sample;
bool g_home_down = false;
uint32_t g_home_t0 = 0;
bool g_home_long = false;
uint32_t g_home_lock = 0;
bool g_wait_release = false;

}  // namespace

Host* g_host = nullptr;
lv_obj_t* g_wifi = nullptr;
lv_obj_t* g_ip = nullptr;
lv_obj_t* g_bat = nullptr;
lv_obj_t* g_km_state = nullptr;
lv_obj_t* g_km_meta = nullptr;
lv_obj_t* g_km_counts = nullptr;
lv_obj_t* g_km_key = nullptr;
lv_obj_t* g_km_paired = nullptr;
lv_obj_t* g_km_pair_btn = nullptr;
lv_obj_t* g_km_pair_label = nullptr;
lv_obj_t* g_line = nullptr;
lv_obj_t* g_jig_state = nullptr;
lv_obj_t* g_jig_left = nullptr;
lv_obj_t* g_jig_sent = nullptr;
lv_obj_t* g_jig_jiggles = nullptr;
lv_obj_t* g_run_btn = nullptr;
lv_obj_t* g_run_label = nullptr;
lv_obj_t* g_usb_label = nullptr;
lv_obj_t* g_timeout_label = nullptr;
lv_obj_t* g_home_km = nullptr;
lv_obj_t* g_home_jig = nullptr;
lv_obj_t* g_home_cfg = nullptr;
lv_obj_t* g_cfg_info = nullptr;
lv_obj_t* g_cfg_setup = nullptr;
lv_obj_t* g_cfg_forget = nullptr;
lv_obj_t* g_pair = nullptr;
lv_obj_t* g_pair_code = nullptr;
lv_point_precise_t g_points[48];

void go(Mode mode) {
  if (g_host == nullptr) {
    return;
  }
  if (!g_nav_pending && mode == g_mode) {
    return;
  }
  g_nav_mode = mode;
  g_nav_pending = true;
}

void apply_nav() {
  if (!g_nav_pending || g_host == nullptr) {
    return;
  }
  const Mode mode = g_nav_mode;
  g_nav_pending = false;
  if (mode == g_mode) {
    return;
  }
  g_mode = mode;
  lv_obj_t* old = lv_screen_active();
  KMJ_LOG("nav %d: build", static_cast<int>(mode));
  clear_screen_ptrs();
  lv_obj_t* scr = nullptr;
  Cmd cmd = Cmd::EnterHome;
  if (mode == Mode::Home) {
    scr = make_home();
    cmd = Cmd::EnterHome;
  } else if (mode == Mode::Km) {
    scr = make_km();
    cmd = Cmd::EnterKm;
  } else if (mode == Mode::Jiggler) {
    scr = make_jiggler();
    cmd = Cmd::EnterJiggler;
  } else {
    scr = make_config();
    cmd = Cmd::EnterConfig;
  }
  g_host->command(cmd, 0);
  KMJ_LOG("nav %d: load", static_cast<int>(mode));
  lv_screen_load(scr);
  if (old != nullptr && old != scr) {
    lv_obj_delete(old);
  }
  KMJ_LOG("nav %d: done", static_cast<int>(mode));
}

void show(lv_obj_t* obj, bool visible) {
  if (obj == nullptr) {
    return;
  }
  lv_obj_set_hidden(obj, !visible);
}

void paint_status(const Model& model) {
  char buf[64];
  if (model.wifi_connected) {
    snprintf(buf, sizeof(buf), "%s %ddB", model.wifi_ssid[0] ? model.wifi_ssid : "link", model.rssi);
  } else if (model.wifi_saved) {
    snprintf(buf, sizeof(buf), "Wi-Fi ...");
  } else {
    snprintf(buf, sizeof(buf), "Wi-Fi off");
  }
  lv_obj_set_style_text_color(g_wifi, lv_color_hex(kDim), 0);
  lv_label_set_text(g_wifi, buf);
  lv_label_set_text(g_ip, model.ip);
  if (model.vbus) {
    snprintf(buf, sizeof(buf), "USB");
  } else if (model.battery_pct >= 0) {
    snprintf(buf, sizeof(buf), "%d%%", model.battery_pct);
  } else {
    snprintf(buf, sizeof(buf), "--");
  }
  lv_label_set_text(g_bat, buf);
}

void paint_trace(const Model& model) {
  if (g_line == nullptr) {
    return;
  }
  for (int i = 0; i < 48; i++) {
    const int src = (model.trace_i + i) % 48;
    g_points[i].x = i * 4;
    g_points[i].y = 20 - model.trace[src];
  }
  lv_line_set_points(g_line, g_points, 48);
}

void paint_screen(const Model& model) {
  char buf[240];
  if (g_home_km != nullptr) {
    const char* state = "idle";
    if (model.km_link == KmLink::Capturing) state = model.km_client[0] ? model.km_client : "capturing";
    else if (model.km_link == KmLink::Connected) state = "connected";
    else if (model.km_link == KmLink::Listening) state = "listening";
    lv_label_set_text(g_home_km, state);
  }
  if (g_home_jig != nullptr) {
    if (!model.jiggler_run) lv_label_set_text(g_home_jig, model.time_up ? "time up" : "stopped");
    else if (model.nudge) lv_label_set_text(g_home_jig, "nudge");
    else if (model.jiggler_moving) lv_label_set_text(g_home_jig, "moving");
    else if (model.next_jiggle_s > 0) {
      snprintf(buf, sizeof(buf), "next %ds", model.next_jiggle_s);
      lv_label_set_text(g_home_jig, buf);
    } else lv_label_set_text(g_home_jig, "run");
  }
  if (g_home_cfg != nullptr) {
    if (model.vbus) snprintf(buf, sizeof(buf), "%s  USB", model.ip);
    else if (model.battery_pct >= 0) snprintf(buf, sizeof(buf), "%s  %d%%", model.ip, model.battery_pct);
    else snprintf(buf, sizeof(buf), "%s", model.ip);
    lv_label_set_text(g_home_cfg, buf);
  }
  if (g_km_state != nullptr) {
    const char* state = "Listening";
    if (model.km_link == KmLink::Capturing) state = "Capturing";
    else if (model.km_link == KmLink::Connected) state = "Connected";
    else if (model.km_link == KmLink::Off) state = "Offline";
    lv_label_set_text(g_km_state, state);
    snprintf(buf, sizeof(buf), "%s  %s  %lums", model.km_client, model.km_addr, static_cast<unsigned long>(model.km_latency_ms));
    lv_label_set_text(g_km_meta, buf);
    snprintf(buf, sizeof(buf), "keys %lu   move %lu", static_cast<unsigned long>(model.km_keys),
             static_cast<unsigned long>(model.km_mouse));
    lv_label_set_text(g_km_counts, buf);
    snprintf(buf, sizeof(buf), "%s", model.last_key);
    lv_label_set_text(g_km_key, buf);
    paint_trace(model);
  }
  if (g_km_paired != nullptr) {
    if (model.pair_armed) {
      lv_label_set_text(g_km_paired, "Waiting to pair");
      lv_obj_set_style_text_color(g_km_paired, lv_color_hex(kCyan), 0);
    } else if (model.peer_count > 0) {
      if (model.peer_count == 1) {
        snprintf(buf, sizeof(buf), "Paired  %s", model.peers[0]);
      } else {
        snprintf(buf, sizeof(buf), "Paired  %d desktops", model.peer_count);
      }
      lv_label_set_text(g_km_paired, buf);
      lv_obj_set_style_text_color(g_km_paired, lv_color_hex(kCyan), 0);
    } else {
      lv_label_set_text(g_km_paired, "Not paired");
      lv_obj_set_style_text_color(g_km_paired, lv_color_hex(kDim), 0);
    }
  }
  if (g_km_pair_btn != nullptr && g_km_pair_label != nullptr) {
    const bool armed = model.pair_armed;
    lv_label_set_text(g_km_pair_label, armed ? "Waiting" : "Pair");
    lv_obj_set_style_text_color(g_km_pair_label, lv_color_hex(armed ? kCyan : kText), 0);
    lv_obj_set_style_bg_color(g_km_pair_btn, lv_color_hex(armed ? 0x143F3A : kPanel), 0);
  }
  if (g_jig_state != nullptr) {
    if (model.nudge) {
      snprintf(buf, sizeof(buf), "nudge %d/%d", model.nudge_leg, model.nudge_legs);
    } else if (!model.jiggler_run) {
      snprintf(buf, sizeof(buf), "%s", model.time_up ? "Stopped, 8h elapsed" : "Stopped");
    } else if (model.next_jiggle_s > 0) {
      snprintf(buf, sizeof(buf), "Run  next %ds", model.next_jiggle_s);
    } else {
      snprintf(buf, sizeof(buf), "Run");
    }
    lv_label_set_text(g_jig_state, buf);
    const uint32_t left = model.run_left_s;
    snprintf(buf, sizeof(buf), "left %lu:%02lu", static_cast<unsigned long>(left / 60),
             static_cast<unsigned long>(left % 60));
    lv_label_set_text(g_jig_left, buf);
    snprintf(buf, sizeof(buf), "sent %lu", static_cast<unsigned long>(model.hid_tx));
    lv_label_set_text(g_jig_sent, buf);
    snprintf(buf, sizeof(buf), "jiggles %lu", static_cast<unsigned long>(model.jiggle_count));
    lv_label_set_text(g_jig_jiggles, buf);
  }
  if (g_run_btn != nullptr && g_run_label != nullptr) {
    const bool running = model.jiggler_run;
    lv_label_set_text(g_run_label, running ? "running" : "stopped");
    lv_obj_set_style_text_color(g_run_label, lv_color_hex(running ? 0x7DCE6A : 0x8E97A3), 0);
    lv_obj_set_style_bg_color(g_run_btn, lv_color_hex(running ? 0x1A3D2E : 0x2A2E34), 0);
  }
  if (g_usb_label != nullptr) {
    lv_label_set_text(g_usb_label, model.usb_wanted ? "USB on" : "USB off");
    lv_obj_set_style_text_color(g_usb_label, lv_color_hex(model.usb_wanted ? 0x7DCE6A : 0x8E97A3), 0);
  }
  if (g_timeout_label != nullptr) {
    const char* text = timeout_caption(model.screen_off_sec);
    if (text != nullptr) {
      lv_label_set_text(g_timeout_label, text);
    } else {
      snprintf(buf, sizeof(buf), "Timeout %ds", model.screen_off_sec);
      lv_label_set_text(g_timeout_label, buf);
    }
  }
  if (g_cfg_info != nullptr) {
    char power[48];
    if (model.battery_mv >= 3000) {
      snprintf(power, sizeof(power), "%d.%02d V  %d%%", model.battery_mv / 1000, (model.battery_mv % 1000) / 10,
               model.battery_pct);
    } else {
      snprintf(power, sizeof(power), "no battery");
    }
    if (model.setup_fail) {
      snprintf(buf, sizeof(buf), "%s\nBattery  %s%s\n%s  rssi %d\nheap %lu  up %lus",
               model.setup_note[0] ? model.setup_note : "Wi-Fi setup failed", power,
               model.charging ? "  charging" : "", model.wifi_connected ? model.wifi_ssid : "Wi-Fi off", model.rssi,
               static_cast<unsigned long>(model.heap_free), static_cast<unsigned long>(model.uptime_s));
    } else {
      snprintf(buf, sizeof(buf), "%s\nBattery  %s%s\n%s  rssi %d\nheap %lu  up %lus",
               model.vbus ? "Power  USB" : "Power  battery", power, model.charging ? "  charging" : "",
               model.wifi_connected ? model.wifi_ssid : "Wi-Fi off", model.rssi,
               static_cast<unsigned long>(model.heap_free), static_cast<unsigned long>(model.uptime_s));
    }
    lv_label_set_text(g_cfg_info, buf);
    lv_obj_set_style_text_color(g_cfg_info, lv_color_hex(model.setup_fail ? kAmber : kDim), 0);
  }
  show(g_cfg_setup, !model.wifi_connected);
  show(g_cfg_forget, model.wifi_connected);
}

void on_home(bool down, uint32_t now) {
  if (down && !g_home_down) {
    g_home_down = true;
    g_home_t0 = now;
    g_home_long = now < g_home_lock;
  } else if (down && g_home_down && !g_home_long && now - g_home_t0 >= kHomeLongMs && now >= g_home_lock) {
    g_home_long = true;
    g_home_lock = now + kHomeLockoutMs;
    g_screen_on = false;
    g_host->command(Cmd::ScreenOff, 0);
  } else if (!down && g_home_down) {
    const bool was_long = g_home_long;
    g_home_down = false;
    if (!was_long && now >= g_home_lock) {
      g_home_lock = now + kHomeLockoutMs;
      if (!g_screen_on) {
        g_screen_on = true;
        g_suppress_until = now + 300;
        g_wait_release = true;
        g_activity = now;
        g_host->command(Cmd::ScreenOn, 0);
      } else if (g_mode != Mode::Home) {
        go(Mode::Home);
      } else {
        g_screen_on = false;
        g_host->command(Cmd::ScreenOff, 0);
      }
    }
  }
}

void ui_init(Host& host) {
  g_host = &host;
  g_activity = lv_tick_get();
  build_status();
  ensure_overlays();
  g_mode = Mode::Home;
  lv_screen_load(make_home());
}

void ui_set_sample(const TouchSample& sample) { g_sample = sample; }

bool ui_pointer_live(uint32_t now_ms) {
  if (!g_screen_on || now_ms < g_suppress_until) {
    return false;
  }
  if (g_wait_release) {
    if (!g_sample.down) {
      g_wait_release = false;
    }
    return false;
  }
  return true;
}

void ui_tick(uint32_t now_ms) {
  if (g_host == nullptr) {
    return;
  }
  if (g_sample.down || g_sample.home) {
    g_activity = now_ms;
  }
  on_home(g_sample.home, now_ms);
  const Model& model = g_host->model();
  if (g_screen_on && model.screen_off_sec > 0 && now_ms - g_activity > static_cast<uint32_t>(model.screen_off_sec) * 1000u) {
    g_screen_on = false;
    g_host->command(Cmd::ScreenOff, 0);
  }
  // g_screen_on is owned by this task. model.screen_on is the IO task's copy of
  // the last ScreenOn/ScreenOff, snapshotted before this tick, so it is still
  // false on the tick that queues ScreenOn. Copying it back cleared the wake
  // and left touch and the Wi-Fi line dead until the next press.
  paint_status(model);
  paint_screen(model);
  if (g_pair_code != nullptr && model.pair_show) {
    lv_label_set_text(g_pair_code, model.pair_code);
  }
  show(g_pair, model.pair_show);
  show(g_wifi, g_screen_on);
  apply_nav();
}

}  // namespace kmj
