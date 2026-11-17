#pragma once

#include <stddef.h>
#include <stdint.h>

namespace kmj {

struct ImprovPacket {
  uint8_t command = 0;
  const uint8_t* data = nullptr;
  uint8_t data_len = 0;
  bool valid = false;
};

// `bytes` includes the trailing checksum. A valid packet's bytes sum to 0 mod 256.
ImprovPacket improv_parse(const uint8_t* bytes, size_t n);

size_t improv_build(uint8_t* out, size_t cap, uint8_t command, const uint8_t* data, size_t data_len);

// Result characteristic: command, data length, length-prefixed strings, checksum.
size_t improv_result(uint8_t* out, size_t cap, uint8_t command, const char* const* strings, size_t count);

bool improv_wifi_credentials(const uint8_t* data, size_t n, char* ssid, size_t ssid_cap, char* password,
                             size_t password_cap);

bool hostname_ok(const char* host);

}  // namespace kmj
