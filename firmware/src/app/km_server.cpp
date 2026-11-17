#include "app/km_server.hpp"

#include <WiFi.h>
#include <stdio.h>
#include <string.h>

#include "app/hid_device.hpp"
#include "app/settings.hpp"
#include "logic/link.hpp"
#include "pins.hpp"

namespace kmj {
namespace {

struct Server {
  WiFiServer listener{kKmPort};
  WiFiClient client;
  Link link;
  bool listening = false;
  bool armed = false;
  char client_name[32] = {};
  uint32_t keys = 0;
  uint32_t moves = 0;
  uint32_t latency_ms = 0;
  uint32_t last_rx = 0;
  uint32_t last_active = 0;
  char last_key[8] = {};
  char note[40] = {};
  bool secure = false;
  bool inject = false;
};

Server g;

void fill_rng(void*, uint8_t* dst, size_t n) { esp_fill_random(dst, n); }

bool peer_known(void*, const uint8_t pub[32]) {
  const Settings& s = settings();
  for (int i = 0; i < s.peer_count; i++) {
    if (pub_equal(s.peers[i], pub)) {
      return true;
    }
  }
  return false;
}

void note_fail(void*, const char* reason) {
  snprintf(g.note, sizeof(g.note), "%s", reason != nullptr ? reason : "closed");
  g.secure = false;
}

void note_paired(void*, const uint8_t pub[32]) {
  if (!settings_add_peer(pub)) {
    snprintf(g.note, sizeof(g.note), "peer list full");
  } else {
    snprintf(g.note, sizeof(g.note), "paired");
  }
  g.armed = false;
}

void on_secure(void*) {
  g.secure = true;
  g.last_rx = millis();
  snprintf(g.client_name, sizeof(g.client_name), "%s", g.link.peer_name());
  uint8_t hello[40] = {0x01, 1, 0x01};
  const char* name = settings().device_name;
  const size_t nlen = strlen(name);
  hello[3] = static_cast<uint8_t>(nlen > 31 ? 31 : nlen);
  memcpy(hello + 4, name, hello[3]);
  g.link.send_app(hello, 4u + hello[3]);
  snprintf(g.note, sizeof(g.note), "session");
}

void usage_name(uint8_t usage, char* buf, size_t cap) {
  if (usage >= 0x04 && usage <= 0x1D) {
    snprintf(buf, cap, "%c", 'A' + (usage - 0x04));
    return;
  }
  if (usage >= 0x1E && usage <= 0x26) {
    snprintf(buf, cap, "%c", usage == 0x27 ? '0' : '1' + (usage - 0x1E));
    return;
  }
  if (usage == 0x27) {
    snprintf(buf, cap, "0");
    return;
  }
  switch (usage) {
    case 0x28: snprintf(buf, cap, "Ent"); return;
    case 0x29: snprintf(buf, cap, "Esc"); return;
    case 0x2A: snprintf(buf, cap, "Bksp"); return;
    case 0x2B: snprintf(buf, cap, "Tab"); return;
    case 0x2C: snprintf(buf, cap, "Spc"); return;
    case 0xE0: snprintf(buf, cap, "Ctl"); return;
    case 0xE1: snprintf(buf, cap, "Sft"); return;
    case 0xE2: snprintf(buf, cap, "Alt"); return;
    case 0xE3: snprintf(buf, cap, "Gui"); return;
    default: snprintf(buf, cap, "%02X", usage); return;
  }
}

uint32_t read_u32(const uint8_t* p) {
  return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
         (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

void handle_app(void*, const uint8_t* msg, size_t n) {
  if (n < 1) {
    return;
  }
  g.last_rx = millis();
  switch (msg[0]) {
    case 0x02:
      if (n >= 3 && g.inject) {
        hid_key(msg[1], msg[2] != 0);
        g.keys++;
        usage_name(msg[1], g.last_key, sizeof(g.last_key));
        g.last_active = g.last_rx;
      }
      break;
    case 0x03:
      if (n >= 5) {
        const int16_t dx = static_cast<int16_t>(msg[1] | (msg[2] << 8));
        const int16_t dy = static_cast<int16_t>(msg[3] | (msg[4] << 8));
        if (g.inject) hid_mouse(dx, dy);
        g.moves++;
        g.last_active = g.last_rx;
      }
      break;
    case 0x04:
      if (n >= 2 && g.inject) {
        hid_buttons(msg[1]);
        g.last_active = g.last_rx;
      }
      break;
    case 0x05:
      if (n >= 3 && g.inject) {
        hid_wheel(static_cast<int8_t>(msg[1]), static_cast<int8_t>(msg[2]));
        g.last_active = g.last_rx;
      }
      break;
    case 0x06:
      if (g.inject) {
        hid_release_all();
      }
      break;
    case 0x07:
      if (n >= 5) {
        if (n >= 9) {
          g.latency_ms = read_u32(msg + 5);
        }
        uint8_t pong[9] = {0x08};
        memcpy(pong + 1, msg + 1, 4);
        const uint32_t now = millis();
        pong[5] = static_cast<uint8_t>(now);
        pong[6] = static_cast<uint8_t>(now >> 8);
        pong[7] = static_cast<uint8_t>(now >> 16);
        pong[8] = static_cast<uint8_t>(now >> 24);
        g.link.send_app(pong, sizeof(pong));
      }
      break;
    default:
      break;
  }
}

void drop_client() {
  if (g.client) {
    g.client.stop();
  }
  hid_release_all();
  g.secure = false;
  g.keys = 0;
  g.moves = 0;
  LinkEvents ev{note_paired, on_secure, handle_app, note_fail, nullptr};
  g.link.reset(Role::Device, settings().identity, fill_rng, nullptr, ev);
  g.link.set_local_name(settings().device_name);
  g.link.set_pairing_armed(g.armed);
  g.link.set_peer_check(peer_known, nullptr);
}

}  // namespace

void km_start() {
  if (g.listening) {
    return;
  }
  g.listener.begin();
  g.listener.setNoDelay(true);
  g.listening = true;
  g.note[0] = '\0';
  drop_client();
}

void km_stop() {
  drop_client();
  g.listener.end();
  g.listening = false;
  g.armed = false;
}

void km_arm(bool armed) {
  g.armed = armed;
  g.link.set_pairing_armed(armed);
  if (armed) {
    snprintf(g.note, sizeof(g.note), "pairing armed");
  }
}

void km_confirm() { g.link.confirm(); }
void km_cancel() { g.link.reject(); }

void km_disconnect() { drop_client(); }

void km_set_inject(bool enabled) {
  g.inject = enabled;
  if (!enabled) {
    hid_release_all();
  }
}

void reset_link() {
  g.secure = false;
  LinkEvents ev{note_paired, on_secure, handle_app, note_fail, nullptr};
  g.link.reset(Role::Device, settings().identity, fill_rng, nullptr, ev);
  g.link.set_local_name(settings().device_name);
  g.link.set_pairing_armed(g.armed);
  g.link.set_peer_check(peer_known, nullptr);
}

void flush_out() {
  uint8_t out[512];
  for (;;) {
    const size_t n = g.link.read_out(out, sizeof(out));
    if (n == 0) {
      break;
    }
    if (g.client) {
      g.client.write(out, n);
    }
  }
}

void km_poll(uint32_t now_ms) {
  if (!g.listening || WiFi.status() != WL_CONNECTED) {
    return;
  }
  if (!g.client || !g.client.connected()) {
    if (g.client) {
      g.client.stop();
    }
    WiFiClient next = g.listener.accept();
    if (next) {
      g.client = next;
      g.client.setNoDelay(true);
      reset_link();
      snprintf(g.note, sizeof(g.note), "client");
      snprintf(g.client_name, sizeof(g.client_name), "%s", g.client.remoteIP().toString().c_str());
    }
  } else {
    uint8_t buf[256];
    while (g.client.available() > 0) {
      const int n = g.client.read(buf, sizeof(buf));
      if (n > 0) {
        g.link.feed(buf, static_cast<size_t>(n));
      }
    }
    flush_out();
    if (g.link.finished_pair()) {
      g.client.stop();
      reset_link();
      snprintf(g.note, sizeof(g.note), "paired");
    } else if (g.link.fail()[0] != '\0') {
      g.client.stop();
      char why[40];
      snprintf(why, sizeof(why), "%s", g.link.fail());
      reset_link();
      snprintf(g.note, sizeof(g.note), "%s", why);
    } else if (g.secure && g.last_rx != 0 && now_ms - g.last_rx > kLinkTimeoutMs) {
      g.client.stop();
      hid_release_all();
      reset_link();
      snprintf(g.note, sizeof(g.note), "link timeout");
    }
  }
  if (g.client && g.client.connected()) {
    WiFiClient extra = g.listener.accept();
    if (extra) {
      extra.stop();
    }
  }
}

KmView km_view() {
  KmView view;
  view.listening = g.listening;
  view.secure = g.secure;
  view.pairing = g.link.waiting_confirm();
  snprintf(view.code, sizeof(view.code), "%s", g.link.pair_code());
  snprintf(view.client, sizeof(view.client), "%s", g.client_name);
  if (g.client && g.client.connected()) {
    snprintf(view.addr, sizeof(view.addr), "%s", g.client.remoteIP().toString().c_str());
  }
  view.keys = g.keys;
  view.moves = g.moves;
  view.latency_ms = g.latency_ms;
  snprintf(view.last_key, sizeof(view.last_key), "%s", g.last_key);
  view.active = g.secure && g.last_active != 0 && millis() - g.last_active < 500;
  snprintf(view.note, sizeof(view.note), "%s", g.note);
  return view;
}

}  // namespace kmj
