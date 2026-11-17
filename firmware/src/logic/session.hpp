#pragma once

#include "logic/crypto.hpp"

namespace kmj {

// See proto/PROTOCOL.md. These helpers are the bytes both sides must match.

struct PairTranscript {
  uint8_t client_static[32];
  uint8_t device_static[32];
  uint8_t client_eph[32];
  uint8_t device_eph[32];
  uint8_t shared[32];
};

void pair_shared(uint8_t out[32], const uint8_t eph_priv[32], const uint8_t peer_eph_pub[32]);
void pair_code(const PairTranscript& t, char out[7]);
void pair_mac(const PairTranscript& t, char role, uint8_t mac[16]);

struct SessionKeys {
  uint8_t c2d[32];
  uint8_t d2c[32];
  uint8_t mac_key[32];
};

// priv flags say which private keys this side holds. The matching public keys are always set.
struct SessionInput {
  uint8_t client_static_pub[32];
  uint8_t client_static_priv[32];
  bool have_client_static_priv;
  uint8_t device_static_pub[32];
  uint8_t device_static_priv[32];
  bool have_device_static_priv;
  uint8_t client_eph_pub[32];
  uint8_t client_eph_priv[32];
  bool have_client_eph_priv;
  uint8_t device_eph_pub[32];
  uint8_t device_eph_priv[32];
  bool have_device_eph_priv;
};

bool session_keys(const SessionInput& in, SessionKeys& out);
void session_mac(const SessionKeys& keys, const uint8_t client_static[32], const uint8_t device_static[32],
                 const uint8_t client_eph[32], const uint8_t device_eph[32], char role, uint8_t mac[16]);

}  // namespace kmj
