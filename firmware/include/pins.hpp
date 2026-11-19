#pragma once

// Shared firmware constants. Board pins, screen geometry and board identity
// live in firmware/include/boards/<board>.hpp, selected by the KMJ_BOARD_*
// build flags set in firmware/platformio.ini.

namespace kmj {

constexpr uint8_t kBacklightMax = 16;
constexpr uint16_t kKmPort = 42424;
constexpr char kFirmwareVersion[] = "0.1.1";
constexpr uint32_t kHomeLongMs = 800;
constexpr uint32_t kHomeLockoutMs = 200;
constexpr uint32_t kHeartbeatMs = 1000;
constexpr uint32_t kLinkTimeoutMs = 3000;
constexpr uint32_t kImprovAuthMs = 60000;

enum class BacklightKind : uint8_t { Pulse, Pwm };
enum class PmuKind : uint8_t { Sy6970, Bq27220 };

}  // namespace kmj

#if defined(KMJ_BOARD_TDISPLAY_S3)
#include "boards/tdisplay_s3.hpp"
#elif defined(KMJ_BOARD_WS_185B)
#include "boards/ws185b.hpp"
#else
#error "Define KMJ_BOARD_TDISPLAY_S3 or KMJ_BOARD_WS_185B"
#endif
