#pragma once

#include <stddef.h>
#include <stdint.h>

namespace kmj {

void improv_set_identify(void (*fn)());
void improv_set_gate(void (*fn)(bool pause));
bool improv_start();
const char* improv_fail_text();
void improv_stop();
void improv_authorize(bool allow);
void improv_poll(uint32_t now_ms);
bool improv_active();
bool improv_needs_auth();
uint8_t improv_state();
const char* improv_status_line();
bool improv_take_wifi(char* ssid, size_t ssid_cap, char* password, size_t password_cap);

}  // namespace kmj
