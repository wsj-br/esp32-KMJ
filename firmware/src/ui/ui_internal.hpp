#pragma once

// Shared between ui.cpp (state, painting, navigation) and the per-board screen
// construction files (ui/screens_wide.cpp, ui/screens_round.cpp). Internal to
// the UI layer; the public interface stays in ui/ui.hpp.

#include <lvgl.h>

#include "ui/ui.hpp"

extern "C" {
extern const lv_font_t font_chakra_16;
extern const lv_font_t font_chakra_20;
extern const lv_font_t font_plex_14;
}

namespace kmj {

constexpr uint32_t kBg = 0x121418;
constexpr uint32_t kPanel = 0x1B2028;
constexpr uint32_t kLine = 0x31404C;
constexpr uint32_t kText = 0xE7E4DC;
constexpr uint32_t kDim = 0x8E97A3;
constexpr uint32_t kCyan = 0x3EE0C5;
constexpr uint32_t kAmber = 0xF0A202;
constexpr uint32_t kSlate = 0xC9D2DC;

extern Host* g_host;

extern lv_obj_t* g_wifi;
extern lv_obj_t* g_ip;
extern lv_obj_t* g_bat;
extern lv_obj_t* g_km_state;
extern lv_obj_t* g_km_meta;
extern lv_obj_t* g_km_counts;
extern lv_obj_t* g_km_key;
extern lv_obj_t* g_km_paired;
extern lv_obj_t* g_km_pair_btn;
extern lv_obj_t* g_km_pair_label;
extern lv_obj_t* g_line;
extern lv_obj_t* g_jig_state;
extern lv_obj_t* g_jig_counts;
extern lv_obj_t* g_run_btn;
extern lv_obj_t* g_run_label;
extern lv_obj_t* g_usb_label;
extern lv_obj_t* g_timeout_label;
extern lv_obj_t* g_home_km;
extern lv_obj_t* g_home_jig;
extern lv_obj_t* g_home_cfg;
extern lv_obj_t* g_cfg_info;
extern lv_obj_t* g_cfg_setup;
extern lv_obj_t* g_cfg_forget;
extern lv_obj_t* g_pair;
extern lv_obj_t* g_pair_code;
extern lv_point_precise_t g_points[48];

// Shared widget builders.
lv_obj_t* label(lv_obj_t* parent, const char* text, const lv_font_t* font, uint32_t color);
lv_obj_t* screen_base();
void style_tile(lv_obj_t* obj, uint32_t accent);
lv_obj_t* back_button(lv_obj_t* parent);
lv_obj_t* make_trace(lv_obj_t* parent);
lv_obj_t* action_button(lv_obj_t* parent, const char* text, int x, int y, int w, lv_event_cb_t cb, void* user);
const char* timeout_caption(int sec);

// Navigation used by widget callbacks; defined in ui.cpp.
void go(Mode mode);

// Per-board screen construction, implemented in ui/screens_wide.cpp or
// ui/screens_round.cpp depending on the selected board.
lv_obj_t* make_home();
lv_obj_t* make_km();
lv_obj_t* make_jiggler();
lv_obj_t* make_config();
void build_status();
void ensure_overlays();
void clear_screen_ptrs();

}  // namespace kmj
