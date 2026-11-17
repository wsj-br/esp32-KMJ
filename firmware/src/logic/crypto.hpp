#pragma once

#include <stddef.h>
#include <stdint.h>

namespace kmj {

struct Slice {
  const uint8_t* p;
  size_t n;
};

void hash32(uint8_t out[32], const Slice* parts, size_t count);
void x25519_public(uint8_t pub[32], const uint8_t priv[32]);
void x25519_shared(uint8_t out[32], const uint8_t priv[32], const uint8_t peer_pub[32]);
void aead_seal(uint8_t* cipher, uint8_t mac[16], const uint8_t key[32], const uint8_t nonce[24],
               const uint8_t* plain, size_t n);
bool aead_open(uint8_t* plain, const uint8_t mac[16], const uint8_t key[32], const uint8_t nonce[24],
               const uint8_t* cipher, size_t n);
void nonce_from_counter(uint8_t nonce[24], uint64_t counter);
bool mac_equal(const uint8_t a[16], const uint8_t b[16]);
bool pub_equal(const uint8_t a[32], const uint8_t b[32]);
void wipe(void* p, size_t n);

struct Identity {
  uint8_t priv[32];
  uint8_t pub[32];
};

void identity_from_priv(Identity& id);

}  // namespace kmj
