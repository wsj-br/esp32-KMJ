# KMJ protocol v1

TCP port **42424**. Both sides set `TCP_NODELAY`.

Cleartext frames and encrypted records share one framing:

| Field | Size |
| --- | --- |
| type | u8 |
| body length | u16 little-endian |
| body | `length` bytes |

Maximum body length is 700.

## Handshake

Static and ephemeral keys are X25519. Hashes are unkeyed BLAKE2b-256. The MAC is the first 16 bytes of a BLAKE2b-256. Transport uses XChaCha20-Poly1305 with a 24-byte nonce: 16 zero bytes followed by the little-endian u64 counter for that direction, starting at 0.

### Client hello `0x10`

`version=1`, `intent` (1 = pair, 2 = session), client static public key (32), name length (u8, max 31), name.

### Server hello `0x11`

`version=1`, `status`, device static public key (32), name length, name.

| Status | Meaning |
| --- | --- |
| 0 | Continue pairing |
| 1 | Continue the session |
| 2 | Pairing is not armed on the device |
| 3 | The client static key is not paired |
| 5 | Unsupported intent or version |

### Pairing

1. Client sends ephemeral public key `0x20` (32 bytes).
2. Device sends ephemeral public key `0x21` (32 bytes).
3. Both compute `shared = X25519(own_eph_priv, peer_eph_pub)`.
4. The confirmation material is `BLAKE2b-256(shared || "KMJ-PAIR-v1" || client_static || device_static || client_eph || device_eph)`.
5. The six-digit code is the first three bytes as a big-endian integer, modulo 1000000, zero-padded.
6. After the operator confirms the codes match, the client sends `0x23` and the device sends `0x24`. Each body is 16 bytes: `BLAKE2b-256(shared || role || "KMJ-PAIR-v1" || client_static || device_static || client_eph || device_eph)[:16]`. `role` is `C` or `D`.
7. `0x25` rejects the attempt. On success both sides store the peer static public key and the TCP connection closes. A session is a new connection.

### Session

1. Client sends ephemeral public key `0x30`.
2. Device sends ephemeral public key `0x31`.
3. Shared secrets, using X25519 symmetry so either side's private key works:

   - `ss_es = X25519(client_eph, device_static)`
   - `ss_ee = X25519(client_eph, device_eph)`
   - `ss_se = X25519(client_static, device_eph)`

4. Key schedule:

   - `ck0 = H("KMJ-SESS-v1" || client_static_pub || device_static_pub)`
   - `ck1 = H(ck0 || ss_es)`
   - `ck2 = H(ck1 || ss_ee)`
   - `ck3 = H(ck2 || ss_se)`
   - `c2d = H("c2d" || ck3)`, `d2c = H("d2c" || ck3)`, `mac_key = H("mac" || ck3)`

5. Client sends `0x32`, device sends `0x33`. Each is 16 bytes: `H(mac_key || role || client_static || device_static || client_eph || device_eph)[:16]`.

### Encrypted records `0x7E`

Body is ciphertext of the same length as the plaintext, then a 16-byte tag. Client-to-device uses `c2d`. Device-to-client uses `d2c`. A failed open closes the link. The device releases every key and button.

## Application messages

These are the plaintext inside `0x7E`.

| Type | Body |
| --- | --- |
| `0x01` HELLO | version u8, capabilities u8, name length u8, name |
| `0x02` KEY | HID usage u8, down u8 (0 or 1) |
| `0x03` MOUSE_MOVE | dx i16le, dy i16le |
| `0x04` MOUSE_BUTTONS | bitmask u8 (bit0 left, bit1 right, bit2 middle) |
| `0x05` WHEEL | vertical i8, horizontal i8 |
| `0x06` RELEASE_ALL | empty |
| `0x07` PING | client time u32le, optional last RTT u32le |
| `0x08` PONG | echoed client time u32le, device time u32le |
| `0x09` STATUS | mode u8, usb state u8, keyboard LED bits u8 |

The client sends `PING` about once a second. If the device receives nothing for 3 seconds it drops the client and releases HID.

The desktop converts OS keycodes to HID usages before `KEY`. The device splits mouse deltas into the int8 HID range.

## Locked vectors

`proto/vectors/handshake.txt` is produced by the firmware native test and checked by `desktop/kmj-proto`.

- Device static private key `77076d0a…2c2a` (RFC 7748 Alice), ephemeral private key 32 bytes of `0xAA`.
- Client static private key `5dab087e…e0eb` (RFC 7748 Bob), ephemeral private key 32 bytes of `0xBB`.
- Pairing code `131422`.
- Session keys `c2d = f5619350…554e`, `d2c = 0b67a8c9…317b`.
- XChaCha20-Poly1305 of plaintext `0102030405` with key 32 bytes of `0x07` and counter nonce 3 is ciphertext `2a4ccae3c3`, tag `0bfe9b01fae115b8c393a0cabc207b60`.
