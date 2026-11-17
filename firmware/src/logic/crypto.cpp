#include "logic/crypto.hpp"

#include <string.h>

extern "C" {
#include "monocypher.h"
}

namespace kmj {

void hash32(uint8_t out[32], const Slice* parts, size_t count) {
  crypto_blake2b_ctx ctx;
  crypto_blake2b_init(&ctx, 32);
  for (size_t i = 0; i < count; i++) {
    if (parts[i].n > 0) {
      crypto_blake2b_update(&ctx, parts[i].p, parts[i].n);
    }
  }
  crypto_blake2b_final(&ctx, out);
}

void x25519_public(uint8_t pub[32], const uint8_t priv[32]) {
  crypto_x25519_public_key(pub, priv);
}

void x25519_shared(uint8_t out[32], const uint8_t priv[32], const uint8_t peer_pub[32]) {
  crypto_x25519(out, priv, peer_pub);
}

void aead_seal(uint8_t* cipher, uint8_t mac[16], const uint8_t key[32], const uint8_t nonce[24],
               const uint8_t* plain, size_t n) {
  crypto_aead_lock(cipher, mac, key, nonce, nullptr, 0, plain, n);
}

bool aead_open(uint8_t* plain, const uint8_t mac[16], const uint8_t key[32], const uint8_t nonce[24],
               const uint8_t* cipher, size_t n) {
  return crypto_aead_unlock(plain, mac, key, nonce, nullptr, 0, cipher, n) == 0;
}

void nonce_from_counter(uint8_t nonce[24], uint64_t counter) {
  memset(nonce, 0, 24);
  for (int i = 0; i < 8; i++) {
    nonce[16 + i] = static_cast<uint8_t>(counter >> (8 * i));
  }
}

bool mac_equal(const uint8_t a[16], const uint8_t b[16]) {
  return crypto_verify16(a, b) == 0;
}

bool pub_equal(const uint8_t a[32], const uint8_t b[32]) {
  return crypto_verify32(a, b) == 0;
}

void wipe(void* p, size_t n) { crypto_wipe(p, n); }

void identity_from_priv(Identity& id) { x25519_public(id.pub, id.priv); }

}  // namespace kmj
