#include "app/runtime.hpp"

#include <Arduino.h>
#include <esp_random.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <stdio.h>
#include <string.h>

#include "app/debug_log.hpp"
#include "app/hid_device.hpp"
#include "app/improv_ble.hpp"
#include "app/km_server.hpp"
#include "app/net_wifi.hpp"
#include "app/settings.hpp"
#include "app/setup_mode.hpp"
#include "hal/backlight.hpp"
#include "hal/display_lgfx.hpp"
#include "hal/pmu_sy6970.hpp"
#include "logic/jiggler.hpp"
#include "pins.hpp"
#include "hal/touch_cst.hpp"
#include "port/lvgl_port.hpp"
#include "ui/ui.hpp"

#include <lvgl.h>

namespace kmj {
namespace {

struct CmdMsg {
  Cmd cmd;
  int arg;
};

QueueHandle_t g_queue = nullptr;
SemaphoreHandle_t g_lock = nullptr;
Model g_live;
Model g_published;
Model g_ui;
Jiggler g_jiggler;
uint32_t g_prev_keys = 0;
uint32_t g_pmu_ms = 0;
bool g_setup_fail = false;
volatile bool g_ui_started = false;
volatile bool g_halting_for_bootloader = false;
volatile bool g_ui_halted = false;

class RuntimeHost : public Host {
 public:
  const Model& model() const override { return g_ui; }
  void command(Cmd cmd, int arg) override {
    const CmdMsg msg{cmd, arg};
    if (g_queue != nullptr) {
      xQueueSend(g_queue, &msg, 0);
    }
  }
};

RuntimeHost g_host;

long jiggler_rand(void*, long lo, long hi) {
  if (hi <= lo) {
    return lo;
  }
  const uint32_t span = static_cast<uint32_t>(hi - lo);
  const uint32_t limit = UINT32_MAX - (UINT32_MAX % span);
  uint32_t r = esp_random();
  while (r >= limit) {
    r = esp_random();
  }
  return lo + static_cast<long>(r % span);
}

void push_trace(int8_t value) {
  g_live.trace[g_live.trace_i % 48] = value;
  g_live.trace_i = static_cast<uint8_t>((g_live.trace_i + 1) % 48);
}

void leave_functions(bool stop_listener) {
  g_jiggler.stop(millis());
  km_set_inject(false);
  if (stop_listener) {
    km_arm(false);
    km_stop();
    g_live.pair_armed = false;
    net_advertise(false, g_live.device_name, kKmPort);
  }
}

void apply(const CmdMsg& msg) {
  switch (msg.cmd) {
    case Cmd::EnterHome:
      leave_functions(true);
      g_live.mode = Mode::Home;
      break;
    case Cmd::EnterKm:
      leave_functions(true);
      g_live.mode = Mode::Km;
      km_start();
      km_set_inject(true);
      net_advertise(true, settings().device_name, kKmPort);
      break;
    case Cmd::EnterJiggler:
      leave_functions(true);
      g_live.mode = Mode::Jiggler;
      g_jiggler.start(millis());
      break;
    case Cmd::EnterConfig:
      leave_functions(true);
      g_live.mode = Mode::Config;
      break;
    case Cmd::ScreenOn:
      g_live.screen_on = true;
      backlight_set(static_cast<uint8_t>(g_live.backlight));
      break;
    case Cmd::ScreenOff:
      g_live.screen_on = false;
      backlight_set(0);
      break;
    case Cmd::Backlight:
      g_live.backlight = msg.arg < 1 ? 1 : (msg.arg > 16 ? 16 : msg.arg);
      settings_save_display(g_live.backlight, g_live.screen_off_sec);
      if (g_live.screen_on) {
        backlight_set(static_cast<uint8_t>(g_live.backlight));
      }
      break;
    case Cmd::ScreenTimeout:
      g_live.screen_off_sec = msg.arg < 0 ? 0 : msg.arg;
      settings_save_display(g_live.backlight, g_live.screen_off_sec);
      break;
    case Cmd::JiggleNow:
      if (g_live.mode == Mode::Jiggler) {
        g_jiggler.trigger_jiggle();
      }
      break;
    case Cmd::Nudge:
      if (g_live.mode == Mode::Jiggler) {
        g_jiggler.trigger_nudge();
      }
      break;
    case Cmd::JigglerEnabled:
      if (msg.arg) {
        g_jiggler.start(millis());
      } else {
        g_jiggler.stop(millis());
      }
      break;
    case Cmd::UsbWanted:
      hid_set_usb_wanted(msg.arg != 0);
      break;
    case Cmd::DisconnectClient:
      km_disconnect();
      break;
    case Cmd::ArmPair:
      km_start();
      km_set_inject(g_live.mode == Mode::Km);
      km_arm(true);
      g_live.pair_armed = true;
      net_advertise(true, settings().device_name, kKmPort);
      break;
    case Cmd::ConfirmPair:
      km_confirm();
      break;
    case Cmd::CancelPair:
      km_cancel();
      km_arm(false);
      g_live.pair_armed = false;
      if (g_live.mode != Mode::Km) {
        km_stop();
        net_advertise(false, settings().device_name, kKmPort);
      }
      break;
    case Cmd::RemovePeer:
      settings_remove_peer(msg.arg);
      break;
    case Cmd::StartImprov:
      KMJ_LOG("cmd StartImprov mode=%d", static_cast<int>(g_live.mode));
      if (g_live.mode == Mode::Km) {
        break;
      }
      // Bluetooth does not fit beside the UI and Wi-Fi. Reboot into a build
      // of the firmware that runs only Wi-Fi setup, and come back when it ends.
      setup_mode_restart();
      break;
    case Cmd::ForgetWifi:
      settings_clear_wifi();
      net_forget();
      break;
    case Cmd::Reboot:
      ESP.restart();
      break;
  }
}

void refresh_live(uint32_t now) {
  const Settings& cfg = settings();
  snprintf(g_live.device_name, sizeof(g_live.device_name), "%s", cfg.device_name);
  snprintf(g_live.version, sizeof(g_live.version), "%s", kFirmwareVersion);
  snprintf(g_live.wifi_ssid, sizeof(g_live.wifi_ssid), "%s", cfg.ssid);
  g_live.wifi_saved = cfg.ssid[0] != '\0';
  g_live.wifi_connected = net_connected();
  g_live.rssi = net_rssi();
  net_ip(g_live.ip, sizeof(g_live.ip));
  net_mac(g_live.mac, sizeof(g_live.mac));
  g_live.usb_mounted = hid_mounted();
  g_live.usb_wanted = hid_usb_wanted();
  g_live.hid_tx = hid_tx();
  g_live.backlight = cfg.backlight;
  g_live.screen_off_sec = cfg.screen_off_sec;
  g_live.uptime_s = now / 1000u;
  g_live.heap_free = ESP.getFreeHeap();
  g_live.psram_free = ESP.getFreePsram();
  g_live.touch_ok = touch_ready();
  g_live.peer_count = cfg.peer_count;
  for (int i = 0; i < cfg.peer_count && i < 4; i++) {
    peer_label(cfg.peers[i], g_live.peers[i], sizeof(g_live.peers[i]));
  }
  g_live.setup_fail = g_setup_fail;
  if (g_setup_fail) {
    snprintf(g_live.setup_note, sizeof(g_live.setup_note), "%s", improv_fail_text());
  } else {
    g_live.setup_note[0] = '\0';
  }

  const KmView km = km_view();
  g_live.km_link = !km.listening ? KmLink::Off : (km.active ? KmLink::Capturing : (km.secure ? KmLink::Connected : KmLink::Listening));
  snprintf(g_live.km_client, sizeof(g_live.km_client), "%s", km.secure ? km.client : "");
  snprintf(g_live.km_addr, sizeof(g_live.km_addr), "%s", km.addr);
  g_live.km_keys = km.keys;
  g_live.km_mouse = km.moves;
  g_live.km_latency_ms = km.latency_ms;
  snprintf(g_live.last_key, sizeof(g_live.last_key), "%s", km.last_key);
  g_live.pair_show = km.pairing;
  if (km.pairing) {
    snprintf(g_live.pair_code, sizeof(g_live.pair_code), "%s", km.code);
  }
  if (strcmp(km.note, "paired") == 0 || strcmp(km.note, "rejected") == 0) {
    g_live.pair_armed = false;
  }
  if (km.note[0] != '\0') {
    snprintf(g_live.note, sizeof(g_live.note), "%s", km.note);
  }
  if (km.keys != g_prev_keys) {
    push_trace(18);
    g_prev_keys = km.keys;
  } else if ((now / 200u) % 2 == 0) {
    push_trace(km.active ? 8 : 2);
  }

  g_live.jiggler_run = g_jiggler.enabled();
  g_live.jiggler_moving = g_jiggler.moving();
  g_live.nudge = g_jiggler.nudging();
  g_live.next_jiggle_s = g_jiggler.countdown_s(now);
  g_live.run_left_s = g_jiggler.run_left_s(now);
  g_live.jiggle_count = g_jiggler.jiggle_count();
  g_live.time_up = g_jiggler.time_up();
  g_live.nudge_leg = g_jiggler.leg() + 1;
  g_live.nudge_legs = g_jiggler.leg_count();
  g_live.nudge_dx = g_jiggler.leg_dx();
  g_live.nudge_dy = g_jiggler.leg_dy();

  if (g_pmu_ms == 0 || now - g_pmu_ms >= 30000) {
    g_pmu_ms = now;
    const PowerSample power = pmu_read();
    g_live.pmu_ok = power.ok;
    g_live.battery_mv = power.battery_mv;
    g_live.battery_pct = power.battery_pct;
    g_live.charging = power.charging;
    g_live.vbus = power.vbus;
  }
}

void io_task(void*) {
  while (true) {
    CmdMsg msg;
    while (xQueueReceive(g_queue, &msg, 0) == pdTRUE) {
      apply(msg);
    }
    const uint32_t now = millis();
    net_poll();
    if (g_live.mode == Mode::Km || g_live.pair_armed) {
      km_poll(now);
      net_advertise(true, settings().device_name, kKmPort);
    }
    if (g_live.mode == Mode::Jiggler) {
      int dx = 0;
      int dy = 0;
      if (g_jiggler.poll(now, dx, dy)) {
        if (!hid_usb_wanted() || hid_mounted()) {
          hid_mouse(dx, dy);
          g_jiggler.ack();
        }
      }
    }
    hid_service();
    refresh_live(now);
    if (xSemaphoreTake(g_lock, pdMS_TO_TICKS(5)) == pdTRUE) {
      g_published = g_live;
      xSemaphoreGive(g_lock);
    }
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

void ui_task(void*) {
  lvgl_port_init();
  ui_init(g_host);
  g_ui_started = true;
  while (true) {
    if (g_halting_for_bootloader) {
      if (backlight_ready() && backlight_level() == 0) {
        const int level = settings().backlight > 0 ? settings().backlight : kBacklightMax;
        backlight_set(static_cast<uint8_t>(level));
      }
      display_show_bootloader();
      g_ui_halted = true;
      vTaskSuspend(nullptr);
    }
    if (xSemaphoreTake(g_lock, pdMS_TO_TICKS(5)) == pdTRUE) {
      g_ui = g_published;
      xSemaphoreGive(g_lock);
    }
    lv_timer_handler();
    ui_tick(millis());
    vTaskDelay(pdMS_TO_TICKS(5));
  }
}

}  // namespace

Host& runtime_host() { return g_host; }

void runtime_start() {
  const Settings& cfg = settings();
  snprintf(g_live.device_name, sizeof(g_live.device_name), "%s", cfg.device_name);
  g_live.backlight = cfg.backlight;
  g_live.screen_off_sec = cfg.screen_off_sec;
  g_live.screen_on = true;
  g_published = g_live;
  g_ui = g_live;
  improv_install_fault_hook();
  if (improv_capture_fault()) {
    g_setup_fail = true;
    KMJ_LOG("previous run faulted: %s", improv_fail_text());
  }
  g_queue = xQueueCreate(16, sizeof(CmdMsg));
  g_lock = xSemaphoreCreateMutex();
  g_jiggler.set_random(jiggler_rand, nullptr);
  TaskHandle_t io_handle = nullptr;
  TaskHandle_t ui_handle = nullptr;
  xTaskCreatePinnedToCore(io_task, "kmj-io", 8192, nullptr, 4, &io_handle, 0);
  xTaskCreatePinnedToCore(ui_task, "kmj-ui", 12288, nullptr, 3, &ui_handle, 1);
  KMJ_LOG_WATCH("io", io_handle);
  KMJ_LOG_WATCH("ui", ui_handle);
}

void runtime_show_bootloader_and_halt() {
  if (g_ui_started) {
    g_halting_for_bootloader = true;
    const uint32_t deadline = millis() + 250;
    while (!g_ui_halted && static_cast<int32_t>(millis() - deadline) < 0) {
      vTaskDelay(pdMS_TO_TICKS(5));
    }
    if (g_ui_halted) {
      return;
    }
  }
  if (backlight_ready() && backlight_level() == 0) {
    const int level = settings().backlight > 0 ? settings().backlight : kBacklightMax;
    backlight_set(static_cast<uint8_t>(level));
  }
  display_show_bootloader();
}

}  // namespace kmj
