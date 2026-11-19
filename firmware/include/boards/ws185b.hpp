#pragma once

// Waveshare ESP32-S3-Touch-LCD-1.85B: 1.85" 360x360 round ST77916 panel over
// QSPI, CST816S capacitive touch, BQ27220 fuel gauge, BOOT (GPIO0) and a
// hardware-latched PWR button. Audio (ES8311/ES7210), IMU (QMI8658) and RTC
// (PCF85063) sit on the shared I2C bus but are not used by KMJ.

namespace kmj {

constexpr char kBoardName[] = "waveshare-esp32-s3-touch-lcd-1.85b";
constexpr char kDefaultNamePrefix[] = "KMJ-TL";

constexpr int kScreenW = 360;
constexpr int kScreenH = 360;
constexpr bool kScreenRound = true;
constexpr int kStatusH = 28;

constexpr int kBacklightPin = 5;
constexpr int kSdCsPin = -1;  // SDMMC controller, not the display SPI bus

constexpr int kTouchSdaPin = 11;
constexpr int kTouchSclPin = 10;
constexpr int kTouchResPin = 1;
constexpr int kTouchIrqPin = 4;
// CST816S only; no touch home key, so GPIO0 (BOOT) is the home button.
constexpr uint8_t kTouchAddrFirst = 0x15;  // CST816_SLAVE_ADDRESS
constexpr uint8_t kTouchAddrSecond = 0x5A; // CST226SE_SLAVE_ADDRESS
constexpr int kHomeButtonPin = 0;
constexpr bool kTouchSwapXY = false;
constexpr bool kTouchMirrorX = false;
constexpr bool kTouchMirrorY = false;

constexpr uint8_t kPmuAddr = 0x55;  // BQ27220 fuel gauge
constexpr PmuKind kPmuKind = PmuKind::Bq27220;
constexpr BacklightKind kBacklightKind = BacklightKind::Pwm;

// Speaker amplifier enable; hold low so the unused audio path stays silent.
constexpr int kPaPin = 9;

}  // namespace kmj
