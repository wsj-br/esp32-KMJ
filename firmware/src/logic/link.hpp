#pragma once

#include "logic/session.hpp"

namespace kmj {

enum class Role : uint8_t { Device, Client };

using RngFn = void (*)(void* ctx, uint8_t* dst, size_t n);
using PeerFn = bool (*)(void* ctx, const uint8_t pub[32]);

struct LinkEvents {
  void (*on_paired)(void* ctx, const uint8_t peer_pub[32]);
  void (*on_secure)(void* ctx);
  void (*on_app)(void* ctx, const uint8_t* msg, size_t n);
  void (*on_fail)(void* ctx, const char* reason);
  void* ctx;
};

// One KMJ handshake + transport. Feed inbound bytes, pull outbound bytes.
class Link {
 public:
  void reset(Role role, const Identity& self, RngFn rng, void* rng_ctx, LinkEvents events);
  void set_local_name(const char* name);
  void set_pairing_armed(bool armed);
  void set_known_peer(const uint8_t pub[32]);
  void clear_known_peer();
  void set_peer_check(PeerFn fn, void* ctx);

  void begin_client(uint8_t intent, const char* name);
  void confirm();
  void reject();

  void feed(const uint8_t* data, size_t n);
  size_t read_out(uint8_t* dst, size_t cap);
  bool send_app(const uint8_t* msg, size_t n);

  bool secure() const { return state_ == 6; }
  bool waiting_confirm() const { return wait_confirm_; }
  bool finished_pair() const { return paired_; }
  const char* pair_code() const { return code_; }
  const char* peer_name() const { return peer_name_; }
  const char* fail() const { return fail_; }
  const uint8_t* peer_pub() const { return peer_pub_; }

 private:
  bool parse_one();
  void handle(uint8_t type, const uint8_t* body, uint16_t len);
  void send_frame(uint8_t type, const uint8_t* body, uint16_t len);
  void fail(const char* reason);
  void send_server_hello(uint8_t status);
  void send_eph(uint8_t type);
  void fill_pair(PairTranscript& t) const;
  void fill_session_input(SessionInput& in) const;
  bool derive();
  void send_pair_mac(char role_char, uint8_t type);
  void send_sess_mac(char role_char, uint8_t type);

  Role role_ = Role::Device;
  Identity self_{};
  RngFn rng_ = nullptr;
  void* rng_ctx_ = nullptr;
  LinkEvents ev_{};
  bool armed_ = false;
  PeerFn peer_fn_ = nullptr;
  void* peer_ctx_ = nullptr;
  bool have_peer_ = false;
  uint8_t known_peer_[32]{};
  uint8_t peer_pub_[32]{};
  uint8_t client_eph_[32]{};
  uint8_t device_eph_[32]{};
  uint8_t eph_priv_[32]{};
  bool have_eph_priv_ = false;
  uint8_t intent_ = 0;
  char name_[32]{};
  char peer_name_[32]{};
  char code_[8]{};
  char fail_[48]{};
  bool wait_confirm_ = false;
  bool local_confirmed_ = false;
  bool peer_accepted_ = false;
  bool paired_ = false;
  uint8_t state_ = 0;
  SessionKeys keys_{};
  uint64_t tx_n_ = 0;
  uint64_t rx_n_ = 0;
  uint8_t in_[1024]{};
  size_t in_len_ = 0;
  uint8_t out_[1536]{};
  size_t out_len_ = 0;
  size_t out_rd_ = 0;
};

}  // namespace kmj
