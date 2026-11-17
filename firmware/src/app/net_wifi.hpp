#pragma once

#include <stddef.h>
#include <stdint.h>

namespace kmj {

void net_begin(const char* hostname);
void net_connect(const char* ssid, const char* password);
void net_forget();
void net_pause();
void net_hold(bool hold);
void net_resume();
void net_poll();
bool net_connected();
bool net_busy();
int net_rssi();
void net_ip(char* buf, size_t cap);
void net_mac(char* buf, size_t cap);
void net_advertise(bool on, const char* name, uint16_t port);

}  // namespace kmj
