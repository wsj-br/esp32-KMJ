#include "ui/ui.hpp"

#include <stdio.h>
#include <string.h>

#include <lvgl.h>

#include "pins.hpp"

extern "C" {
extern const lv_font_t font_chakra_16;
extern const lv_font_t font_chakra_20;
extern const lv_font_t font_plex_14;
}

namespace kmj {
namespace {

constexpr uint32_t kBg = 0x121418;
constexpr uint32_t kPanel = 0x1B2028;
constexpr uint32_t kLine = 0x31404C;
constexpr uint32_t kText = 0xE7E4DC;
constexpr uint32_t kDim = 0x8E97A3;
constexpr uint32_t kCyan = 0x3EE0C5;
constexpr uint32_t kAmber = 0xF0A202;
constexpr uint32_t kSlate = 0xC9D2DC;

Host* g_host = nullptr;
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
bool g_setup_msg = false;
bool g_wait_release = false;

lv_obj_t* g_wifi = nullptr;
lv_obj_t* g_ip = nullptr;
lv_obj_t* g_bat = nullptr;
lv_obj_t* g_km_state = nullptr;
lv_obj_t* g_km_meta = nullptr;
lv_obj_t* g_km_counts = nullptr;
lv_obj_t* g_km_key = nullptr;
lv_obj_t* g_line = nullptr;
lv_obj_t* g_jig_state = nullptr;
lv_obj_t* g_jig_counts = nullptr;
lv_obj_t* g_run_btn = nullptr;
lv_obj_t* g_run_label = nullptr;
lv_obj_t* g_usb_label = nullptr;
lv_obj_t* g_timeout_label = nullptr;
lv_obj_t* g_home_km = nullptr;
lv_obj_t* g_home_jig = nullptr;
lv_obj_t* g_home_cfg = nullptr;
lv_obj_t* g_cfg_info = nullptr;
lv_obj_t* g_pair = nullptr;
lv_obj_t* g_pair_code = nullptr;
lv_obj_t* g_auth = nullptr;
lv_point_precise_t g_points[48];

lv_obj_t* screen_base() {
  lv_obj_t* scr = lv_obj_create(nullptr);
  lv_obj_set_style_bg_color(scr, lv_color_hex(kBg), 0);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
  lv_obj_set_style_text_color(scr, lv_color_hex(kText), 0);
  lv_obj_set_style_text_font(scr, &font_chakra_16, 0);
  lv_obj_set_style_pad_all(scr, 0, 0);
  lv_obj_set_style_border_width(scr, 0, 0);
  lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
  return scr;
}

void style_tile(lv_obj_t* obj, uint32_t accent) {
  lv_obj_set_style_bg_color(obj, lv_color_hex(kPanel), 0);
  lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(obj, 8, 0);
  lv_obj_set_style_border_width(obj, 1, 0);
  lv_obj_set_style_border_color(obj, lv_color_hex(kLine), 0);
  lv_obj_set_style_border_side(obj, LV_BORDER_SIDE_FULL, 0);
  lv_obj_set_style_pad_all(obj, 8, 0);
  lv_obj_set_style_shadow_width(obj, 0, 0);
  lv_obj_set_style_transform_pivot_x(obj, lv_pct(50), 0);
  lv_obj_set_style_transform_pivot_y(obj, lv_pct(50), 0);
  lv_obj_set_style_transform_scale(obj, 245, LV_STATE_PRESSED);
  lv_obj_t* rail = lv_obj_create(obj);
  lv_obj_set_size(rail, 4, lv_pct(70));
  lv_obj_align(rail, LV_ALIGN_LEFT_MID, -2, 0);
  lv_obj_set_style_bg_color(rail, lv_color_hex(accent), 0);
  lv_obj_set_style_border_width(rail, 0, 0);
  lv_obj_set_style_radius(rail, 2, 0);
  lv_obj_remove_flag(rail, LV_OBJ_FLAG_CLICKABLE);
}

void clear_screen_ptrs() {
  g_km_state = g_km_meta = g_km_counts = g_km_key = g_line = nullptr;
  g_jig_state = g_jig_counts = g_run_btn = g_run_label = g_usb_label = g_timeout_label = nullptr;
  g_home_km = g_home_jig = g_home_cfg = g_cfg_info = nullptr;
}

lv_obj_t* label(lv_obj_t* parent, const char* text, const lv_font_t* font, uint32_t color) {
  lv_obj_t* lab = lv_label_create(parent);
  lv_label_set_text(lab, text);
  lv_obj_set_style_text_font(lab, font, 0);
  lv_obj_set_style_text_color(lab, lv_color_hex(color), 0);
  lv_obj_remove_flag(lab, LV_OBJ_FLAG_CLICKABLE);
  return lab;
}

void go(Mode mode);

void on_gesture(lv_event_t* e) {
  lv_indev_t* indev = lv_indev_active();
  if (indev != nullptr && lv_indev_get_gesture_dir(indev) == LV_DIR_RIGHT) {
    go(Mode::Home);
    lv_indev_wait_release(indev);
  }
  (void)e;
}

lv_obj_t* back_button(lv_obj_t* parent) {
  lv_obj_t* btn = lv_button_create(parent);
  lv_obj_set_size(btn, 64, 28);
  lv_obj_set_pos(btn, 8, kStatusH + 4);
  lv_obj_set_style_bg_color(btn, lv_color_hex(kPanel), 0);
  lv_obj_set_style_radius(btn, 6, 0);
  label(btn, "Back", &font_plex_14, kSlate);
  lv_obj_add_event_cb(btn, [](lv_event_t*) { go(Mode::Home); }, LV_EVENT_CLICKED, nullptr);
  return btn;
}

lv_obj_t* make_trace(lv_obj_t* parent) {
  lv_obj_t* line = lv_line_create(parent);
  for (int i = 0; i < 48; i++) {
    g_points[i].x = i * 4;
    g_points[i].y = 16;
  }
  lv_line_set_points(line, g_points, 48);
  lv_obj_set_style_line_width(line, 2, 0);
  lv_obj_set_style_line_color(line, lv_color_hex(kCyan), 0);
  lv_obj_remove_flag(line, LV_OBJ_FLAG_CLICKABLE);
  return line;
}

lv_obj_t* make_home() {
  lv_obj_t* scr = screen_base();
  lv_obj_t* km = lv_button_create(scr);
  lv_obj_set_pos(km, 8, kStatusH + 8);
  lv_obj_set_size(km, 248, 176);
  style_tile(km, kCyan);
  label(km, "KM", &font_plex_14, kCyan);
  lv_obj_align(lv_obj_get_child(km, 1), LV_ALIGN_TOP_LEFT, 10, 0);
  lv_obj_t* title = label(km, "KM Emulator", &font_chakra_20, kText);
  lv_obj_align(title, LV_ALIGN_TOP_LEFT, 10, 16);
  g_home_km = label(km, "idle", &font_plex_14, kDim);
  lv_obj_align(g_home_km, LV_ALIGN_BOTTOM_LEFT, 10, 0);
  lv_obj_add_event_cb(km, [](lv_event_t*) { go(Mode::Km); }, LV_EVENT_CLICKED, nullptr);

  lv_obj_t* jig = lv_button_create(scr);
  lv_obj_set_pos(jig, 264, kStatusH + 8);
  lv_obj_set_size(jig, 208, 84);
  style_tile(jig, kAmber);
  lv_obj_t* jt = label(jig, "Jiggler", &font_chakra_16, kText);
  lv_obj_align(jt, LV_ALIGN_TOP_LEFT, 10, 0);
  g_home_jig = label(jig, "stopped", &font_plex_14, kDim);
  lv_obj_align(g_home_jig, LV_ALIGN_BOTTOM_LEFT, 10, 0);
  lv_obj_add_event_cb(jig, [](lv_event_t*) { go(Mode::Jiggler); }, LV_EVENT_CLICKED, nullptr);

  lv_obj_t* cfg = lv_button_create(scr);
  lv_obj_set_pos(cfg, 264, kStatusH + 100);
  lv_obj_set_size(cfg, 208, 84);
  style_tile(cfg, kSlate);
  lv_obj_t* ct = label(cfg, "Config", &font_chakra_16, kText);
  lv_obj_align(ct, LV_ALIGN_TOP_LEFT, 10, 0);
  g_home_cfg = label(cfg, "", &font_plex_14, kDim);
  lv_obj_align(g_home_cfg, LV_ALIGN_BOTTOM_LEFT, 10, 0);
  lv_obj_add_event_cb(cfg, [](lv_event_t*) { go(Mode::Config); }, LV_EVENT_CLICKED, nullptr);
  return scr;
}

lv_obj_t* make_km() {
  lv_obj_t* scr = screen_base();
  lv_obj_add_event_cb(scr, on_gesture, LV_EVENT_GESTURE, nullptr);
  back_button(scr);
  lv_obj_t* title = label(scr, "KM Emulator", &font_chakra_20, kCyan);
  lv_obj_set_pos(title, 84, kStatusH + 6);
  g_km_state = label(scr, "Listening", &font_chakra_16, kText);
  lv_obj_set_pos(g_km_state, 16, kStatusH + 40);
  g_km_meta = label(scr, "", &font_plex_14, kDim);
  lv_obj_set_pos(g_km_meta, 16, kStatusH + 64);
  g_km_counts = label(scr, "", &font_plex_14, kText);
  lv_obj_set_pos(g_km_counts, 16, kStatusH + 86);
  g_km_key = label(scr, "", &font_plex_14, kAmber);
  lv_obj_set_pos(g_km_key, 300, kStatusH + 40);
  g_line = make_trace(scr);
  lv_obj_set_pos(g_line, 16, kStatusH + 120);
  lv_obj_t* disc = lv_button_create(scr);
  lv_obj_set_size(disc, 120, 32);
  lv_obj_set_pos(disc, 344, 176);
  lv_obj_set_style_bg_color(disc, lv_color_hex(0x3A2A2A), 0);
  label(disc, "Drop", &font_plex_14, kText);
  lv_obj_add_event_cb(disc, [](lv_event_t*) { g_host->command(Cmd::DisconnectClient, 0); }, LV_EVENT_CLICKED, nullptr);
  return scr;
}

lv_obj_t* make_jiggler() {
  lv_obj_t* scr = screen_base();
  lv_obj_add_event_cb(scr, on_gesture, LV_EVENT_GESTURE, nullptr);
  back_button(scr);
  lv_obj_t* title = label(scr, "Mouse Jiggler", &font_chakra_20, kAmber);
  lv_obj_set_pos(title, 84, kStatusH + 6);
  g_jig_state = label(scr, "", &font_chakra_16, kText);
  lv_obj_set_pos(g_jig_state, 16, kStatusH + 42);
  g_jig_counts = label(scr, "", &font_plex_14, kDim);
  lv_obj_set_pos(g_jig_counts, 16, kStatusH + 66);
  g_run_btn = lv_button_create(scr);
  lv_obj_set_pos(g_run_btn, 8, 168);
  lv_obj_set_size(g_run_btn, 148, 36);
  lv_obj_set_style_bg_color(g_run_btn, lv_color_hex(0x2A2E34), 0);
  lv_obj_set_style_radius(g_run_btn, 8, 0);
  g_run_label = label(g_run_btn, "stopped", &font_plex_14, 0x8E97A3);
  lv_obj_add_event_cb(g_run_btn, [](lv_event_t*) {
    g_host->command(Cmd::JigglerEnabled, g_host->model().jiggler_run ? 0 : 1);
  }, LV_EVENT_CLICKED, nullptr);

  struct Press {
    const char* text;
    int x;
    Cmd cmd;
  };
  static const Press kPresses[] = {
      {"Jiggle", 168, Cmd::JiggleNow},
      {"Nudge", 280, Cmd::Nudge},
  };
  for (const Press& press : kPresses) {
    lv_obj_t* btn = lv_button_create(scr);
    lv_obj_set_pos(btn, press.x, 168);
    lv_obj_set_size(btn, 100, 36);
    lv_obj_set_style_bg_color(btn, lv_color_hex(kPanel), 0);
    lv_obj_set_style_radius(btn, 8, 0);
    label(btn, press.text, &font_plex_14, kText);
    lv_obj_add_event_cb(btn, [](lv_event_t* e) {
      const Press* spec = static_cast<const Press*>(lv_event_get_user_data(e));
      g_host->command(spec->cmd, 0);
    }, LV_EVENT_CLICKED, const_cast<Press*>(&press));
  }
  lv_obj_t* usb = lv_button_create(scr);
  lv_obj_set_pos(usb, 360, kStatusH + 40);
  lv_obj_set_size(usb, 108, 32);
  lv_obj_set_style_bg_color(usb, lv_color_hex(kPanel), 0);
  g_usb_label = label(usb, "USB off", &font_plex_14, kText);
  lv_obj_add_event_cb(usb, [](lv_event_t*) {
    g_host->command(Cmd::UsbWanted, g_host->model().usb_wanted ? 0 : 1);
  }, LV_EVENT_CLICKED, nullptr);
  return scr;
}

const char* timeout_caption(int sec) {
  switch (sec) {
    case 0:
      return "Timeout off";
    case 30:
      return "Timeout 30s";
    case 60:
      return "Timeout 1 min";
    case 300:
      return "Timeout 5 min";
    default:
      return nullptr;
  }
}

void cycle_timeout(lv_event_t*) {
  const int cur = g_host->model().screen_off_sec;
  const int next = cur == 0 ? 30 : cur == 30 ? 60 : cur == 60 ? 300 : 0;
  g_host->command(Cmd::ScreenTimeout, next);
}

lv_obj_t* action_button(lv_obj_t* parent, const char* text, int x, int y, int w, lv_event_cb_t cb, void* user) {
  lv_obj_t* btn = lv_button_create(parent);
  lv_obj_set_pos(btn, x, y);
  lv_obj_set_size(btn, w, 32);
  lv_obj_set_style_bg_color(btn, lv_color_hex(kPanel), 0);
  lv_obj_set_style_radius(btn, 6, 0);
  lv_obj_remove_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
  label(btn, text, &font_plex_14, kText);
  lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, user);
  return btn;
}

void on_cmd_button(lv_event_t* e) {
  const Cmd cmd = static_cast<Cmd>(reinterpret_cast<uintptr_t>(lv_event_get_user_data(e)));
  if (cmd == Cmd::StartImprov) {
    g_setup_msg = true;
  }
  g_host->command(cmd, 0);
}

lv_obj_t* make_config() {
  lv_obj_t* scr = screen_base();
  back_button(scr);
  lv_obj_t* title = label(scr, "Configuration", &font_chakra_20, kSlate);
  lv_obj_set_pos(title, 84, kStatusH + 6);

  const int y0 = kStatusH + 40;
  action_button(scr, "Wi-Fi setup", 8, y0, 128, on_cmd_button, reinterpret_cast<void*>(static_cast<uintptr_t>(Cmd::StartImprov)));
  action_button(scr, "Forget Wi-Fi", 144, y0, 140, on_cmd_button, reinterpret_cast<void*>(static_cast<uintptr_t>(Cmd::ForgetWifi)));
  lv_obj_t* pair = action_button(scr, "Pair", 292, y0, 56, on_cmd_button, reinterpret_cast<void*>(static_cast<uintptr_t>(Cmd::ArmPair)));
  lv_obj_set_style_pad_hor(pair, 8, 0);
  const char* timeout = timeout_caption(g_host->model().screen_off_sec);
  lv_obj_t* blank = action_button(scr, timeout != nullptr ? timeout : "Timeout", 356, y0, 116, cycle_timeout, nullptr);
  lv_obj_set_style_pad_hor(blank, 4, 0);
  g_timeout_label = lv_obj_get_child(blank, 0);
  lv_label_set_long_mode(g_timeout_label, LV_LABEL_LONG_CLIP);

  action_button(scr, "Reboot", 8, y0 + 40, 112, on_cmd_button, reinterpret_cast<void*>(static_cast<uintptr_t>(Cmd::Reboot)));
  lv_obj_t* slider = lv_slider_create(scr);
  lv_obj_set_pos(slider, 136, y0 + 48);
  lv_obj_set_width(slider, 200);
  lv_slider_set_range(slider, 1, 16);
  lv_slider_set_value(slider, g_host->model().backlight, LV_ANIM_OFF);
  lv_obj_add_event_cb(slider, [](lv_event_t* e) {
    g_host->command(Cmd::Backlight, lv_slider_get_value(static_cast<lv_obj_t*>(lv_event_get_target(e))));
  }, LV_EVENT_RELEASED, nullptr);
  lv_obj_t* light = label(scr, "Light", &font_plex_14, kDim);
  lv_obj_set_pos(light, 348, y0 + 44);

  g_cfg_info = label(scr, "", &font_plex_14, kDim);
  lv_obj_set_pos(g_cfg_info, 8, y0 + 80);
  lv_obj_set_width(g_cfg_info, 464);
  lv_label_set_long_mode(g_cfg_info, LV_LABEL_LONG_CLIP);
  return scr;
}

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
  lv_screen_load(scr);
  if (old != nullptr && old != scr) {
    lv_obj_delete(old);
  }
}

void ensure_overlays() {
  if (g_pair == nullptr) {
    g_pair = lv_obj_create(lv_layer_top());
    lv_obj_set_size(g_pair, 280, 120);
    lv_obj_center(g_pair);
    lv_obj_set_style_bg_color(g_pair, lv_color_hex(0x10141A), 0);
    lv_obj_set_style_border_color(g_pair, lv_color_hex(kCyan), 0);
    lv_obj_set_style_border_width(g_pair, 1, 0);
    label(g_pair, "Pairing code", &font_plex_14, kDim);
    g_pair_code = label(g_pair, "000000", &font_chakra_20, kCyan);
    lv_obj_align(g_pair_code, LV_ALIGN_CENTER, 0, -6);
    lv_obj_t* ok = lv_button_create(g_pair);
    lv_obj_set_size(ok, 90, 28);
    lv_obj_align(ok, LV_ALIGN_BOTTOM_LEFT, 8, -8);
    label(ok, "Match", &font_plex_14, kText);
    lv_obj_add_event_cb(ok, [](lv_event_t*) { g_host->command(Cmd::ConfirmPair, 0); }, LV_EVENT_CLICKED, nullptr);
    lv_obj_t* no = lv_button_create(g_pair);
    lv_obj_set_size(no, 90, 28);
    lv_obj_align(no, LV_ALIGN_BOTTOM_RIGHT, -8, -8);
    label(no, "Cancel", &font_plex_14, kText);
    lv_obj_add_event_cb(no, [](lv_event_t*) { g_host->command(Cmd::CancelPair, 0); }, LV_EVENT_CLICKED, nullptr);
    lv_obj_add_flag(g_pair, LV_OBJ_FLAG_HIDDEN);
  }
  if (g_auth == nullptr) {
    g_auth = lv_obj_create(lv_layer_top());
    lv_obj_set_size(g_auth, 400, 132);
    lv_obj_center(g_auth);
    lv_obj_set_style_bg_color(g_auth, lv_color_hex(0x10141A), 0);
    lv_obj_set_style_border_color(g_auth, lv_color_hex(kAmber), 0);
    lv_obj_set_style_border_width(g_auth, 1, 0);
    lv_obj_t* ask = label(g_auth, "Waiting for Wi-Fi config via Bluetooth", &font_plex_14, kAmber);
    lv_obj_align(ask, LV_ALIGN_TOP_MID, 0, 6);
    lv_obj_t* hint = label(g_auth, "Tap Allow, then use the KMJ app", &font_plex_14, kDim);
    lv_obj_align(hint, LV_ALIGN_TOP_MID, 0, 28);
    lv_obj_t* allow = lv_button_create(g_auth);
    lv_obj_set_size(allow, 90, 28);
    lv_obj_align(allow, LV_ALIGN_BOTTOM_LEFT, 8, -8);
    label(allow, "Allow", &font_plex_14, kText);
    lv_obj_add_event_cb(allow, [](lv_event_t*) { g_host->command(Cmd::ImprovAllow, 0); }, LV_EVENT_CLICKED, nullptr);
    lv_obj_t* deny = lv_button_create(g_auth);
    lv_obj_set_size(deny, 90, 28);
    lv_obj_align(deny, LV_ALIGN_BOTTOM_RIGHT, -8, -8);
    label(deny, "Deny", &font_plex_14, kText);
    lv_obj_add_event_cb(deny, [](lv_event_t*) { g_host->command(Cmd::ImprovDeny, 0); }, LV_EVENT_CLICKED, nullptr);
    lv_obj_add_flag(g_auth, LV_OBJ_FLAG_HIDDEN);
  }
}

void show(lv_obj_t* obj, bool visible) {
  if (obj == nullptr) {
    return;
  }
  if (visible) {
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
  }
}

void build_status() {
  lv_obj_t* bar = lv_obj_create(lv_layer_top());
  lv_obj_set_size(bar, kScreenW, kStatusH);
  lv_obj_set_pos(bar, 0, 0);
  lv_obj_set_style_bg_color(bar, lv_color_hex(0x0C0F13), 0);
  lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(bar, 0, 0);
  lv_obj_set_style_border_width(bar, 0, 0);
  lv_obj_set_style_border_side(bar, LV_BORDER_SIDE_BOTTOM, 0);
  lv_obj_set_style_pad_all(bar, 0, 0);
  lv_obj_remove_flag(bar, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_remove_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
  g_wifi = label(bar, "Wi-Fi", &font_plex_14, kDim);
  lv_obj_set_pos(g_wifi, 8, 4);
  lv_obj_set_width(g_wifi, 360);
  lv_label_set_long_mode(g_wifi, LV_LABEL_LONG_CLIP);
  g_ip = label(bar, "", &font_plex_14, kText);
  lv_obj_set_pos(g_ip, 150, 4);
  g_bat = label(bar, "--", &font_plex_14, kText);
  lv_obj_align(g_bat, LV_ALIGN_RIGHT_MID, -8, 0);
}

void paint_status(const Model& model) {
  char buf[64];
  const bool waiting = g_setup_msg || model.setup_line[0] != '\0';
  if (model.setup_fail && !model.improv_on && model.setup_line[0] == '\0') {
    snprintf(buf, sizeof(buf), "%s", model.setup_note[0] ? model.setup_note : "Bluetooth did not start");
    lv_obj_set_style_text_color(g_wifi, lv_color_hex(kAmber), 0);
  } else if (waiting) {
    snprintf(buf, sizeof(buf), "%s", model.setup_line[0] ? model.setup_line : "Waiting for Wi-Fi config via Bluetooth");
    lv_obj_set_style_text_color(g_wifi, lv_color_hex(kAmber), 0);
  } else if (model.wifi_connected) {
    snprintf(buf, sizeof(buf), "%s %ddB", model.wifi_ssid[0] ? model.wifi_ssid : "link", model.rssi);
    lv_obj_set_style_text_color(g_wifi, lv_color_hex(kDim), 0);
  } else if (model.wifi_saved) {
    snprintf(buf, sizeof(buf), "Wi-Fi ...");
    lv_obj_set_style_text_color(g_wifi, lv_color_hex(kDim), 0);
  } else {
    snprintf(buf, sizeof(buf), "Wi-Fi off");
    lv_obj_set_style_text_color(g_wifi, lv_color_hex(kDim), 0);
  }
  lv_label_set_text(g_wifi, buf);
  const bool setup_line = waiting || (model.setup_fail && !model.improv_on);
  show(g_ip, !setup_line);
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
    snprintf(buf, sizeof(buf), "left %lu:%02lu   sent %lu  jiggles %lu", static_cast<unsigned long>(left / 60),
             static_cast<unsigned long>(left % 60), static_cast<unsigned long>(model.hid_tx),
             static_cast<unsigned long>(model.jiggle_count));
    lv_label_set_text(g_jig_counts, buf);
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
    const bool waiting = g_setup_msg || model.setup_line[0] != '\0';
    if (model.setup_line[0] != '\0' || model.setup_wait || model.improv_on || model.setup_fail) {
      g_setup_msg = false;
    }
    if (model.setup_fail && !model.improv_on && model.setup_line[0] == '\0') {
      snprintf(buf, sizeof(buf), "%s\nBattery  %s%s\n%s  rssi %d\nheap %lu  up %lus",
               model.setup_note[0] ? model.setup_note : "Bluetooth did not start", power,
               model.charging ? "  charging" : "", model.wifi_connected ? model.wifi_ssid : "Wi-Fi off", model.rssi,
               static_cast<unsigned long>(model.heap_free), static_cast<unsigned long>(model.uptime_s));
    } else if (waiting) {
      const char* step = model.setup_line[0] ? model.setup_line : "Waiting for Wi-Fi config via Bluetooth";
      snprintf(buf, sizeof(buf), "%s\nBattery  %s%s\n%s  rssi %d\n%s", step, power, model.charging ? "  charging" : "",
               model.wifi_connected ? model.wifi_ssid : "Wi-Fi off", model.rssi,
               model.vbus ? "Power  USB" : "Power  battery");
    } else {
      snprintf(buf, sizeof(buf), "%s\nBattery  %s%s\n%s  rssi %d\nheap %lu  up %lus",
               model.vbus ? "Power  USB" : "Power  battery", power, model.charging ? "  charging" : "",
               model.wifi_connected ? model.wifi_ssid : "Wi-Fi off", model.rssi,
               static_cast<unsigned long>(model.heap_free), static_cast<unsigned long>(model.uptime_s));
    }
    lv_label_set_text(g_cfg_info, buf);
    const bool active = waiting || (model.setup_fail && !model.improv_on);
    lv_obj_set_style_text_color(g_cfg_info, lv_color_hex(active ? kAmber : kDim), 0);
  }
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

}  // namespace

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
  if (!model.screen_on) {
    g_screen_on = false;
  }
  paint_status(model);
  paint_screen(model);
  if (g_pair_code != nullptr && model.pair_show) {
    lv_label_set_text(g_pair_code, model.pair_code);
  }
  show(g_pair, model.pair_show);
  show(g_auth, g_screen_on && model.improv_ask && !model.pair_show);
  show(g_wifi, g_screen_on);
  apply_nav();
}

}  // namespace kmj
