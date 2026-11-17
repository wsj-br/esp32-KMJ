#pragma once

// LilyGO T-Display-S3 Pro. Do not substitute the non-Pro variant pin macros.

namespace kmj {

constexpr int kBacklightPin = 48;
constexpr int kSdCsPin = 14;
constexpr int kTouchResPin = 13;
constexpr int kTouchIrqPin = 21;
constexpr int kTouchSdaPin = 5;
constexpr int kTouchSclPin = 6;
constexpr int kBacklightMax = 16;
constexpr uint8_t kPmuAddr = 0x6A;
constexpr int kScreenW = 480;
constexpr int kScreenH = 222;
constexpr int kStatusH = 26;
constexpr uint16_t kKmPort = 42424;
constexpr char kFirmwareVersion[] = "0.1.0";
constexpr uint32_t kHomeLongMs = 800;
constexpr uint32_t kHomeLockoutMs = 200;
constexpr uint32_t kHeartbeatMs = 1000;
constexpr uint32_t kLinkTimeoutMs = 3000;
constexpr uint32_t kImprovAuthMs = 60000;

}  // namespace kmj
