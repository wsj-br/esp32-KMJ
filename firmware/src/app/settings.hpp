#pragma once

#include <stdint.h>

#include "logic/crypto.hpp"

namespace kmj {

constexpr int kMaxPeers = 4;

struct Settings {
  char ssid[33] = {};
  char password[65] = {};
  // Empty until settings_load() fills them with the board default
  // (kDefaultNamePrefix + last Wi-Fi MAC byte) or a name saved via Improv.
  char hostname[32] = {};
  char device_name[20] = {};
  Identity identity{};
  uint8_t peers[kMaxPeers][32]{};
  int peer_count = 0;
  int backlight = 16;
  int screen_off_sec = 0;
  uint16_t vid = 0x413C;
  uint16_t pid = 0x250E;
};

Settings& settings();
void settings_load();
void settings_save_wifi(const char* ssid, const char* password);
void settings_clear_wifi();
void settings_save_display(int backlight, int screen_off_sec);
void settings_save_name(const char* hostname, const char* device_name);
bool settings_add_peer(const uint8_t pub[32]);
void settings_remove_peer(int index);
const char* peer_label(const uint8_t pub[32], char* buf, size_t cap);

}  // namespace kmj
