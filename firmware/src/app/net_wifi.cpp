#include "app/net_wifi.hpp"

#include <ESPmDNS.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <stdio.h>
#include <string.h>

namespace kmj {
namespace {

bool g_begun = false;
bool g_paused = false;
bool g_hold = false;
bool g_radio_stopped = false;
bool g_mdns_up = false;
bool g_service = false;
uint32_t g_last_try = 0;
char g_host[64] = {};
char g_ssid[33] = {};
char g_pass[65] = {};
bool g_want = false;

}  // namespace

void net_begin(const char* hostname) {
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.setAutoReconnect(true);
  if (hostname != nullptr && hostname[0] != '\0') {
    snprintf(g_host, sizeof(g_host), "%s", hostname);
    WiFi.setHostname(g_host);
  }
  g_begun = true;
}

void net_pause() {
  g_paused = true;
  if (g_mdns_up) {
    MDNS.end();
    g_mdns_up = false;
    g_service = false;
  }
  WiFi.setAutoReconnect(false);
  // Stop the radio but leave the driver loaded. Deinit drops the shared
  // radio state, and the Bluetooth controller then refuses to start.
  if (esp_wifi_stop() == ESP_OK) {
    g_radio_stopped = true;
  }
}

void net_hold(bool hold) {
  g_hold = hold;
  if (!hold) {
    g_last_try = 0;
  }
}

void net_resume() {
  if (g_radio_stopped) {
    esp_wifi_start();
    g_radio_stopped = false;
  }
  WiFi.persistent(false);
  WiFi.setSleep(false);
  if (g_host[0] != '\0') {
    WiFi.setHostname(g_host);
  }
  g_paused = false;
  if (g_hold) {
    // A reconnect here owns the radio and the board never shows up in a scan.
    WiFi.setAutoReconnect(false);
    esp_wifi_disconnect();
    wifi_config_t blank = {};
    esp_wifi_set_config(WIFI_IF_STA, &blank);
    return;
  }
  WiFi.setAutoReconnect(true);
  if (g_want && g_ssid[0] != '\0') {
    WiFi.begin(g_ssid, g_pass);
    g_last_try = millis();
  }
}

void net_connect(const char* ssid, const char* password) {
  if (ssid == nullptr || ssid[0] == '\0') {
    g_want = false;
    return;
  }
  snprintf(g_ssid, sizeof(g_ssid), "%s", ssid);
  snprintf(g_pass, sizeof(g_pass), "%s", password != nullptr ? password : "");
  g_want = true;
  g_hold = false;
  WiFi.setAutoReconnect(true);
  WiFi.disconnect(false, false);
  WiFi.begin(g_ssid, g_pass);
  g_last_try = millis();
}

void net_forget() {
  g_want = false;
  g_ssid[0] = '\0';
  WiFi.disconnect(true, true);
}

void net_poll() {
  if (g_paused || g_hold || !g_begun || !g_want) {
    return;
  }
  if (WiFi.status() == WL_CONNECTED) {
    return;
  }
  if (millis() - g_last_try < 8000) {
    return;
  }
  g_last_try = millis();
  WiFi.begin(g_ssid, g_pass);
}

bool net_connected() { return WiFi.status() == WL_CONNECTED; }
bool net_busy() { return g_want && WiFi.status() != WL_CONNECTED; }
int net_rssi() { return net_connected() ? WiFi.RSSI() : 0; }

void net_ip(char* buf, size_t cap) {
  if (!net_connected()) {
    snprintf(buf, cap, "-");
    return;
  }
  snprintf(buf, cap, "%s", WiFi.localIP().toString().c_str());
}

void net_mac(char* buf, size_t cap) { snprintf(buf, cap, "%s", WiFi.macAddress().c_str()); }

void net_advertise(bool on, const char* name, uint16_t port) {
  if (!net_connected()) {
    return;
  }
  if (!g_mdns_up) {
    if (!MDNS.begin(WiFi.getHostname() != nullptr ? WiFi.getHostname() : "kmj")) {
      return;
    }
    g_mdns_up = true;
  }
  if (on && !g_service) {
    MDNS.addService("kmj", "tcp", port);
    if (name != nullptr) {
      MDNS.addServiceTxt("kmj", "tcp", "name", name);
    }
    g_service = true;
  } else if (!on && g_service) {
    MDNS.end();
    g_mdns_up = false;
    g_service = false;
  }
}

}  // namespace kmj
