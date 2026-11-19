#pragma once

// LilyGO T-Display-S3 Pro. Do not substitute the non-Pro variant pin macros.

namespace kmj {

constexpr char kBoardName[] = "lilygo-t-display-s3-pro";
constexpr char kDefaultNamePrefix[] = "KMJ-TD";

constexpr int kScreenW = 480;
constexpr int kScreenH = 222;
constexpr bool kScreenRound = false;
constexpr int kStatusH = 26;

constexpr int kBacklightPin = 48;
constexpr int kSdCsPin = 14;

constexpr int kTouchSdaPin = 5;
constexpr int kTouchSclPin = 6;
constexpr int kTouchResPin = 13;
constexpr int kTouchIrqPin = 21;
// CST226SE first, CST816 as the fallback; the home key comes from the touch
// controller, so there is no physical home button pin.
constexpr uint8_t kTouchAddrFirst = 0x5A;   // CST226SE_SLAVE_ADDRESS
constexpr uint8_t kTouchAddrSecond = 0x15;  // CST816_SLAVE_ADDRESS
constexpr int kHomeButtonPin = -1;
constexpr bool kTouchSwapXY = true;
constexpr bool kTouchMirrorX = false;
constexpr bool kTouchMirrorY = true;

constexpr uint8_t kPmuAddr = 0x6A;
constexpr PmuKind kPmuKind = PmuKind::Sy6970;
constexpr BacklightKind kBacklightKind = BacklightKind::Pulse;
constexpr int kPaPin = -1;

}  // namespace kmj
