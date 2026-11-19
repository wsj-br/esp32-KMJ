#pragma once

#include <stddef.h>
#include <stdint.h>

namespace kmj {

// Writes "<prefix>-<XX>" into out, where XX is mac_last as two uppercase hex
// digits (e.g. prefix "KMJ-TD", mac_last 0x9F -> "KMJ-TD-9F"). Returns false
// when out is too small or the arguments are bad; the result of a failed call
// is not usable.
bool format_default_name(const char* prefix, uint8_t mac_last, char* out, size_t out_len);

}  // namespace kmj
