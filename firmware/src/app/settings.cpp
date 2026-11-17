#include "app/settings.hpp"

#include <Preferences.h>
#include <esp_random.h>
#include <stdio.h>
#include <string.h>

namespace kmj {
namespace {

Settings g_settings;
constexpr char kNs[] = "kmj";

void copy_str(char* dst, size_t cap, const char* src) {
  if (src == nullptr) {
    dst[0] = '\0';
    return;
  }
  snprintf(dst, cap, "%s", src);
}

}  // namespace

Settings& settings() { return g_settings; }

void settings_load() {
  Preferences prefs;
  prefs.begin(kNs, false);
  prefs.getString("ssid", g_settings.ssid, sizeof(g_settings.ssid));
  prefs.getString("pass", g_settings.password, sizeof(g_settings.password));
  prefs.getString("host", g_settings.hostname, sizeof(g_settings.hostname));
  prefs.getString("name", g_settings.device_name, sizeof(g_settings.device_name));
  if (g_settings.hostname[0] == '\0') {
    copy_str(g_settings.hostname, sizeof(g_settings.hostname), "kmj");
  }
  if (g_settings.device_name[0] == '\0') {
    copy_str(g_settings.device_name, sizeof(g_settings.device_name), "KMJ");
  }
  if (prefs.getBytes("priv", g_settings.identity.priv, 32) != 32) {
    esp_fill_random(g_settings.identity.priv, 32);
    prefs.putBytes("priv", g_settings.identity.priv, 32);
  }
  identity_from_priv(g_settings.identity);
  g_settings.peer_count = prefs.getInt("peers", 0);
  if (g_settings.peer_count < 0 || g_settings.peer_count > kMaxPeers) {
    g_settings.peer_count = 0;
  }
  prefs.getBytes("peer", g_settings.peers, sizeof(g_settings.peers));
  g_settings.backlight = prefs.getInt("bl", 16);
  g_settings.screen_off_sec = prefs.getInt("off", 0);
  g_settings.vid = static_cast<uint16_t>(prefs.getUShort("vid", 0x413C));
  g_settings.pid = static_cast<uint16_t>(prefs.getUShort("pid", 0x250E));
  if (g_settings.backlight < 1) {
    g_settings.backlight = 1;
  }
  if (g_settings.backlight > 16) {
    g_settings.backlight = 16;
  }
  prefs.end();
}

void settings_save_wifi(const char* ssid, const char* password) {
  copy_str(g_settings.ssid, sizeof(g_settings.ssid), ssid);
  copy_str(g_settings.password, sizeof(g_settings.password), password);
  Preferences prefs;
  prefs.begin(kNs, false);
  prefs.putString("ssid", g_settings.ssid);
  prefs.putString("pass", g_settings.password);
  prefs.end();
}

void settings_clear_wifi() {
  g_settings.ssid[0] = '\0';
  g_settings.password[0] = '\0';
  Preferences prefs;
  prefs.begin(kNs, false);
  prefs.remove("ssid");
  prefs.remove("pass");
  prefs.end();
}

void settings_save_display(int backlight, int screen_off_sec) {
  g_settings.backlight = backlight;
  g_settings.screen_off_sec = screen_off_sec;
  Preferences prefs;
  prefs.begin(kNs, false);
  prefs.putInt("bl", backlight);
  prefs.putInt("off", screen_off_sec);
  prefs.end();
}

void settings_save_name(const char* hostname, const char* device_name) {
  copy_str(g_settings.hostname, sizeof(g_settings.hostname), hostname);
  copy_str(g_settings.device_name, sizeof(g_settings.device_name), device_name);
  Preferences prefs;
  prefs.begin(kNs, false);
  prefs.putString("host", g_settings.hostname);
  prefs.putString("name", g_settings.device_name);
  prefs.end();
}

bool settings_add_peer(const uint8_t pub[32]) {
  for (int i = 0; i < g_settings.peer_count; i++) {
    if (pub_equal(g_settings.peers[i], pub)) {
      return true;
    }
  }
  if (g_settings.peer_count >= kMaxPeers) {
    return false;
  }
  memcpy(g_settings.peers[g_settings.peer_count], pub, 32);
  g_settings.peer_count++;
  Preferences prefs;
  prefs.begin(kNs, false);
  prefs.putInt("peers", g_settings.peer_count);
  prefs.putBytes("peer", g_settings.peers, sizeof(g_settings.peers));
  prefs.end();
  return true;
}

void settings_remove_peer(int index) {
  if (index < 0 || index >= g_settings.peer_count) {
    return;
  }
  for (int i = index; i < g_settings.peer_count - 1; i++) {
    memcpy(g_settings.peers[i], g_settings.peers[i + 1], 32);
  }
  g_settings.peer_count--;
  memset(g_settings.peers[g_settings.peer_count], 0, 32);
  Preferences prefs;
  prefs.begin(kNs, false);
  prefs.putInt("peers", g_settings.peer_count);
  prefs.putBytes("peer", g_settings.peers, sizeof(g_settings.peers));
  prefs.end();
}

const char* peer_label(const uint8_t pub[32], char* buf, size_t cap) {
  snprintf(buf, cap, "%02X%02X%02X%02X", pub[0], pub[1], pub[2], pub[3]);
  return buf;
}

}  // namespace kmj
