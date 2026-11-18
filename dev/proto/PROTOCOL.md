# KMJ protocol v1

KMJ uses TCP port **42424** with `TCP_NODELAY` enabled. Unless stated otherwise, integers are unsigned and little-endian, and lengths count bytes rather than characters.

Cleartext frames and encrypted records share one framing:

| Field | Size |
| --- | --- |
| type | u8 |
| body length | u16 little-endian |
| body | `length` bytes |

The maximum frame body is 700 bytes. An encrypted body includes its 16-byte authentication tag, so its plaintext cannot exceed 684 bytes. The firmware currently limits its own outgoing application plaintext to 400 bytes.

## Handshake

Static and ephemeral keys are X25519 keys. `H(...)` means unkeyed BLAKE2b-256 over the exact concatenation of its arguments. Handshake authenticators are the first 16 bytes of the indicated hash.

### Client hello `0x10`

| Field | Size | Value |
| --- | --- | --- |
| version | u8 | `1` |
| intent | u8 | `1` = pair, `2` = session |
| client static public key | 32 bytes | X25519 public key |
| name length | u8 | 0–31 |
| name | variable | UTF-8 bytes |

### Server hello `0x11`

| Field | Size | Value |
| --- | --- | --- |
| version | u8 | `1` |
| status | u8 | See below |
| device static public key | 32 bytes | X25519 public key |
| name length | u8 | 0–31 |
| name | variable | UTF-8 bytes |

| Status | Meaning |
| --- | --- |
| 0 | Continue pairing |
| 1 | Continue the session |
| 2 | Pairing is not armed on the device |
| 3 | The client static key is not paired |
| 5 | Unsupported intent |

### Pairing

1. Client sends ephemeral public key `0x20` (32 bytes).
2. Device sends ephemeral public key `0x21` (32 bytes).
3. Both compute `shared = X25519(own ephemeral private key, peer ephemeral public key)`.
4. The confirmation material is `BLAKE2b-256(shared || "KMJ-PAIR-v1" || client_static || device_static || client_eph || device_eph)`.
5. The six-digit code is the first three bytes as a big-endian integer, modulo 1000000, zero-padded.
6. After the operator confirms the codes match, the client sends `0x23` and the device replies with `0x24`. Each body is 16 bytes: `H(shared || role || "KMJ-PAIR-v1" || client_static || device_static || client_eph || device_eph)[:16]`. `role` is the single ASCII byte `C` for the client or `D` for the device.
7. Either side can send `0x25` with an empty body to reject the attempt.

On success, each side stores the peer's static public key and closes the TCP connection. Starting a session requires a new connection.

### Session

1. Client sends ephemeral public key `0x30` (32 bytes).
2. Device sends ephemeral public key `0x31` (32 bytes).
3. The peers calculate these X25519 shared secrets:

   - `ss_es = X25519(client_eph_private, device_static_public)`
   - `ss_ee = X25519(client_eph_private, device_eph_public)`
   - `ss_se = X25519(client_static_private, device_eph_public)`

   The device computes the same values with its corresponding private keys.

4. Key schedule:

   - `ck0 = H("KMJ-SESS-v1" || client_static_pub || device_static_pub)`
   - `ck1 = H(ck0 || ss_es)`
   - `ck2 = H(ck1 || ss_ee)`
   - `ck3 = H(ck2 || ss_se)`
   - `c2d = H("c2d" || ck3)`, `d2c = H("d2c" || ck3)`, `mac_key = H("mac" || ck3)`

5. Client sends `0x32`; after verifying it, the device sends `0x33`. Each body is 16 bytes: `H(mac_key || role || client_static_pub || device_static_pub || client_eph_pub || device_eph_pub)[:16]`, using the same ASCII role bytes as pairing.

### Encrypted records `0x7E`

Transport uses XChaCha20-Poly1305. Each direction has an independent u64 record counter starting at 0. Its 24-byte nonce is 16 zero bytes followed by the counter in little-endian order. Additional authenticated data is empty; the outer type and length are framing only.

The record body is ciphertext of the same length as the plaintext followed by the 16-byte Poly1305 tag. Client-to-device records use `c2d`; device-to-client records use `d2c`. Increment the receive counter only after successful authentication. A failed open closes the link, and the device releases every key and mouse button.

## Application messages

The first plaintext byte inside `0x7E` is the application message type.

| Type | Direction | Body after type |
| --- | --- | --- |
| `0x01` HELLO | device → client | version u8, capabilities u8, name length u8, UTF-8 name |
| `0x02` KEY | client → device | HID usage u8, down u8 (`0` or `1`) |
| `0x03` MOUSE_MOVE | client → device | dx i16le, dy i16le |
| `0x04` MOUSE_BUTTONS | client → device | bitmask u8 (bit 0 left, bit 1 right, bit 2 middle) |
| `0x05` WHEEL | client → device | vertical i8, horizontal i8 |
| `0x06` RELEASE_ALL | client → device | empty |
| `0x07` PING | client → device | client time u32le, optionally last RTT u32le |
| `0x08` PONG | device → client | echoed client time u32le, device uptime u32le |
| `0x09` STATUS | device → client | reserved; not currently emitted |

The device currently sends HELLO with version `1` and capabilities `0x01`. The client sends PING about once a second. If the device receives nothing for three seconds, it drops the connection and releases HID.

The desktop converts OS keycodes to HID usages before `KEY`. The device splits mouse deltas into the int8 HID range.

## Locked vectors

[`vectors/handshake.txt`](vectors/handshake.txt) is produced by the firmware native test and checked by `desktop/kmj-proto`.

- Device static private key `77076d0a…2c2a` (RFC 7748 Alice), ephemeral private key 32 bytes of `0xAA`.
- Client static private key `5dab087e…e0eb` (RFC 7748 Bob), ephemeral private key 32 bytes of `0xBB`.
- Pairing code `131422`.
- Session keys `c2d = f5619350…554e`, `d2c = 0b67a8c9…317b`.
- XChaCha20-Poly1305 of plaintext `0102030405` with key 32 bytes of `0x07` and counter nonce 3 is ciphertext `2a4ccae3c3`, tag `0bfe9b01fae115b8c393a0cabc207b60`.
