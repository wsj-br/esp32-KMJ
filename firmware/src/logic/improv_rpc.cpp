#include "logic/improv_rpc.hpp"

#include <string.h>

namespace kmj {

ImprovPacket improv_parse(const uint8_t* bytes, size_t n) {
  ImprovPacket pkt;
  if (bytes == nullptr || n < 3) {
    return pkt;
  }
  unsigned sum = 0;
  for (size_t i = 0; i < n; i++) {
    sum += bytes[i];
  }
  if ((sum & 0xff) != 0) {
    return pkt;
  }
  const uint8_t data_len = bytes[1];
  if (n != static_cast<size_t>(data_len) + 3) {
    return pkt;
  }
  pkt.command = bytes[0];
  pkt.data = bytes + 2;
  pkt.data_len = data_len;
  pkt.valid = true;
  return pkt;
}

size_t improv_build(uint8_t* out, size_t cap, uint8_t command, const uint8_t* data, size_t data_len) {
  if (data_len > 255 || cap < data_len + 3) {
    return 0;
  }
  out[0] = command;
  out[1] = static_cast<uint8_t>(data_len);
  if (data_len > 0 && data != nullptr) {
    memcpy(out + 2, data, data_len);
  }
  unsigned sum = 0;
  for (size_t i = 0; i < data_len + 2; i++) {
    sum += out[i];
  }
  out[data_len + 2] = static_cast<uint8_t>((0u - sum) & 0xff);
  return data_len + 3;
}

size_t improv_result(uint8_t* out, size_t cap, uint8_t command, const char* const* strings, size_t count) {
  uint8_t data[240];
  size_t n = 0;
  for (size_t i = 0; i < count; i++) {
    const char* s = strings[i] != nullptr ? strings[i] : "";
    const size_t len = strlen(s);
    if (len > 255 || n + 1 + len > sizeof(data)) {
      return 0;
    }
    data[n++] = static_cast<uint8_t>(len);
    memcpy(data + n, s, len);
    n += len;
  }
  return improv_build(out, cap, command, data, n);
}

bool improv_wifi_credentials(const uint8_t* data, size_t n, char* ssid, size_t ssid_cap, char* password,
                             size_t password_cap) {
  if (data == nullptr || n < 2 || ssid_cap == 0 || password_cap == 0) {
    return false;
  }
  const size_t ssid_len = data[0];
  if (1u + ssid_len >= n) {
    return false;
  }
  const size_t pass_len = data[1 + ssid_len];
  if (1u + ssid_len + 1u + pass_len != n) {
    return false;
  }
  if (ssid_len >= ssid_cap || pass_len >= password_cap || ssid_len == 0 || ssid_len > 32) {
    return false;
  }
  memcpy(ssid, data + 1, ssid_len);
  ssid[ssid_len] = '\0';
  memcpy(password, data + 2 + ssid_len, pass_len);
  password[pass_len] = '\0';
  return true;
}

bool hostname_ok(const char* host) {
  if (host == nullptr) {
    return false;
  }
  const size_t n = strlen(host);
  if (n == 0 || n > 63) {
    return false;
  }
  if (host[0] == '-' || host[n - 1] == '-') {
    return false;
  }
  for (size_t i = 0; i < n; i++) {
    const char c = host[i];
    const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-';
    if (!ok) {
      return false;
    }
  }
  return true;
}

}  // namespace kmj
