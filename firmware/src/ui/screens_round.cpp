// Screen construction for the Waveshare ESP32-S3-Touch-LCD-1.85B (360x360
// round). Widget geometry comes from logic/layout_round.hpp, which the native
// tests verify against the visible circle. State, painting and navigation
// live in ui.cpp; this file only builds the widget trees.
//
// There is no back button on the round screens: the BOOT button (wired as the
// home/wake button) and the swipe-right gesture both return home.

#include "ui/ui_internal.hpp"

#include <stdio.h>

#include "logic/layout_round.hpp"
#include "pins.hpp"
#include "ui/build_stamp.hpp"

#if defined(KMJ_BOARD_WS_185B)

namespace kmj {

using namespace layout_round;

namespace {

void on_gesture(lv_event_t* e) {
  lv_indev_t* indev = lv_indev_active();
  if (indev != nullptr && lv_indev_get_gesture_dir(indev) == LV_DIR_RIGHT) {
    go(Mode::Home);
    lv_indev_wait_release(indev);
  }
  (void)e;
}

}  // namespace

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
  lv_obj_set_style_radius(obj, 12, 0);
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
  lv_obj_set_pos(km, kHomeKm.x, kHomeKm.y);
  lv_obj_set_size(km, kHomeKm.w, kHomeKm.h);
  style_tile(km, kCyan);
  label(km, "KM", &font_plex_14, kCyan);
  lv_obj_align(lv_obj_get_child(km, 1), LV_ALIGN_TOP_LEFT, 10, 0);
  lv_obj_t* title = label(km, "KM Emulator", &font_chakra_20, kText);
  lv_obj_align(title, LV_ALIGN_TOP_LEFT, 10, 16);
  g_home_km = label(km, "idle", &font_plex_14, kDim);
  lv_obj_align(g_home_km, LV_ALIGN_BOTTOM_LEFT, 10, 0);
  lv_obj_add_event_cb(km, [](lv_event_t*) { go(Mode::Km); }, LV_EVENT_CLICKED, nullptr);

  lv_obj_t* jig = lv_button_create(scr);
  lv_obj_set_pos(jig, kHomeJig.x, kHomeJig.y);
  lv_obj_set_size(jig, kHomeJig.w, kHomeJig.h);
  style_tile(jig, kAmber);
  lv_obj_t* jt = label(jig, "Jiggler", &font_chakra_16, kText);
  lv_obj_align(jt, LV_ALIGN_TOP_LEFT, 8, 0);
  g_home_jig = label(jig, "stopped", &font_plex_14, kDim);
  lv_obj_align(g_home_jig, LV_ALIGN_BOTTOM_LEFT, 8, 0);
  lv_obj_add_event_cb(jig, [](lv_event_t*) { go(Mode::Jiggler); }, LV_EVENT_CLICKED, nullptr);

  lv_obj_t* cfg = lv_button_create(scr);
  lv_obj_set_pos(cfg, kHomeCfg.x, kHomeCfg.y);
  lv_obj_set_size(cfg, kHomeCfg.w, kHomeCfg.h);
  style_tile(cfg, kSlate);
  lv_obj_t* ct = label(cfg, "Config", &font_chakra_16, kText);
  lv_obj_align(ct, LV_ALIGN_TOP_LEFT, 8, 0);
  g_home_cfg = label(cfg, "", &font_plex_14, kDim);
  lv_obj_align(g_home_cfg, LV_ALIGN_BOTTOM_LEFT, 8, 0);
  lv_obj_add_event_cb(cfg, [](lv_event_t*) { go(Mode::Config); }, LV_EVENT_CLICKED, nullptr);
  return scr;
}

lv_obj_t* make_km() {
  lv_obj_t* scr = screen_base();
  lv_obj_add_event_cb(scr, on_gesture, LV_EVENT_GESTURE, nullptr);
  lv_obj_t* title = label(scr, "KM Emulator", &font_chakra_20, kCyan);
  lv_obj_set_pos(title, kKmTitle.x, kKmTitle.y);
  lv_obj_set_width(title, kKmTitle.w);
  lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);

  g_km_state = label(scr, "Listening", &font_chakra_16, kText);
  lv_obj_set_pos(g_km_state, kKmState.x, kKmState.y);
  g_km_meta = label(scr, "", &font_plex_14, kDim);
  lv_obj_set_pos(g_km_meta, kKmMeta.x, kKmMeta.y);
  lv_obj_set_width(g_km_meta, kKmMeta.w);
  lv_label_set_long_mode(g_km_meta, LV_LABEL_LONG_CLIP);
  g_km_counts = label(scr, "", &font_plex_14, kText);
  lv_obj_set_pos(g_km_counts, kKmCounts.x, kKmCounts.y);
  lv_obj_set_width(g_km_counts, kKmCounts.w);
  lv_label_set_long_mode(g_km_counts, LV_LABEL_LONG_CLIP);
  g_line = make_trace(scr);
  lv_obj_set_pos(g_line, kKmTrace.x, kKmTrace.y);

  g_km_paired = label(scr, "Not paired", &font_plex_14, kDim);
  lv_obj_set_pos(g_km_paired, kKmPaired.x, kKmPaired.y);
  lv_obj_set_width(g_km_paired, kKmPaired.w);
  lv_label_set_long_mode(g_km_paired, LV_LABEL_LONG_CLIP);
  g_km_key = label(scr, "", &font_plex_14, kAmber);
  lv_obj_set_pos(g_km_key, kKmKey.x, kKmKey.y);
  lv_obj_set_width(g_km_key, kKmKey.w);
  lv_label_set_long_mode(g_km_key, LV_LABEL_LONG_CLIP);

  g_km_pair_btn = lv_button_create(scr);
  lv_obj_set_pos(g_km_pair_btn, kKmPairBtn.x, kKmPairBtn.y);
  lv_obj_set_size(g_km_pair_btn, kKmPairBtn.w, kKmPairBtn.h);
  lv_obj_set_style_bg_color(g_km_pair_btn, lv_color_hex(kPanel), 0);
  g_km_pair_label = label(g_km_pair_btn, "Pair", &font_plex_14, kText);
  lv_obj_add_event_cb(g_km_pair_btn, [](lv_event_t*) { g_host->command(Cmd::ArmPair, 0); }, LV_EVENT_CLICKED, nullptr);
  lv_obj_t* disc = lv_button_create(scr);
  lv_obj_set_pos(disc, kKmDropBtn.x, kKmDropBtn.y);
  lv_obj_set_size(disc, kKmDropBtn.w, kKmDropBtn.h);
  lv_obj_set_style_bg_color(disc, lv_color_hex(0x3A2A2A), 0);
  label(disc, "Drop", &font_plex_14, kText);
  lv_obj_add_event_cb(disc, [](lv_event_t*) { g_host->command(Cmd::DisconnectClient, 0); }, LV_EVENT_CLICKED, nullptr);
  return scr;
}

lv_obj_t* make_jiggler() {
  lv_obj_t* scr = screen_base();
  lv_obj_add_event_cb(scr, on_gesture, LV_EVENT_GESTURE, nullptr);
  lv_obj_t* title = label(scr, "Mouse Jiggler", &font_chakra_20, kAmber);
  lv_obj_set_pos(title, kJigTitle.x, kJigTitle.y);
  lv_obj_set_width(title, kJigTitle.w);
  lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);

  g_jig_state = label(scr, "", &font_chakra_16, kText);
  lv_obj_set_pos(g_jig_state, kJigState.x, kJigState.y);
  lv_obj_set_width(g_jig_state, kJigState.w);
  lv_obj_set_style_text_align(g_jig_state, LV_TEXT_ALIGN_CENTER, 0);
  g_jig_counts = label(scr, "", &font_plex_14, kDim);
  lv_obj_set_pos(g_jig_counts, kJigCounts.x, kJigCounts.y);
  lv_obj_set_width(g_jig_counts, kJigCounts.w);
  lv_obj_set_style_text_align(g_jig_counts, LV_TEXT_ALIGN_CENTER, 0);

  g_run_btn = lv_button_create(scr);
  lv_obj_set_pos(g_run_btn, kJigRunBtn.x, kJigRunBtn.y);
  lv_obj_set_size(g_run_btn, kJigRunBtn.w, kJigRunBtn.h);
  lv_obj_set_style_bg_color(g_run_btn, lv_color_hex(0x2A2E34), 0);
  lv_obj_set_style_radius(g_run_btn, 10, 0);
  g_run_label = label(g_run_btn, "stopped", &font_plex_14, 0x8E97A3);
  lv_obj_add_event_cb(g_run_btn, [](lv_event_t*) {
    g_host->command(Cmd::JigglerEnabled, g_host->model().jiggler_run ? 0 : 1);
  }, LV_EVENT_CLICKED, nullptr);

  struct Press {
    const char* text;
    RoundRect rect;
    Cmd cmd;
  };
  static const Press kPresses[] = {
      {"Jiggle", kJigNowBtn, Cmd::JiggleNow},
      {"Nudge", kJigNudgeBtn, Cmd::Nudge},
  };
  for (const Press& press : kPresses) {
    lv_obj_t* btn = lv_button_create(scr);
    lv_obj_set_pos(btn, press.rect.x, press.rect.y);
    lv_obj_set_size(btn, press.rect.w, press.rect.h);
    lv_obj_set_style_bg_color(btn, lv_color_hex(kPanel), 0);
    lv_obj_set_style_radius(btn, 10, 0);
    label(btn, press.text, &font_plex_14, kText);
    lv_obj_add_event_cb(btn, [](lv_event_t* e) {
      const Press* spec = static_cast<const Press*>(lv_event_get_user_data(e));
      g_host->command(spec->cmd, 0);
    }, LV_EVENT_CLICKED, const_cast<Press*>(&press));
  }
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
  lv_obj_set_style_radius(btn, 8, 0);
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
  lv_obj_t* title = label(scr, "Configuration", &font_chakra_20, kSlate);
  lv_obj_set_pos(title, kCfgTitle.x, kCfgTitle.y);
  lv_obj_set_width(title, kCfgTitle.w);
  lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);

  // One Wi-Fi button at a time: setup while offline, forget while connected.
  g_cfg_setup = action_button(scr, "Wi-Fi setup", kCfgSetup.x, kCfgSetup.y, kCfgSetup.w, on_cmd_button,
                              reinterpret_cast<void*>(static_cast<uintptr_t>(Cmd::StartImprov)));
  g_cfg_forget = action_button(scr, "Forget Wi-Fi", kCfgSetup.x, kCfgSetup.y, kCfgSetup.w, on_cmd_button,
                               reinterpret_cast<void*>(static_cast<uintptr_t>(Cmd::ForgetWifi)));
  lv_obj_set_hidden(g_cfg_forget, true);
  const char* timeout = timeout_caption(g_host->model().screen_off_sec);
  lv_obj_t* blank = action_button(scr, timeout != nullptr ? timeout : "Timeout", kCfgTimeout.x, kCfgTimeout.y,
                                  kCfgTimeout.w, cycle_timeout, nullptr);
  lv_obj_set_style_pad_hor(blank, 2, 0);
  g_timeout_label = lv_obj_get_child(blank, 0);
  lv_label_set_long_mode(g_timeout_label, LV_LABEL_LONG_CLIP);

  action_button(scr, "Reboot", kCfgReboot.x, kCfgReboot.y, kCfgReboot.w, on_cmd_button,
                reinterpret_cast<void*>(static_cast<uintptr_t>(Cmd::Reboot)));
  lv_obj_t* slider = lv_slider_create(scr);
  lv_obj_set_pos(slider, kCfgSlider.x, kCfgSlider.y);
  lv_obj_set_width(slider, kCfgSlider.w);
  lv_slider_set_range(slider, 1, 16);
  lv_slider_set_value(slider, g_host->model().backlight, LV_ANIM_OFF);
  lv_obj_add_event_cb(slider, [](lv_event_t* e) {
    g_host->command(Cmd::Backlight, lv_slider_get_value(static_cast<lv_obj_t*>(lv_event_get_target(e))));
  }, LV_EVENT_RELEASED, nullptr);

  g_cfg_info = label(scr, "", &font_plex_14, kDim);
  lv_obj_set_pos(g_cfg_info, kCfgInfo.x, kCfgInfo.y);
  lv_obj_set_width(g_cfg_info, kCfgInfo.w);
  lv_label_set_long_mode(g_cfg_info, LV_LABEL_LONG_CLIP);

  char stamp[64];
  snprintf(stamp, sizeof(stamp), "%s  %s", kFirmwareVersion, build_timestamp());
  lv_obj_t* build = label(scr, stamp, &font_plex_14, kDim);
  lv_obj_set_pos(build, kCfgStamp.x, kCfgStamp.y);
  lv_obj_set_width(build, kCfgStamp.w);
  lv_obj_set_style_text_align(build, LV_TEXT_ALIGN_CENTER, 0);
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
  // Three labels along the vertical axis where the chord is wide; no full
  // width bar, which a round panel does not have room for.
  g_wifi = label(lv_layer_top(), "Wi-Fi", &font_plex_14, kDim);
  lv_obj_set_pos(g_wifi, kWifi.x, kWifi.y);
  lv_obj_set_width(g_wifi, kWifi.w);
  lv_label_set_long_mode(g_wifi, LV_LABEL_LONG_CLIP);
  g_bat = label(lv_layer_top(), "--", &font_plex_14, kText);
  lv_obj_set_pos(g_bat, kBat.x, kBat.y);
  lv_obj_set_width(g_bat, kBat.w);
  lv_obj_set_style_text_align(g_bat, LV_TEXT_ALIGN_RIGHT, 0);
  g_ip = label(lv_layer_top(), "", &font_plex_14, kText);
  lv_obj_set_pos(g_ip, kIp.x, kIp.y);
  lv_obj_set_width(g_ip, kIp.w);
  lv_obj_set_style_text_align(g_ip, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_long_mode(g_ip, LV_LABEL_LONG_CLIP);
}

}  // namespace kmj

#endif  // KMJ_BOARD_WS_185B
