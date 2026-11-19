#include <stdio.h>
#include <string.h>

#include <unity.h>

#include "logic/button.hpp"
#include "logic/improv_rpc.hpp"
#include "logic/jiggler.hpp"
#include "logic/keys.hpp"
#include "logic/layout_round.hpp"
#include "logic/link.hpp"
#include "logic/names.hpp"

namespace {

int hex_nibble(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  return c - 'A' + 10;
}

void unhex(const char* hex, uint8_t* out, size_t n) {
  for (size_t i = 0; i < n; i++) {
    out[i] = static_cast<uint8_t>((hex_nibble(hex[i * 2]) << 4) | hex_nibble(hex[i * 2 + 1]));
  }
}

void to_hex(const uint8_t* p, size_t n, char* out) {
  static const char* kDigits = "0123456789abcdef";
  for (size_t i = 0; i < n; i++) {
    out[i * 2] = kDigits[p[i] >> 4];
    out[i * 2 + 1] = kDigits[p[i] & 0xf];
  }
  out[n * 2] = '\0';
}

struct Script {
  const uint8_t* key;
};

void script_rng(void* ctx, uint8_t* dst, size_t n) {
  memcpy(dst, static_cast<Script*>(ctx)->key, n);
}

long seq_rand(void* ctx, long lo, long) {
  int* cursor = static_cast<int*>(ctx);
  const long values[] = {30000, 10, 0, 30000};
  const long v = values[*cursor % 4];
  *cursor += 1;
  return v < lo ? lo : v;
}

struct PairSink {
  bool paired = false;
  bool secure = false;
  bool failed = false;
  uint8_t peer[32]{};
  uint8_t app[64]{};
  size_t app_len = 0;
};

void on_paired(void* ctx, const uint8_t pub[32]) {
  auto* s = static_cast<PairSink*>(ctx);
  s->paired = true;
  memcpy(s->peer, pub, 32);
}

void on_secure(void* ctx) { static_cast<PairSink*>(ctx)->secure = true; }

void on_fail(void* ctx, const char*) { static_cast<PairSink*>(ctx)->failed = true; }

void on_app(void* ctx, const uint8_t* msg, size_t n) {
  auto* s = static_cast<PairSink*>(ctx);
  if (n > sizeof(s->app)) {
    n = sizeof(s->app);
  }
  memcpy(s->app, msg, n);
  s->app_len = n;
}

void exchange(kmj::Link& a, kmj::Link& b) {
  uint8_t buf[1600];
  for (int i = 0; i < 12; i++) {
    const size_t n1 = a.read_out(buf, sizeof(buf));
    if (n1 > 0) {
      b.feed(buf, n1);
    }
    const size_t n2 = b.read_out(buf, sizeof(buf));
    if (n2 > 0) {
      a.feed(buf, n2);
    }
  }
}

}  // namespace

void test_blake2_and_x25519() {
  uint8_t empty[32];
  kmj::hash32(empty, nullptr, 0);
  char hex[65];
  to_hex(empty, 32, hex);
  TEST_ASSERT_EQUAL_STRING("0e5751c026e543b2e8ab2eb06099daa1d1e5df47778f7787faab45cdf12fe3a8", hex);

  const char* alice_priv = "77076d0a7318a57d3c16c17251b26645df4c2f87ebc0992ab177fba51db92c2a";
  kmj::Identity alice{};
  unhex(alice_priv, alice.priv, 32);
  kmj::identity_from_priv(alice);
  to_hex(alice.pub, 32, hex);
  TEST_ASSERT_EQUAL_STRING("8520f0098930a754748b7ddcb43ef75a0dbf3a0d26381af4eba4a98eaa9b4e6a", hex);

  const char* bob_priv = "5dab087e624a8a4b79e17f8b83800ee66f3bb1292618b6fd1c2f8b27ff88e0eb";
  kmj::Identity bob{};
  unhex(bob_priv, bob.priv, 32);
  kmj::identity_from_priv(bob);
  to_hex(bob.pub, 32, hex);
  TEST_ASSERT_EQUAL_STRING("de9edb7d7b7dc1b4d35b61c2ece435373f8343c85b78674dadfc7e146f882b4f", hex);

  uint8_t shared_a[32];
  uint8_t shared_b[32];
  kmj::x25519_shared(shared_a, alice.priv, bob.pub);
  kmj::x25519_shared(shared_b, bob.priv, alice.pub);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(shared_a, shared_b, 32);
  to_hex(shared_a, 32, hex);
  TEST_ASSERT_EQUAL_STRING("4a5d9d5ba4ce2de1728e3bf480350f25e07e21c947d19e3376f09b3c1e161742", hex);
}

void test_aead_roundtrip() {
  uint8_t key[32];
  memset(key, 7, sizeof(key));
  uint8_t nonce[24];
  kmj::nonce_from_counter(nonce, 3);
  const uint8_t plain[] = {1, 2, 3, 4, 5};
  uint8_t cipher[5];
  uint8_t mac[16];
  kmj::aead_seal(cipher, mac, key, nonce, plain, sizeof(plain));
  char cipher_hex[11];
  char mac_hex[33];
  to_hex(cipher, 5, cipher_hex);
  to_hex(mac, 16, mac_hex);
  fprintf(stderr, "AEAD %s %s\n", cipher_hex, mac_hex);
  TEST_ASSERT_EQUAL_STRING("2a4ccae3c3", cipher_hex);
  TEST_ASSERT_EQUAL_STRING("0bfe9b01fae115b8c393a0cabc207b60", mac_hex);
  uint8_t opened[5];
  TEST_ASSERT_TRUE(kmj::aead_open(opened, mac, key, nonce, cipher, sizeof(cipher)));
  TEST_ASSERT_EQUAL_UINT8_ARRAY(plain, opened, 5);
  mac[0] ^= 1;
  TEST_ASSERT_FALSE(kmj::aead_open(opened, mac, key, nonce, cipher, sizeof(cipher)));
}

void test_pair_and_session() {
  const char* alice_priv = "77076d0a7318a57d3c16c17251b26645df4c2f87ebc0992ab177fba51db92c2a";
  const char* bob_priv = "5dab087e624a8a4b79e17f8b83800ee66f3bb1292618b6fd1c2f8b27ff88e0eb";
  const char* alice_eph = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
  const char* bob_eph = "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";
  kmj::Identity device{};
  kmj::Identity client{};
  unhex(alice_priv, device.priv, 32);
  unhex(bob_priv, client.priv, 32);
  kmj::identity_from_priv(device);
  kmj::identity_from_priv(client);
  uint8_t dev_eph[32];
  uint8_t cli_eph[32];
  unhex(alice_eph, dev_eph, 32);
  unhex(bob_eph, cli_eph, 32);
  Script dev_rng{dev_eph};
  Script cli_rng{cli_eph};

  PairSink dev_sink;
  PairSink cli_sink;
  kmj::LinkEvents dev_ev{on_paired, on_secure, on_app, on_fail, &dev_sink};
  kmj::LinkEvents cli_ev{on_paired, on_secure, on_app, on_fail, &cli_sink};
  kmj::Link device_link;
  kmj::Link client_link;
  device_link.reset(kmj::Role::Device, device, script_rng, &dev_rng, dev_ev);
  client_link.reset(kmj::Role::Client, client, script_rng, &cli_rng, cli_ev);
  device_link.set_local_name("KMJ");
  device_link.set_pairing_armed(true);
  client_link.begin_client(1, "desk");
  exchange(device_link, client_link);
  TEST_ASSERT_TRUE(device_link.waiting_confirm());
  TEST_ASSERT_TRUE(client_link.waiting_confirm());
  TEST_ASSERT_EQUAL_STRING(device_link.pair_code(), client_link.pair_code());
  fprintf(stderr, "PAIR_CODE %s\n", device_link.pair_code());
  TEST_ASSERT_EQUAL_STRING("131422", device_link.pair_code());

  device_link.confirm();
  client_link.confirm();
  exchange(device_link, client_link);
  TEST_ASSERT_TRUE(dev_sink.paired);
  TEST_ASSERT_TRUE(cli_sink.paired);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(client.pub, dev_sink.peer, 32);

  dev_sink = PairSink();
  cli_sink = PairSink();
  device_link.reset(kmj::Role::Device, device, script_rng, &dev_rng, dev_ev);
  client_link.reset(kmj::Role::Client, client, script_rng, &cli_rng, cli_ev);
  dev_ev.ctx = &dev_sink;
  cli_ev.ctx = &cli_sink;
  device_link.reset(kmj::Role::Device, device, script_rng, &dev_rng, dev_ev);
  client_link.reset(kmj::Role::Client, client, script_rng, &cli_rng, cli_ev);
  device_link.set_local_name("KMJ");
  device_link.set_known_peer(client.pub);
  client_link.begin_client(2, "desk");
  exchange(device_link, client_link);
  TEST_ASSERT_TRUE(device_link.secure());
  TEST_ASSERT_TRUE(client_link.secure());
  const uint8_t msg[] = {0x02, 0x04, 0x01};
  TEST_ASSERT_TRUE(client_link.send_app(msg, sizeof(msg)));
  exchange(device_link, client_link);
  TEST_ASSERT_EQUAL_UINT(sizeof(msg), dev_sink.app_len);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(msg, dev_sink.app, sizeof(msg));

  kmj::SessionInput in{};
  memcpy(in.client_static_pub, client.pub, 32);
  memcpy(in.client_static_priv, client.priv, 32);
  in.have_client_static_priv = true;
  memcpy(in.device_static_pub, device.pub, 32);
  uint8_t client_eph_pub[32];
  uint8_t device_eph_pub[32];
  kmj::x25519_public(client_eph_pub, cli_eph);
  kmj::x25519_public(device_eph_pub, dev_eph);
  memcpy(in.client_eph_pub, client_eph_pub, 32);
  memcpy(in.client_eph_priv, cli_eph, 32);
  in.have_client_eph_priv = true;
  memcpy(in.device_eph_pub, device_eph_pub, 32);
  kmj::SessionKeys keys{};
  TEST_ASSERT_TRUE(kmj::session_keys(in, keys));
  char hex[65];
  to_hex(keys.c2d, 32, hex);
  fprintf(stderr, "C2D %s\n", hex);
  TEST_ASSERT_EQUAL_STRING("f5619350ef423d9a2e7d51648d661a46b7b61fe4c6982cb8b9db96c470b2554e", hex);
  to_hex(keys.d2c, 32, hex);
  fprintf(stderr, "D2C %s\n", hex);
  to_hex(client_eph_pub, 32, hex);
  fprintf(stderr, "CEPH %s\n", hex);
  to_hex(device_eph_pub, 32, hex);
  fprintf(stderr, "DEPH %s\n", hex);
}

void test_unknown_peer_and_unarmed() {
  kmj::Identity device{};
  kmj::Identity client{};
  memset(device.priv, 1, 32);
  memset(client.priv, 2, 32);
  kmj::identity_from_priv(device);
  kmj::identity_from_priv(client);
  uint8_t eph[32];
  memset(eph, 3, 32);
  Script rng{eph};
  PairSink sink;
  kmj::LinkEvents ev{on_paired, on_secure, on_app, on_fail, &sink};
  kmj::Link device_link;
  kmj::Link client_link;
  device_link.reset(kmj::Role::Device, device, script_rng, &rng, ev);
  client_link.reset(kmj::Role::Client, client, script_rng, &rng, ev);
  client_link.begin_client(1, "desk");
  exchange(device_link, client_link);
  TEST_ASSERT_TRUE(sink.failed);
  TEST_ASSERT_FALSE(device_link.secure());
}

void test_jiggler_returns_and_counts() {
  int cursor = 0;
  kmj::Jiggler j;
  j.set_random(seq_rand, &cursor);
  j.start(0);
  TEST_ASSERT_EQUAL_INT(30, j.countdown_s(0));
  TEST_ASSERT_EQUAL_INT(29, j.countdown_s(1000));
  TEST_ASSERT_EQUAL_INT(1, j.countdown_s(29500));
  TEST_ASSERT_EQUAL_INT(-1, j.countdown_s(30000));
  TEST_ASSERT_EQUAL_UINT32(28800, j.run_left_s(0));
  TEST_ASSERT_EQUAL_UINT32(28799, j.run_left_s(1000));
  TEST_ASSERT_EQUAL_UINT32(28795, j.run_left_s(5000));
  int dx = 0;
  int dy = 0;
  int guard = 0;
  bool saw = false;
  while (j.jiggle_count() == 0 && guard++ < 100) {
    if (j.poll(30000 + static_cast<uint32_t>(guard) * 20, dx, dy)) {
      saw = true;
      j.ack();
    }
  }
  TEST_ASSERT_TRUE(saw);
  TEST_ASSERT_EQUAL_UINT32(1, j.jiggle_count());
  TEST_ASSERT_FALSE(j.moving());

  j.trigger_nudge();
  int net_x = 0;
  int net_y = 0;
  guard = 0;
  while (j.moving() && guard++ < 400) {
    if (j.poll(40000 + static_cast<uint32_t>(guard) * 20u, dx, dy)) {
      net_x += dx;
      net_y += dy;
      j.ack();
    }
  }
  TEST_ASSERT_EQUAL_INT(0, net_x);
  TEST_ASSERT_EQUAL_INT(0, net_y);
  TEST_ASSERT_FALSE(j.moving());
  TEST_ASSERT_EQUAL_UINT32(2, j.jiggle_count());

  j.start(0);
  TEST_ASSERT_TRUE(j.enabled());
  TEST_ASSERT_EQUAL_UINT32(28795, j.run_left_s(5000));
  j.stop(5000);
  TEST_ASSERT_FALSE(j.enabled());
  TEST_ASSERT_EQUAL_UINT32(28795, j.run_left_s(5000));
  TEST_ASSERT_EQUAL_UINT32(28795, j.run_left_s(60000));
  j.start(60000);
  TEST_ASSERT_TRUE(j.enabled());
  TEST_ASSERT_EQUAL_UINT32(28800, j.run_left_s(60000));
  TEST_ASSERT_EQUAL_UINT32(28799, j.run_left_s(61000));
  j.poll(60000u + 8u * 60u * 60u * 1000u + 5u, dx, dy);
  TEST_ASSERT_TRUE(j.time_up());
  TEST_ASSERT_FALSE(j.enabled());
  TEST_ASSERT_EQUAL_UINT32(0, j.run_left_s(60000u + 8u * 60u * 60u * 1000u + 5u));

  struct AxisSeq {
    int axis = 0;
    int call = 0;
  } axis_seq;
  kmj::Jiggler dir_j;
  dir_j.set_random(
      [](void* ctx, long lo, long hi) -> long {
        auto* s = static_cast<AxisSeq*>(ctx);
        if (lo == 0 && hi == 4) {
          return s->axis++;
        }
        return lo;
      },
      &axis_seq);
  const int expected_sign[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
  for (int d = 0; d < 4; d++) {
    dir_j.trigger_jiggle();
    TEST_ASSERT_TRUE(dir_j.moving());
    TEST_ASSERT_EQUAL_INT(expected_sign[d][0] * 10, dir_j.leg_dx());
    TEST_ASSERT_EQUAL_INT(expected_sign[d][1] * 10, dir_j.leg_dy());
    int sum_x = 0;
    int sum_y = 0;
    guard = 0;
    while (dir_j.moving() && guard++ < 100) {
      if (dir_j.poll(static_cast<uint32_t>(guard) * 20u, dx, dy)) {
        sum_x += dx;
        sum_y += dy;
        dir_j.ack();
      }
    }
    TEST_ASSERT_FALSE(dir_j.moving());
    TEST_ASSERT_EQUAL_INT(0, sum_x);
    TEST_ASSERT_EQUAL_INT(0, sum_y);
  }
}

void test_keys_and_mouse_chunks() {
  kmj::KeyReport6 report;
  TEST_ASSERT_TRUE(report.set(0xE0, true));
  TEST_ASSERT_EQUAL_UINT8(0x01, report.mods);
  TEST_ASSERT_TRUE(report.set(0x04, true));
  TEST_ASSERT_TRUE(report.set(0x05, true));
  TEST_ASSERT_TRUE(report.set(0x06, true));
  TEST_ASSERT_TRUE(report.set(0x07, true));
  TEST_ASSERT_TRUE(report.set(0x08, true));
  TEST_ASSERT_TRUE(report.set(0x09, true));
  TEST_ASSERT_FALSE(report.set(0x0A, true));
  TEST_ASSERT_TRUE(report.set(0x04, false));
  TEST_ASSERT_EQUAL_UINT8(0x05, report.keys[0]);
  TEST_ASSERT_EQUAL_UINT8(0x09, report.keys[4]);
  TEST_ASSERT_EQUAL_UINT8(0, report.keys[5]);
  TEST_ASSERT_TRUE(report.set(0x0A, true));
  TEST_ASSERT_EQUAL_UINT8(0x0A, report.keys[5]);
  report.clear();
  TEST_ASSERT_EQUAL_UINT8(0, report.mods);

  kmj::MouseAccum mouse;
  mouse.add_move(200, -200);
  int8_t x, y, w, p;
  uint8_t buttons = 0;
  TEST_ASSERT_TRUE(mouse.take(x, y, w, p, buttons));
  TEST_ASSERT_EQUAL_INT8(127, x);
  TEST_ASSERT_EQUAL_INT8(-127, y);
  TEST_ASSERT_TRUE(mouse.take(x, y, w, p, buttons));
  TEST_ASSERT_EQUAL_INT8(73, x);
  TEST_ASSERT_EQUAL_INT8(-73, y);
  TEST_ASSERT_FALSE(mouse.take(x, y, w, p, buttons));
}

void test_improv_checksum_and_wifi() {
  const uint8_t ssid[] = "lab";
  const uint8_t pass[] = "secret";
  uint8_t data[16];
  data[0] = 3;
  memcpy(data + 1, ssid, 3);
  data[4] = 6;
  memcpy(data + 5, pass, 6);
  uint8_t packet[32];
  const size_t n = kmj::improv_build(packet, sizeof(packet), 0x01, data, 11);
  TEST_ASSERT_TRUE(n > 0);
  const kmj::ImprovPacket parsed = kmj::improv_parse(packet, n);
  TEST_ASSERT_TRUE(parsed.valid);
  TEST_ASSERT_EQUAL_UINT8(0x01, parsed.command);
  char ssid_out[33];
  char pass_out[65];
  TEST_ASSERT_TRUE(kmj::improv_wifi_credentials(parsed.data, parsed.data_len, ssid_out, sizeof(ssid_out), pass_out,
                                                sizeof(pass_out)));
  TEST_ASSERT_EQUAL_STRING("lab", ssid_out);
  TEST_ASSERT_EQUAL_STRING("secret", pass_out);
  packet[n - 1] ^= 0xff;
  TEST_ASSERT_FALSE(kmj::improv_parse(packet, n).valid);
  TEST_ASSERT_TRUE(kmj::hostname_ok("kmj-1"));
  TEST_ASSERT_FALSE(kmj::hostname_ok("-bad"));
  TEST_ASSERT_FALSE(kmj::hostname_ok("has space"));
  const char* strs[] = {"KMJ", "0.1.0"};
  uint8_t result[64];
  const size_t rn = kmj::improv_result(result, sizeof(result), 0x03, strs, 2);
  TEST_ASSERT_TRUE(kmj::improv_parse(result, rn).valid);
}

void test_button_debounce() {
  kmj::Button btn;
  btn.begin(false, 1000);

  // Bouncing right after the edge must not confirm a press.
  TEST_ASSERT_FALSE(btn.update(true, 1010));
  TEST_ASSERT_FALSE(btn.update(false, 1015));
  TEST_ASSERT_FALSE(btn.update(true, 1020));
  TEST_ASSERT_FALSE(btn.down());

  // Stable low for the debounce window confirms exactly one press.
  TEST_ASSERT_TRUE(btn.update(true, 1055));
  TEST_ASSERT_TRUE(btn.down());
  TEST_ASSERT_FALSE(btn.update(true, 2000));

  // Release also needs a stable window; a bounce does not flip the state.
  TEST_ASSERT_FALSE(btn.update(false, 2010));
  TEST_ASSERT_TRUE(btn.down());
  TEST_ASSERT_FALSE(btn.update(true, 2015));
  TEST_ASSERT_TRUE(btn.down());
  TEST_ASSERT_FALSE(btn.update(false, 2045));  // first stable sample, window running
  TEST_ASSERT_TRUE(btn.down());
  TEST_ASSERT_FALSE(btn.update(false, 2080));  // window elapsed: release confirmed
  TEST_ASSERT_FALSE(btn.down());
}

void test_button_begin_held() {
  kmj::Button btn;
  // Held during boot: seeded down, no phantom press event.
  btn.begin(true, 500);
  TEST_ASSERT_TRUE(btn.down());
  TEST_ASSERT_FALSE(btn.update(true, 600));
}

void test_round_layout_fits_circle() {
  using namespace kmj::layout_round;
  for (size_t i = 0; i < kRoundWidgetCount; i++) {
    const RoundWidget& w = kRoundWidgets[i];
    TEST_ASSERT_TRUE_MESSAGE(rect_inside_circle(w.rect), w.name);
    if (w.interactive) {
      TEST_ASSERT_TRUE_MESSAGE(w.rect.w >= kMinTouchW, w.name);
      TEST_ASSERT_TRUE_MESSAGE(w.rect.h >= kMinTouchH, w.name);
    }
    TEST_ASSERT_TRUE_MESSAGE(w.rect.x >= 0 && w.rect.y >= 0, w.name);
    TEST_ASSERT_TRUE_MESSAGE(w.rect.x + w.rect.w <= kRoundSize && w.rect.y + w.rect.h <= kRoundSize, w.name);
  }
  // The status strip must stay inside the top/bottom bands where the chord is
  // wide enough to be readable: the outermost status rows sit no further from
  // the center than 140 px.
  TEST_ASSERT_LESS_OR_EQUAL_INT(140, kBat.y + kBat.h - kRoundCenter);
  TEST_ASSERT_LESS_OR_EQUAL_INT(140, kRoundCenter - kIp.y);
  // A sample chord: at the widest row the usable width is the full diameter
  // minus the margin.
  TEST_ASSERT_EQUAL_INT(kRoundUsableRadius, chord_half_width(kRoundCenter));
  TEST_ASSERT_EQUAL_INT(0, chord_half_width(0));
}

void test_default_names() {
  char buf[32];
  // The example from the request: MAC 78:ab:bb:9b:7c:9f.
  TEST_ASSERT_TRUE(kmj::format_default_name("KMJ-TD", 0x9F, buf, sizeof(buf)));
  TEST_ASSERT_EQUAL_STRING("KMJ-TD-9F", buf);
  TEST_ASSERT_TRUE(kmj::format_default_name("KMJ-TL", 0x9F, buf, sizeof(buf)));
  TEST_ASSERT_EQUAL_STRING("KMJ-TL-9F", buf);
  // Leading zero bytes stay two digits.
  TEST_ASSERT_TRUE(kmj::format_default_name("KMJ-TD", 0x0A, buf, sizeof(buf)));
  TEST_ASSERT_EQUAL_STRING("KMJ-TD-0A", buf);
  TEST_ASSERT_TRUE(kmj::format_default_name("KMJ-TL", 0x00, buf, sizeof(buf)));
  TEST_ASSERT_EQUAL_STRING("KMJ-TL-00", buf);
  // Both defaults are valid hostnames and fit the settings buffers.
  TEST_ASSERT_TRUE(kmj::hostname_ok("KMJ-TD-9F"));
  TEST_ASSERT_TRUE(kmj::hostname_ok("KMJ-TL-9F"));
  // Too-small buffer fails.
  TEST_ASSERT_FALSE(kmj::format_default_name("KMJ-TD", 0x9F, buf, 8));
  TEST_ASSERT_FALSE(kmj::format_default_name("KMJ-TD", 0x9F, buf, 0));
  TEST_ASSERT_FALSE(kmj::format_default_name("", 0x9F, buf, sizeof(buf)));
  TEST_ASSERT_FALSE(kmj::format_default_name(nullptr, 0x9F, buf, sizeof(buf)));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_blake2_and_x25519);
  RUN_TEST(test_aead_roundtrip);
  RUN_TEST(test_pair_and_session);
  RUN_TEST(test_unknown_peer_and_unarmed);
  RUN_TEST(test_jiggler_returns_and_counts);
  RUN_TEST(test_keys_and_mouse_chunks);
  RUN_TEST(test_improv_checksum_and_wifi);
  RUN_TEST(test_button_debounce);
  RUN_TEST(test_button_begin_held);
  RUN_TEST(test_round_layout_fits_circle);
  RUN_TEST(test_default_names);
  return UNITY_END();
}
