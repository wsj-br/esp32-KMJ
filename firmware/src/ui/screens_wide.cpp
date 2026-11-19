// Screen construction for the LilyGO T-Display-S3 Pro (480x222 landscape).
// State, painting and navigation live in ui.cpp; this file only builds the
// widget trees. Keep the layout in sync with paint_screen() in ui.cpp.

#include "ui/ui_internal.hpp"

#include <stdio.h>

#include "pins.hpp"
#include "ui/build_stamp.hpp"

#if defined(KMJ_BOARD_TDISPLAY_S3)

namespace kmj {

lv_obj_t* screen_base() {
  lv_obj_t* scr = lv_obj_create(nullptr);
  lv_obj_set_style_bg_color(scr, lv_color_hex(kBg), 0);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
  lv_obj_set_style_text_color(scr, lv_color_hex(kText), 0);
  lv_obj_set_style_text_font(scr, &font_chakra_16, 0);
  lv_obj_set_style_pad_all(scr, 0, 0);
  lv_obj_set_style_border_width(scr, 0, 0);
  lv_obj_set_scrollable(scr, false);
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
  lv_obj_set_clickable(rail, false);
}

void clear_screen_ptrs() {
  g_km_state = g_km_meta = g_km_counts = g_km_key = g_km_paired = nullptr;
  g_km_pair_btn = g_km_pair_label = g_line = nullptr;
  g_jig_state = g_jig_counts = g_run_btn = g_run_label = g_usb_label = g_timeout_label = nullptr;
  g_home_km = g_home_jig = g_home_cfg = nullptr;
  g_cfg_info = g_cfg_setup = g_cfg_forget = nullptr;
}

lv_obj_t* label(lv_obj_t* parent, const char* text, const lv_font_t* font, uint32_t color) {
  lv_obj_t* lab = lv_label_create(parent);
  lv_label_set_text(lab, text);
  lv_obj_set_style_text_font(lab, font, 0);
  lv_obj_set_style_text_color(lab, lv_color_hex(color), 0);
  lv_obj_set_clickable(lab, false);
  return lab;
}

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
  lv_obj_set_clickable(line, false);
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
  g_km_paired = label(scr, "Not paired", &font_plex_14, kDim);
  lv_obj_set_pos(g_km_paired, 300, kStatusH + 6);
  lv_obj_set_width(g_km_paired, 164);
  lv_label_set_long_mode(g_km_paired, LV_LABEL_LONG_CLIP);
  g_line = make_trace(scr);
  lv_obj_set_pos(g_line, 16, kStatusH + 120);
  g_km_pair_btn = lv_button_create(scr);
  lv_obj_set_size(g_km_pair_btn, 120, 32);
  lv_obj_set_pos(g_km_pair_btn, 216, 176);
  lv_obj_set_style_bg_color(g_km_pair_btn, lv_color_hex(kPanel), 0);
  g_km_pair_label = label(g_km_pair_btn, "Pair", &font_plex_14, kText);
  lv_obj_add_event_cb(g_km_pair_btn, [](lv_event_t*) { g_host->command(Cmd::ArmPair, 0); }, LV_EVENT_CLICKED, nullptr);
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
  lv_obj_set_scrollable(btn, false);
  label(btn, text, &font_plex_14, kText);
  lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, user);
  return btn;
}

void on_cmd_button(lv_event_t* e) {
  const Cmd cmd = static_cast<Cmd>(reinterpret_cast<uintptr_t>(lv_event_get_user_data(e)));
  g_host->command(cmd, 0);
}

lv_obj_t* make_config() {
  lv_obj_t* scr = screen_base();
  back_button(scr);
  lv_obj_t* title = label(scr, "Configuration", &font_chakra_20, kSlate);
  lv_obj_set_pos(title, 84, kStatusH + 6);

  const int y0 = kStatusH + 40;
  // One Wi-Fi button at a time: setup while offline, forget while connected.
  g_cfg_setup = action_button(scr, "Wi-Fi setup", 8, y0, 140, on_cmd_button, reinterpret_cast<void*>(static_cast<uintptr_t>(Cmd::StartImprov)));
  g_cfg_forget = action_button(scr, "Forget Wi-Fi", 8, y0, 140, on_cmd_button, reinterpret_cast<void*>(static_cast<uintptr_t>(Cmd::ForgetWifi)));
  lv_obj_set_hidden(g_cfg_forget, true);
  const char* timeout = timeout_caption(g_host->model().screen_off_sec);
  lv_obj_t* blank = action_button(scr, timeout != nullptr ? timeout : "Timeout", 156, y0, 116, cycle_timeout, nullptr);
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

  char stamp[64];
  snprintf(stamp, sizeof(stamp), "%s  %s", kFirmwareVersion, build_timestamp());
  lv_obj_t* build = label(scr, stamp, &font_plex_14, kDim);
  lv_obj_align(build, LV_ALIGN_BOTTOM_RIGHT, -8, -8);
  return scr;
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
    lv_obj_set_hidden(g_pair, true);
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
  lv_obj_set_clickable(bar, false);
  lv_obj_set_scrollable(bar, false);
  g_wifi = label(bar, "Wi-Fi", &font_plex_14, kDim);
  lv_obj_set_pos(g_wifi, 8, 4);
  lv_obj_set_width(g_wifi, 360);
  lv_label_set_long_mode(g_wifi, LV_LABEL_LONG_CLIP);
  g_ip = label(bar, "", &font_plex_14, kText);
  lv_obj_set_pos(g_ip, 150, 4);
  g_bat = label(bar, "--", &font_plex_14, kText);
  lv_obj_align(g_bat, LV_ALIGN_RIGHT_MID, -8, 0);
}

}  // namespace kmj

#endif  // KMJ_BOARD_TDISPLAY_S3
