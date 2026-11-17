#include "logic/link.hpp"

#include <stdio.h>
#include <string.h>

namespace kmj {
namespace {

constexpr uint8_t kClientHello = 0x10;
constexpr uint8_t kServerHello = 0x11;
constexpr uint8_t kPairEph = 0x20;
constexpr uint8_t kPairEphReply = 0x21;
constexpr uint8_t kPairAccept = 0x23;
constexpr uint8_t kPairAcceptReply = 0x24;
constexpr uint8_t kPairReject = 0x25;
constexpr uint8_t kSessEph = 0x30;
constexpr uint8_t kSessEphReply = 0x31;
constexpr uint8_t kSessClientMac = 0x32;
constexpr uint8_t kSessServerMac = 0x33;
constexpr uint8_t kEncrypted = 0x7E;

constexpr uint8_t kStWaitHello = 0;
constexpr uint8_t kStWaitServer = 1;
constexpr uint8_t kStWaitPairEph = 2;
constexpr uint8_t kStWaitPairAccept = 3;
constexpr uint8_t kStWaitSessEph = 4;
constexpr uint8_t kStWaitSessMac = 5;
constexpr uint8_t kStSecure = 6;
constexpr uint8_t kStClosed = 7;

void copy_name(char* dst, size_t cap, const uint8_t* src, size_t n) {
  if (n >= cap) {
    n = cap - 1;
  }
  memcpy(dst, src, n);
  dst[n] = '\0';
}

}  // namespace

void Link::reset(Role role, const Identity& self, RngFn rng, void* rng_ctx, LinkEvents events) {
  wipe(eph_priv_, sizeof(eph_priv_));
  wipe(&keys_, sizeof(keys_));
  *this = Link();
  role_ = role;
  self_ = self;
  rng_ = rng;
  rng_ctx_ = rng_ctx;
  ev_ = events;
  state_ = role == Role::Device ? kStWaitHello : kStClosed;
}

void Link::set_local_name(const char* name) {
  snprintf(name_, sizeof(name_), "%s", name != nullptr ? name : "");
}

void Link::set_pairing_armed(bool armed) { armed_ = armed; }

void Link::set_known_peer(const uint8_t pub[32]) {
  memcpy(known_peer_, pub, 32);
  have_peer_ = true;
}

void Link::clear_known_peer() { have_peer_ = false; }

void Link::set_peer_check(PeerFn fn, void* ctx) {
  peer_fn_ = fn;
  peer_ctx_ = ctx;
}

void Link::fail(const char* reason) {
  if (state_ == kStClosed) {
    return;
  }
  snprintf(fail_, sizeof(fail_), "%s", reason);
  state_ = kStClosed;
  wait_confirm_ = false;
  if (ev_.on_fail) {
    ev_.on_fail(ev_.ctx, fail_);
  }
}

void Link::send_frame(uint8_t type, const uint8_t* body, uint16_t len) {
  if (out_len_ + 3u + len > sizeof(out_)) {
    fail("outbound full");
    return;
  }
  out_[out_len_++] = type;
  out_[out_len_++] = static_cast<uint8_t>(len & 0xff);
  out_[out_len_++] = static_cast<uint8_t>(len >> 8);
  if (len > 0 && body != nullptr) {
    memcpy(out_ + out_len_, body, len);
    out_len_ += len;
  }
}

size_t Link::read_out(uint8_t* dst, size_t cap) {
  const size_t avail = out_len_ - out_rd_;
  const size_t n = avail < cap ? avail : cap;
  if (n > 0) {
    memcpy(dst, out_ + out_rd_, n);
  }
  out_rd_ += n;
  if (out_rd_ == out_len_) {
    out_rd_ = 0;
    out_len_ = 0;
  }
  return n;
}

void Link::begin_client(uint8_t intent, const char* name) {
  if (role_ != Role::Client) {
    fail("not client");
    return;
  }
  intent_ = intent;
  snprintf(name_, sizeof(name_), "%s", name != nullptr ? name : "");
  uint8_t body[1 + 1 + 32 + 1 + 31];
  body[0] = 1;
  body[1] = intent;
  memcpy(body + 2, self_.pub, 32);
  const size_t nlen = strlen(name_);
  body[34] = static_cast<uint8_t>(nlen > 31 ? 31 : nlen);
  memcpy(body + 35, name_, body[34]);
  state_ = kStWaitServer;
  send_frame(kClientHello, body, static_cast<uint16_t>(35 + body[34]));
}

void Link::send_server_hello(uint8_t status) {
  uint8_t body[1 + 1 + 32 + 1 + 31];
  body[0] = 1;
  body[1] = status;
  memcpy(body + 2, self_.pub, 32);
  const size_t nlen = strlen(name_);
  body[34] = static_cast<uint8_t>(nlen > 31 ? 31 : nlen);
  memcpy(body + 35, name_, body[34]);
  send_frame(kServerHello, body, static_cast<uint16_t>(35 + body[34]));
}

void Link::send_eph(uint8_t type) {
  if (!have_eph_priv_) {
    if (rng_ == nullptr) {
      fail("no rng");
      return;
    }
    rng_(rng_ctx_, eph_priv_, 32);
    have_eph_priv_ = true;
  }
  uint8_t pub[32];
  x25519_public(pub, eph_priv_);
  if (role_ == Role::Client) {
    memcpy(client_eph_, pub, 32);
  } else {
    memcpy(device_eph_, pub, 32);
  }
  send_frame(type, pub, 32);
}

void Link::fill_pair(PairTranscript& t) const {
  memcpy(t.client_static, role_ == Role::Client ? self_.pub : peer_pub_, 32);
  memcpy(t.device_static, role_ == Role::Device ? self_.pub : peer_pub_, 32);
  memcpy(t.client_eph, client_eph_, 32);
  memcpy(t.device_eph, device_eph_, 32);
  pair_shared(t.shared, eph_priv_, role_ == Role::Client ? device_eph_ : client_eph_);
}

void Link::fill_session_input(SessionInput& in) const {
  memset(&in, 0, sizeof(in));
  if (role_ == Role::Client) {
    memcpy(in.client_static_pub, self_.pub, 32);
    memcpy(in.client_static_priv, self_.priv, 32);
    in.have_client_static_priv = true;
    memcpy(in.device_static_pub, peer_pub_, 32);
    memcpy(in.client_eph_pub, client_eph_, 32);
    memcpy(in.client_eph_priv, eph_priv_, 32);
    in.have_client_eph_priv = true;
    memcpy(in.device_eph_pub, device_eph_, 32);
  } else {
    memcpy(in.device_static_pub, self_.pub, 32);
    memcpy(in.device_static_priv, self_.priv, 32);
    in.have_device_static_priv = true;
    memcpy(in.client_static_pub, peer_pub_, 32);
    memcpy(in.client_eph_pub, client_eph_, 32);
    memcpy(in.device_eph_pub, device_eph_, 32);
    memcpy(in.device_eph_priv, eph_priv_, 32);
    in.have_device_eph_priv = true;
  }
}

bool Link::derive() {
  SessionInput in;
  fill_session_input(in);
  const bool ok = session_keys(in, keys_);
  wipe(&in, sizeof(in));
  return ok;
}

void Link::send_pair_mac(char role_char, uint8_t type) {
  PairTranscript t;
  fill_pair(t);
  uint8_t mac[16];
  pair_mac(t, role_char, mac);
  wipe(t.shared, sizeof(t.shared));
  send_frame(type, mac, 16);
}

void Link::send_sess_mac(char role_char, uint8_t type) {
  uint8_t mac[16];
  const uint8_t* cstatic = role_ == Role::Client ? self_.pub : peer_pub_;
  const uint8_t* dstatic = role_ == Role::Device ? self_.pub : peer_pub_;
  session_mac(keys_, cstatic, dstatic, client_eph_, device_eph_, role_char, mac);
  send_frame(type, mac, 16);
}

void Link::confirm() {
  if (!wait_confirm_) {
    return;
  }
  local_confirmed_ = true;
  wait_confirm_ = false;
  if (role_ == Role::Client) {
    send_pair_mac('C', kPairAccept);
    return;
  }
  if (peer_accepted_) {
    send_pair_mac('D', kPairAcceptReply);
    paired_ = true;
    armed_ = false;
    if (ev_.on_paired) {
      ev_.on_paired(ev_.ctx, peer_pub_);
    }
    state_ = kStClosed;
  }
}

void Link::reject() {
  if (state_ == kStClosed) {
    return;
  }
  send_frame(kPairReject, nullptr, 0);
  fail("rejected");
}

void Link::feed(const uint8_t* data, size_t n) {
  if (state_ == kStClosed || n == 0) {
    return;
  }
  if (in_len_ + n > sizeof(in_)) {
    fail("inbound full");
    return;
  }
  memcpy(in_ + in_len_, data, n);
  in_len_ += n;
  while (state_ != kStClosed && parse_one()) {
  }
}

bool Link::parse_one() {
  if (in_len_ < 3) {
    return false;
  }
  const uint16_t len = static_cast<uint16_t>(in_[1] | (static_cast<uint16_t>(in_[2]) << 8));
  if (len > 700) {
    fail("frame too large");
    return false;
  }
  if (in_len_ < 3u + len) {
    return false;
  }
  const uint8_t type = in_[0];
  uint8_t body[700];
  if (len > 0) {
    memcpy(body, in_ + 3, len);
  }
  const size_t rest = in_len_ - (3u + len);
  memmove(in_, in_ + 3 + len, rest);
  in_len_ = rest;
  handle(type, body, len);
  return true;
}

void Link::handle(uint8_t type, const uint8_t* body, uint16_t len) {
  if (type == kClientHello && role_ == Role::Device && state_ == kStWaitHello) {
    if (len < 35 || body[0] != 1) {
      fail("bad hello");
      return;
    }
    intent_ = body[1];
    memcpy(peer_pub_, body + 2, 32);
    const uint8_t nlen = body[34];
    if (35u + nlen > len) {
      fail("bad hello");
      return;
    }
    copy_name(peer_name_, sizeof(peer_name_), body + 35, nlen);
    if (intent_ == 1) {
      if (!armed_) {
        send_server_hello(2);
        fail("pairing not armed");
        return;
      }
      send_server_hello(0);
      state_ = kStWaitPairEph;
      return;
    }
    if (intent_ == 2) {
      const bool known = peer_fn_ != nullptr ? peer_fn_(peer_ctx_, peer_pub_)
                                             : (have_peer_ && pub_equal(known_peer_, peer_pub_));
      if (!known) {
        send_server_hello(3);
        fail("unknown peer");
        return;
      }
      send_server_hello(1);
      state_ = kStWaitSessEph;
      return;
    }
    send_server_hello(5);
    fail("bad intent");
    return;
  }

  if (type == kServerHello && role_ == Role::Client && state_ == kStWaitServer) {
    if (len < 35 || body[0] != 1) {
      fail("bad hello");
      return;
    }
    const uint8_t status = body[1];
    memcpy(peer_pub_, body + 2, 32);
    const uint8_t nlen = body[34];
    if (35u + nlen > len) {
      fail("bad hello");
      return;
    }
    copy_name(peer_name_, sizeof(peer_name_), body + 35, nlen);
    if (status == 2) {
      fail("pairing not armed");
      return;
    }
    if (status == 3) {
      fail("unknown peer");
      return;
    }
    if (status == 5) {
      fail("bad version");
      return;
    }
    if (intent_ == 1 && status == 0) {
      send_eph(kPairEph);
      state_ = kStWaitPairEph;
      return;
    }
    if (intent_ == 2 && status == 1) {
      send_eph(kSessEph);
      state_ = kStWaitSessEph;
      return;
    }
    fail("unexpected status");
    return;
  }

  if (type == kPairEph && role_ == Role::Device && state_ == kStWaitPairEph && len == 32) {
    memcpy(client_eph_, body, 32);
    send_eph(kPairEphReply);
    PairTranscript t;
    fill_pair(t);
    kmj::pair_code(t, code_);
    wipe(t.shared, sizeof(t.shared));
    wait_confirm_ = true;
    state_ = kStWaitPairAccept;
    return;
  }

  if (type == kPairEphReply && role_ == Role::Client && state_ == kStWaitPairEph && len == 32) {
    memcpy(device_eph_, body, 32);
    PairTranscript t;
    fill_pair(t);
    kmj::pair_code(t, code_);
    wipe(t.shared, sizeof(t.shared));
    wait_confirm_ = true;
    state_ = kStWaitPairAccept;
    return;
  }

  if (type == kPairAccept && role_ == Role::Device && state_ == kStWaitPairAccept && len == 16) {
    PairTranscript t;
    fill_pair(t);
    uint8_t expect[16];
    pair_mac(t, 'C', expect);
    wipe(t.shared, sizeof(t.shared));
    if (!mac_equal(expect, body)) {
      fail("pair mac");
      return;
    }
    peer_accepted_ = true;
    if (local_confirmed_) {
      send_pair_mac('D', kPairAcceptReply);
      paired_ = true;
      armed_ = false;
      if (ev_.on_paired) {
        ev_.on_paired(ev_.ctx, peer_pub_);
      }
      state_ = kStClosed;
    }
    return;
  }

  if (type == kPairAcceptReply && role_ == Role::Client && state_ == kStWaitPairAccept && len == 16) {
    PairTranscript t;
    fill_pair(t);
    uint8_t expect[16];
    pair_mac(t, 'D', expect);
    wipe(t.shared, sizeof(t.shared));
    if (!mac_equal(expect, body)) {
      fail("pair mac");
      return;
    }
    paired_ = true;
    state_ = kStClosed;
    if (ev_.on_paired) {
      ev_.on_paired(ev_.ctx, peer_pub_);
    }
    return;
  }

  if (type == kPairReject) {
    fail("rejected");
    return;
  }

  if (type == kSessEph && role_ == Role::Device && state_ == kStWaitSessEph && len == 32) {
    memcpy(client_eph_, body, 32);
    send_eph(kSessEphReply);
    if (!derive()) {
      fail("kdf");
      return;
    }
    state_ = kStWaitSessMac;
    return;
  }

  if (type == kSessEphReply && role_ == Role::Client && state_ == kStWaitSessEph && len == 32) {
    memcpy(device_eph_, body, 32);
    if (!derive()) {
      fail("kdf");
      return;
    }
    send_sess_mac('C', kSessClientMac);
    state_ = kStWaitSessMac;
    return;
  }

  if (type == kSessClientMac && role_ == Role::Device && state_ == kStWaitSessMac && len == 16) {
    uint8_t expect[16];
    session_mac(keys_, peer_pub_, self_.pub, client_eph_, device_eph_, 'C', expect);
    if (!mac_equal(expect, body)) {
      fail("session mac");
      return;
    }
    send_sess_mac('D', kSessServerMac);
    state_ = kStSecure;
    tx_n_ = 0;
    rx_n_ = 0;
    if (ev_.on_secure) {
      ev_.on_secure(ev_.ctx);
    }
    return;
  }

  if (type == kSessServerMac && role_ == Role::Client && state_ == kStWaitSessMac && len == 16) {
    uint8_t expect[16];
    session_mac(keys_, self_.pub, peer_pub_, client_eph_, device_eph_, 'D', expect);
    if (!mac_equal(expect, body)) {
      fail("session mac");
      return;
    }
    state_ = kStSecure;
    tx_n_ = 0;
    rx_n_ = 0;
    if (ev_.on_secure) {
      ev_.on_secure(ev_.ctx);
    }
    return;
  }

  if (type == kEncrypted && state_ == kStSecure && len >= 16) {
    const size_t clen = len - 16;
    uint8_t plain[700];
    uint8_t nonce[24];
    nonce_from_counter(nonce, rx_n_);
    const uint8_t* key = role_ == Role::Client ? keys_.d2c : keys_.c2d;
    if (!aead_open(plain, body + clen, key, nonce, body, clen)) {
      fail("decrypt");
      return;
    }
    rx_n_++;
    if (ev_.on_app) {
      ev_.on_app(ev_.ctx, plain, clen);
    }
    return;
  }

  fail("unexpected frame");
}

bool Link::send_app(const uint8_t* msg, size_t n) {
  if (state_ != kStSecure || n > 400) {
    return false;
  }
  uint8_t cipher[400];
  uint8_t mac[16];
  uint8_t nonce[24];
  nonce_from_counter(nonce, tx_n_);
  const uint8_t* key = role_ == Role::Client ? keys_.c2d : keys_.d2c;
  aead_seal(cipher, mac, key, nonce, msg, n);
  uint8_t body[416];
  memcpy(body, cipher, n);
  memcpy(body + n, mac, 16);
  tx_n_++;
  send_frame(kEncrypted, body, static_cast<uint16_t>(n + 16));
  return state_ == kStSecure;
}

}  // namespace kmj
