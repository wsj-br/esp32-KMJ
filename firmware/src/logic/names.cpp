#include "logic/names.hpp"

#include <stdio.h>

namespace kmj {

bool format_default_name(const char* prefix, uint8_t mac_last, char* out, size_t out_len) {
  if (out == nullptr || out_len == 0 || prefix == nullptr || prefix[0] == '\0') {
    return false;
  }
  const int n = snprintf(out, out_len, "%s-%02X", prefix, mac_last);
  return n > 0 && static_cast<size_t>(n) < out_len;
}

}  // namespace kmj
