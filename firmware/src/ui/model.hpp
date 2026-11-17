#pragma once

#include <stdint.h>

namespace kmj {

enum class Mode : uint8_t { Home, Km, Jiggler, Config };

enum class KmLink : uint8_t { Off, Listening, Connected, Capturing };

enum class Cmd : uint8_t {
  EnterHome,
  EnterKm,
  EnterJiggler,
  EnterConfig,
  ScreenOn,
  ScreenOff,
  Backlight,
  ScreenTimeout,
  JiggleNow,
  Nudge,
  JigglerEnabled,
  UsbWanted,
  DisconnectClient,
  ArmPair,
  ConfirmPair,
  CancelPair,
  RemovePeer,
  StartImprov,
  ImprovAllow,
  ImprovDeny,
  ForgetWifi,
  Reboot,
};

struct Model {
  Mode mode = Mode::Home;
  bool screen_on = true;
  int backlight = 16;
  int screen_off_sec = 0;
  char wifi_ssid[33] = {};
  bool wifi_connected = false;
  bool wifi_saved = false;
  int rssi = 0;
  char ip[16] = {};
  char mac[18] = {};
  bool usb_mounted = false;
  bool usb_wanted = true;
  int battery_mv = 0;
  int battery_pct = -1;
  bool charging = false;
  bool vbus = false;
  bool pmu_ok = false;
  char km_client[32] = {};
  char km_addr[24] = {};
  KmLink km_link = KmLink::Off;
  uint32_t km_keys = 0;
  uint32_t km_mouse = 0;
  uint32_t km_latency_ms = 0;
  char last_key[8] = {};
  int8_t trace[48] = {};
  uint8_t trace_i = 0;
  bool jiggler_run = false;
  bool jiggler_moving = false;
  bool nudge = false;
  int next_jiggle_s = -1;
  uint32_t run_left_s = 0;
  uint32_t hid_tx = 0;
  uint32_t jiggle_count = 0;
  int nudge_leg = 0;
  int nudge_legs = 0;
  int nudge_dx = 0;
  int nudge_dy = 0;
  bool time_up = false;
  bool pair_armed = false;
  bool pair_show = false;
  char pair_code[8] = {};
  char peers[4][18] = {};
  int peer_count = 0;
  bool improv_on = false;
  bool improv_ask = false;
  bool setup_wait = false;
  bool setup_fail = false;
  char setup_note[48] = {};
  char setup_line[48] = {};
  uint8_t improv_state = 0;
  char device_name[20] = "KMJ";
  char version[12] = "0.1.0";
  uint32_t heap_free = 0;
  uint32_t psram_free = 0;
  uint32_t uptime_s = 0;
  bool touch_ok = false;
  char note[40] = {};
};

struct Host {
  virtual ~Host() = default;
  virtual const Model& model() const = 0;
  virtual void command(Cmd cmd, int arg) = 0;
};

}  // namespace kmj
