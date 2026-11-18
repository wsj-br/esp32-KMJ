#include "app/improv_ble.hpp"

#include <stdio.h>
#include <string.h>

#include <NimBLEDevice.h>
#include <WiFi.h>
#include <esp_attr.h>
#include <esp_bt.h>
#include <esp_heap_caps.h>
#include <esp_system.h>

#include <string>

#include "esp32-hal-alloc-ble-mem.h"
#include "esp32-hal.h"
#include "app/debug_log.hpp"
#include "sdkconfig.h"

static_assert(CONFIG_BT_NIMBLE_MSYS_2_BLOCK_COUNT == 4, "NimBLE pool override was not applied");

#include "app/net_wifi.hpp"
#include "app/settings.hpp"
#include "logic/improv_rpc.hpp"
#include "pins.hpp"

// Every NimBLE buffer the controller can be handed must live in internal RAM.
// A PSRAM block here makes the controller raise a hardware error, and that
// used to reset the board.
void* nimble_mem(size_t size, bool clear) {
  const uint32_t internal = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
  void* ptr = clear ? heap_caps_calloc(1, size, internal) : heap_caps_malloc(size, internal);
  if (ptr == nullptr) {
    const uint32_t external = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
    ptr = clear ? heap_caps_calloc(1, size, external) : heap_caps_malloc(size, external);
  }
  return ptr;
}

extern "C" void* __wrap_nimble_platform_mem_malloc(size_t size) { return nimble_mem(size, false); }

extern "C" void* __wrap_nimble_platform_mem_calloc(size_t n, size_t size) {
  return nimble_mem(n * size, true);
}

extern "C" void __wrap_nimble_platform_mem_free(void* ptr) { heap_caps_free(ptr); }

struct BootFault {
  uint32_t magic;
  uint32_t pc;
  uint8_t stage;
  uint8_t core;
  uint8_t trace_len;
  char reason[20];
  uint32_t trace[20];
};

constexpr uint32_t kFaultMagic = 0x4B4D4A46;
RTC_NOINIT_ATTR BootFault g_boot_fault;

void copy_reason(char* dst, size_t cap, const char* src) {
  size_t i = 0;
  if (src != nullptr) {
    while (i + 1 < cap && src[i] != '\0') {
      dst[i] = src[i];
      i++;
    }
  }
  dst[i] = '\0';
}

extern "C" void kmj_on_panic(arduino_panic_info_t* info, void*) {
  g_boot_fault.magic = kFaultMagic;
  g_boot_fault.pc = info != nullptr && info->pc != nullptr ? static_cast<uint32_t>(reinterpret_cast<uintptr_t>(info->pc)) : 0;
  g_boot_fault.core = info != nullptr ? static_cast<uint8_t>(info->core) : 0;
  copy_reason(g_boot_fault.reason, sizeof(g_boot_fault.reason), info != nullptr ? info->reason : nullptr);
  g_boot_fault.trace_len = 0;
  if (info != nullptr) {
    const unsigned cap = sizeof(g_boot_fault.trace) / sizeof(g_boot_fault.trace[0]);
    for (unsigned i = 0; i < info->backtrace_len && i < cap; i++) {
      g_boot_fault.trace[i] = info->backtrace[i];
      g_boot_fault.trace_len = static_cast<uint8_t>(i + 1);
    }
  }
}

namespace kmj {
namespace {

constexpr char kService[] = "00467768-6228-2272-4663-277478268000";
constexpr char kStateUuid[] = "00467768-6228-2272-4663-277478268001";
constexpr char kErrorUuid[] = "00467768-6228-2272-4663-277478268002";
constexpr char kRpcUuid[] = "00467768-6228-2272-4663-277478268003";
constexpr char kResultUuid[] = "00467768-6228-2272-4663-277478268004";
constexpr char kCapsUuid[] = "00467768-6228-2272-4663-277478268005";

constexpr uint8_t kAuthRequired = 0x01;
constexpr uint8_t kAuthorized = 0x02;
constexpr uint8_t kProvisioning = 0x03;
constexpr uint8_t kProvisioned = 0x04;
constexpr uint8_t kCaps = 0x0F;

NimBLECharacteristic* g_state = nullptr;
NimBLECharacteristic* g_error = nullptr;
NimBLECharacteristic* g_result = nullptr;
bool g_on = false;
uint8_t g_state_b = 0;
uint8_t g_error_b = 0;
uint32_t g_auth_deadline = 0;
uint32_t g_prov_start = 0;
uint32_t g_stop_at = 0;
bool g_ask = false;
enum class Notice : uint8_t { None, Declined, TimedOut };
Notice g_notice = Notice::None;
bool g_wifi_pending = false;
char g_ssid[33] = {};
char g_pass[65] = {};
uint8_t g_acc[220] = {};
size_t g_acc_n = 0;
void (*g_identify)() = nullptr;
bool g_scanning = false;
char g_fail[48] = {};
uint32_t g_adv_ms = 0;

void fault_stage(uint8_t stage) {
  g_boot_fault.magic = kFaultMagic;
  g_boot_fault.stage = stage;
  if (stage == 1) {
    g_boot_fault.trace_len = 0;
  }
}

void publish_state(uint8_t state) {
  g_state_b = state;
  if (g_state == nullptr) {
    return;
  }
  g_state->setValue(&state, 1);
  if (g_on) {
    g_state->notify();
  }
}

void publish_error(uint8_t err) {
  g_error_b = err;
  if (g_error == nullptr) {
    return;
  }
  g_error->setValue(&err, 1);
  if (g_on) {
    g_error->notify();
  }
}

void publish_result(uint8_t cmd, const char* const* strings, size_t count) {
  uint8_t buf[240];
  const size_t n = improv_result(buf, sizeof(buf), cmd, strings, count);
  if (n > 0 && g_result != nullptr) {
    g_result->setValue(buf, n);
    g_result->notify();
  }
}

bool advertise() {
  NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
  if (adv->isAdvertising()) {
    adv->stop();
    for (int i = 0; i < 40 && adv->isAdvertising(); i++) {
      delay(10);
    }
  }
  // Build the packet in the advertising object, then let start() hand it to
  // the controller. Pushing the bytes before start() leaves the radio silent.
  const char* name = settings().device_name[0] != '\0' ? settings().device_name : "KMJ";
  uint8_t service_data[6] = {g_state_b, kCaps, 0, 0, 0, 0};
  adv->clearData();
  adv->enableScanResponse(true);
  adv->addServiceUUID(kService);
  adv->setServiceData(NimBLEUUID(static_cast<uint16_t>(0x4677)), service_data, sizeof(service_data));
  adv->setName(name);
  adv->setMinInterval(0x20);
  adv->setMaxInterval(0x40);
  if (adv->start()) {
    return true;
  }
  delay(100);
  return adv->start();
}

void handle_packet(const ImprovPacket& pkt) {
  if (pkt.command == 0x01) {
    if (g_state_b != kAuthorized) {
      publish_error(0x04);
      return;
    }
    if (!improv_wifi_credentials(pkt.data, pkt.data_len, g_ssid, sizeof(g_ssid), g_pass, sizeof(g_pass))) {
      publish_error(0x01);
      return;
    }
    g_wifi_pending = true;
    publish_error(0x00);
    publish_state(kProvisioning);
    g_prov_start = millis();
    return;
  }
  if (pkt.command == 0x02) {
    if (g_identify != nullptr) {
      g_identify();
    }
    return;
  }
  if (pkt.command == 0x03) {
    const char* info[] = {"KMJ", kFirmwareVersion, "lilygo-t-display-s3-pro", settings().device_name};
    publish_result(0x03, info, 4);
    return;
  }
  if (pkt.command == 0x04) {
    net_resume();
    WiFi.scanDelete();
    WiFi.scanNetworks(true);
    g_scanning = true;
    return;
  }
  if (pkt.command == 0x05) {
    if (g_state_b != kAuthorized && pkt.data_len > 0) {
      publish_error(0x04);
      return;
    }
    if (pkt.data_len == 0) {
      const char* host[] = {settings().hostname};
      publish_result(0x05, host, 1);
      return;
    }
    char host[64];
    const size_t n = pkt.data_len < sizeof(host) - 1 ? pkt.data_len : sizeof(host) - 1;
    memcpy(host, pkt.data, n);
    host[n] = '\0';
    if (!hostname_ok(host)) {
      publish_error(0x05);
      return;
    }
    settings_save_name(host, settings().device_name);
    const char* out[] = {settings().hostname};
    publish_result(0x05, out, 1);
    g_auth_deadline = millis() + kImprovAuthMs;
    return;
  }
  if (pkt.command == 0x06) {
    if (g_state_b != kAuthorized && pkt.data_len > 0) {
      publish_error(0x04);
      return;
    }
    if (pkt.data_len == 0) {
      const char* name[] = {settings().device_name};
      publish_result(0x06, name, 1);
      return;
    }
    char name[20];
    const size_t n = pkt.data_len < sizeof(name) - 1 ? pkt.data_len : sizeof(name) - 1;
    memcpy(name, pkt.data, n);
    name[n] = '\0';
    settings_save_name(settings().hostname, name);
    const char* out[] = {settings().device_name};
    publish_result(0x06, out, 1);
    g_auth_deadline = millis() + kImprovAuthMs;
    return;
  }
  publish_error(0x02);
}

class RpcCallbacks : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic* chr, NimBLEConnInfo&) override {
    const NimBLEAttValue value = chr->getValue();
    if (g_acc_n + value.length() > sizeof(g_acc)) {
      g_acc_n = 0;
      publish_error(0x01);
      return;
    }
    memcpy(g_acc + g_acc_n, value.data(), value.length());
    g_acc_n += value.length();
    const ImprovPacket pkt = improv_parse(g_acc, g_acc_n);
    if (!pkt.valid) {
      if (g_acc_n >= 3 && g_acc_n >= static_cast<size_t>(g_acc[1]) + 3) {
        g_acc_n = 0;
        publish_error(0x01);
      }
      return;
    }
    g_acc_n = 0;
    handle_packet(pkt);
  }
};

RpcCallbacks g_rpc;

void finish_scan() {
  const int found = WiFi.scanComplete();
  if (found < 0) {
    return;
  }
  g_scanning = false;
  const char* strings[45];
  char storage[15][48];
  int count = 0;
  const int limit = found > 15 ? 15 : found;
  for (int i = 0; i < limit; i++) {
    snprintf(storage[count], sizeof(storage[count]), "%s", WiFi.SSID(i).c_str());
    strings[count] = storage[count];
    count++;
    snprintf(storage[count], sizeof(storage[count]), "%d", WiFi.RSSI(i));
    strings[count] = storage[count];
    count++;
    const wifi_auth_mode_t auth = WiFi.encryptionType(i);
    const char* kind = "NO";
    if (auth == WIFI_AUTH_WEP) kind = "WEP";
    else if (auth == WIFI_AUTH_WPA_PSK) kind = "WPA";
    else if (auth == WIFI_AUTH_WPA2_PSK) kind = "WPA2";
    else if (auth == WIFI_AUTH_WPA_WPA2_PSK) kind = "WPA/WPA2";
    else if (auth == WIFI_AUTH_WPA3_PSK) kind = "WPA3";
    else if (auth == WIFI_AUTH_WPA2_ENTERPRISE) kind = "WPA2 EAP";
    snprintf(storage[count], sizeof(storage[count]), "%s", kind);
    strings[count] = storage[count];
    count++;
  }
  publish_result(0x04, strings, static_cast<size_t>(count));
  WiFi.scanDelete();
}

}  // namespace

void improv_install_fault_hook() { set_arduino_panic_handler(kmj_on_panic, nullptr); }

bool improv_capture_fault() {
  const esp_reset_reason_t why = esp_reset_reason();
  const bool fault = why == ESP_RST_PANIC || why == ESP_RST_INT_WDT || why == ESP_RST_TASK_WDT || why == ESP_RST_WDT ||
                     why == ESP_RST_BROWNOUT;
  if (!fault || g_boot_fault.magic != kFaultMagic || g_boot_fault.stage == 0) {
    return false;
  }
  const char* kind = "reset";
  if (why == ESP_RST_PANIC) {
    kind = g_boot_fault.reason[0] != '\0' ? g_boot_fault.reason : "panic";
  } else if (why == ESP_RST_INT_WDT) {
    kind = "int wdt";
  } else if (why == ESP_RST_TASK_WDT) {
    kind = "task wdt";
  } else if (why == ESP_RST_BROWNOUT) {
    kind = "brownout";
  } else if (why == ESP_RST_WDT) {
    kind = "wdt";
  }
  snprintf(g_fail, sizeof(g_fail), "%s st%u", kind, g_boot_fault.stage);
  KMJ_LOG("fault capture: %s stage %u pc 0x%08x core %u", kind, static_cast<unsigned>(g_boot_fault.stage),
          static_cast<unsigned>(g_boot_fault.pc), static_cast<unsigned>(g_boot_fault.core));
  for (unsigned i = 0; i < g_boot_fault.trace_len && i < 20; i++) {
    KMJ_LOG("  bt[%u] 0x%08x", i, static_cast<unsigned>(g_boot_fault.trace[i]));
  }
  g_boot_fault.stage = 0;
  return true;
}

void improv_set_identify(void (*fn)()) { g_identify = fn; }

const char* improv_fail_text() { return g_fail; }

bool improv_start(bool keep_wifi) {
  g_notice = Notice::None;
  if (g_on) {
    if (g_state_b == kAuthRequired) {
      g_ask = true;
    }
    return true;
  }
  fault_stage(1);
  net_hold(true);
  if (!keep_wifi) {
    net_pause();
  }
  fault_stage(2);
  KMJ_LOG_HEAP("before ble");
  const bool ready = NimBLEDevice::init(settings().device_name);
  KMJ_LOG("ble: init returned %d, controller status %d", ready ? 1 : 0, static_cast<int>(esp_bt_controller_get_status()));
  KMJ_LOG_HEAP("after init");
  if (!ready) {
    const int status = static_cast<int>(esp_bt_controller_get_status());
    const unsigned kb =
        static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL) / 1024u);
    snprintf(g_fail, sizeof(g_fail), "Bluetooth did not start (%d, %uk)", status, kb);
    if (esp_bt_controller_get_status() != ESP_BT_CONTROLLER_STATUS_IDLE) {
      esp_bt_controller_disable();
      esp_bt_controller_deinit();
    }
    net_hold(false);
    net_resume();
    fault_stage(0);
    return false;
  }
  g_fail[0] = '\0';
  fault_stage(3);
  KMJ_LOG("ble: create server");
  NimBLEServer* server = NimBLEDevice::createServer();
  NimBLEService* service = server->createService(kService);
  g_state = service->createCharacteristic(kStateUuid, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);
  g_error = service->createCharacteristic(kErrorUuid, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);
  NimBLECharacteristic* rpc =
      service->createCharacteristic(kRpcUuid, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR);
  rpc->setCallbacks(&g_rpc);
  g_result = service->createCharacteristic(kResultUuid, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);
  NimBLECharacteristic* caps = service->createCharacteristic(kCapsUuid, NIMBLE_PROPERTY::READ);
  const uint8_t cap = kCaps;
  caps->setValue(&cap, 1);
  publish_state(kAuthRequired);
  publish_error(0x00);
  server->start();
  g_on = true;
  g_ask = true;
  g_stop_at = 0;
  fault_stage(4);
  KMJ_LOG("ble: advertise");
  const bool advertising = advertise();
  KMJ_LOG("ble: advertise returned %d", advertising ? 1 : 0);
  return true;
}

void improv_stop() {
  if (!g_on) {
    return;
  }
  NimBLEDevice::getAdvertising()->stop();
  NimBLEDevice::deinit(true);
  fault_stage(0);
  net_hold(false);
  net_resume();
  g_state = nullptr;
  g_error = nullptr;
  g_result = nullptr;
  g_on = false;
  g_ask = false;
  g_state_b = 0;
  g_scanning = false;
}

void improv_authorize(bool allow) {
  g_ask = false;
  if (!allow) {
    g_notice = Notice::Declined;
    improv_stop();
    return;
  }
  if (!g_on) {
    return;
  }
  publish_state(kAuthorized);
  publish_error(0x00);
  g_auth_deadline = millis() + kImprovAuthMs;
  advertise();
}

void improv_poll(uint32_t now_ms) {
  if (!g_on) {
    return;
  }
  if (!NimBLEDevice::getAdvertising()->isAdvertising() && (g_adv_ms == 0 || now_ms - g_adv_ms >= 1000)) {
    g_adv_ms = now_ms;
    advertise();
  }
  if (g_scanning) {
    finish_scan();
  }
  if (g_state_b == kAuthorized && g_auth_deadline != 0 && now_ms > g_auth_deadline) {
    g_notice = Notice::TimedOut;
    improv_stop();
    return;
  }
  if (g_state_b == kProvisioning) {
    if (net_connected()) {
      char url[40];
      char ip[16];
      net_ip(ip, sizeof(ip));
      snprintf(url, sizeof(url), "http://%s/", ip);
      const char* strings[] = {url};
      publish_result(0x01, strings, 1);
      publish_state(kProvisioned);
      advertise();
      g_stop_at = now_ms + 4000;
    } else if (now_ms - g_prov_start > 20000) {
      publish_error(0x03);
      publish_state(kAuthorized);
      g_auth_deadline = now_ms + kImprovAuthMs;
    }
  }
  if (g_stop_at != 0 && now_ms > g_stop_at) {
    improv_stop();
  }
}

bool improv_active() { return g_on; }
bool improv_needs_auth() { return g_on && g_ask; }
uint8_t improv_state() { return g_state_b; }

const char* improv_status_line() {
  if (g_on && g_ask) {
    return "Waiting for Wi-Fi config via Bluetooth";
  }
  if (g_on && g_state_b == kProvisioning) {
    return "Joining the network";
  }
  if (g_on && g_state_b == kProvisioned) {
    return "Wi-Fi joined";
  }
  if (g_on && g_state_b == kAuthorized) {
    return "Allowed. Use the KMJ app";
  }
  if (g_notice == Notice::Declined) {
    return "Wi-Fi setup declined";
  }
  if (g_notice == Notice::TimedOut) {
    return "Wi-Fi setup timed out";
  }
  return nullptr;
}

bool improv_take_wifi(char* ssid, size_t ssid_cap, char* password, size_t password_cap) {
  if (!g_wifi_pending) {
    return false;
  }
  g_wifi_pending = false;
  snprintf(ssid, ssid_cap, "%s", g_ssid);
  snprintf(password, password_cap, "%s", g_pass);
  return true;
}

}  // namespace kmj
