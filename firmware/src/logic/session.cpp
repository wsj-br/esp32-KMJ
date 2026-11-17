#include "logic/session.hpp"

#include <stdio.h>
#include <string.h>

namespace kmj {
namespace {

Slice raw(const void* p, size_t n) {
  Slice s;
  s.p = static_cast<const uint8_t*>(p);
  s.n = n;
  return s;
}

Slice bytes(const uint8_t* p, size_t n) { return raw(p, n); }

const char kPairLabel[] = "KMJ-PAIR-v1";
const char kSessLabel[] = "KMJ-SESS-v1";

}  // namespace

void pair_shared(uint8_t out[32], const uint8_t eph_priv[32], const uint8_t peer_eph_pub[32]) {
  x25519_shared(out, eph_priv, peer_eph_pub);
}

void pair_code(const PairTranscript& t, char out[7]) {
  Slice parts[6];
  parts[0] = bytes(t.shared, 32);
  parts[1] = raw(kPairLabel, sizeof(kPairLabel) - 1);
  parts[2] = bytes(t.client_static, 32);
  parts[3] = bytes(t.device_static, 32);
  parts[4] = bytes(t.client_eph, 32);
  parts[5] = bytes(t.device_eph, 32);
  uint8_t material[32];
  hash32(material, parts, 6);
  const unsigned value = (static_cast<unsigned>(material[0]) << 16) |
                         (static_cast<unsigned>(material[1]) << 8) |
                         static_cast<unsigned>(material[2]);
  snprintf(out, 7, "%06u", value % 1000000u);
  wipe(material, sizeof(material));
}

void pair_mac(const PairTranscript& t, char role, uint8_t mac[16]) {
  const uint8_t role_b = static_cast<uint8_t>(role);
  Slice parts[7];
  parts[0] = bytes(t.shared, 32);
  parts[1] = bytes(&role_b, 1);
  parts[2] = raw(kPairLabel, sizeof(kPairLabel) - 1);
  parts[3] = bytes(t.client_static, 32);
  parts[4] = bytes(t.device_static, 32);
  parts[5] = bytes(t.client_eph, 32);
  parts[6] = bytes(t.device_eph, 32);
  uint8_t full[32];
  hash32(full, parts, 7);
  memcpy(mac, full, 16);
  wipe(full, sizeof(full));
}

bool session_keys(const SessionInput& in, SessionKeys& out) {
  uint8_t ss_es[32];
  uint8_t ss_ee[32];
  uint8_t ss_se[32];
  if (in.have_device_static_priv) {
    x25519_shared(ss_es, in.device_static_priv, in.client_eph_pub);
  } else if (in.have_client_eph_priv) {
    x25519_shared(ss_es, in.client_eph_priv, in.device_static_pub);
  } else {
    return false;
  }
  if (in.have_client_eph_priv) {
    x25519_shared(ss_ee, in.client_eph_priv, in.device_eph_pub);
  } else if (in.have_device_eph_priv) {
    x25519_shared(ss_ee, in.device_eph_priv, in.client_eph_pub);
  } else {
    return false;
  }
  if (in.have_device_eph_priv) {
    x25519_shared(ss_se, in.device_eph_priv, in.client_static_pub);
  } else if (in.have_client_static_priv) {
    x25519_shared(ss_se, in.client_static_priv, in.device_eph_pub);
  } else {
    return false;
  }

  uint8_t ck0[32];
  uint8_t ck1[32];
  uint8_t ck2[32];
  uint8_t ck3[32];
  Slice p0[3] = {raw(kSessLabel, sizeof(kSessLabel) - 1), bytes(in.client_static_pub, 32),
                 bytes(in.device_static_pub, 32)};
  hash32(ck0, p0, 3);
  Slice p1[2] = {bytes(ck0, 32), bytes(ss_es, 32)};
  hash32(ck1, p1, 2);
  Slice p2[2] = {bytes(ck1, 32), bytes(ss_ee, 32)};
  hash32(ck2, p2, 2);
  Slice p3[2] = {bytes(ck2, 32), bytes(ss_se, 32)};
  hash32(ck3, p3, 2);

  const char* labels[3] = {"c2d", "d2c", "mac"};
  uint8_t* dest[3] = {out.c2d, out.d2c, out.mac_key};
  for (int i = 0; i < 3; i++) {
    Slice parts[2] = {raw(labels[i], 3), bytes(ck3, 32)};
    hash32(dest[i], parts, 2);
  }
  wipe(ss_es, sizeof(ss_es));
  wipe(ss_ee, sizeof(ss_ee));
  wipe(ss_se, sizeof(ss_se));
  wipe(ck0, sizeof(ck0));
  wipe(ck1, sizeof(ck1));
  wipe(ck2, sizeof(ck2));
  wipe(ck3, sizeof(ck3));
  return true;
}

void session_mac(const SessionKeys& keys, const uint8_t client_static[32], const uint8_t device_static[32],
                 const uint8_t client_eph[32], const uint8_t device_eph[32], char role, uint8_t mac[16]) {
  const uint8_t role_b = static_cast<uint8_t>(role);
  Slice parts[6] = {bytes(keys.mac_key, 32), bytes(&role_b, 1), bytes(client_static, 32),
                    bytes(device_static, 32), bytes(client_eph, 32), bytes(device_eph, 32)};
  uint8_t full[32];
  hash32(full, parts, 6);
  memcpy(mac, full, 16);
  wipe(full, sizeof(full));
}

}  // namespace kmj
