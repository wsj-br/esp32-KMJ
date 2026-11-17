#pragma once

#include <stdint.h>

namespace kmj {

struct KmView {
  bool listening = false;
  bool secure = false;
  bool pairing = false;
  char code[8] = {};
  char client[32] = {};
  char addr[24] = {};
  uint32_t keys = 0;
  uint32_t moves = 0;
  uint32_t latency_ms = 0;
  char last_key[8] = {};
  bool active = false;
  char note[40] = {};
};

void km_start();
void km_stop();
void km_poll(uint32_t now_ms);
void km_arm(bool armed);
void km_confirm();
void km_cancel();
void km_disconnect();
void km_set_inject(bool enabled);
KmView km_view();

}  // namespace kmj
