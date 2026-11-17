#pragma once

#include <stdint.h>

#include "logic/keys.hpp"

namespace kmj {

void hid_begin(uint16_t vid, uint16_t pid);
void hid_service();
void hid_set_usb_wanted(bool wanted);
bool hid_usb_wanted();
bool hid_mounted();
uint8_t hid_leds();
uint32_t hid_tx();

void hid_key(uint8_t usage, bool down);
void hid_mouse(int dx, int dy);
void hid_buttons(uint8_t mask);
void hid_wheel(int vertical, int horizontal);
void hid_release_all();

}  // namespace kmj
