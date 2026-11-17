//! Improv Wi-Fi RPC packets. The board accepts the same layout over BLE.

pub fn packet(command: u8, data: &[u8]) -> Option<Vec<u8>> {
    if data.len() > 255 {
        return None;
    }
    let mut out = Vec::with_capacity(data.len() + 3);
    out.push(command);
    out.push(data.len() as u8);
    out.extend_from_slice(data);
    let sum: u16 = out.iter().map(|byte| u16::from(*byte)).sum();
    out.push((0u16.wrapping_sub(sum) & 0xff) as u8);
    Some(out)
}

pub fn parse(bytes: &[u8]) -> Option<(u8, Vec<u8>)> {
    if bytes.len() < 3 {
        return None;
    }
    let sum: u16 = bytes.iter().map(|byte| u16::from(*byte)).sum();
    if sum & 0xff != 0 {
        return None;
    }
    let data_len = bytes[1] as usize;
    if bytes.len() != data_len + 3 {
        return None;
    }
    Some((bytes[0], bytes[2..2 + data_len].to_vec()))
}

pub fn length_strings(data: &[u8]) -> Option<Vec<String>> {
    let mut out = Vec::new();
    let mut index = 0;
    while index < data.len() {
        let len = data[index] as usize;
        index += 1;
        if index + len > data.len() {
            return None;
        }
        let text = std::str::from_utf8(&data[index..index + len]).ok()?;
        out.push(text.to_string());
        index += len;
    }
    Some(out)
}

pub fn wifi_settings(ssid: &str, password: &str) -> Result<Vec<u8>, &'static str> {
    if ssid.is_empty() || ssid.len() > 32 {
        return Err("the network name must be 1 to 32 characters");
    }
    if password.len() > 64 {
        return Err("the password must be at most 64 characters");
    }
    let mut data = Vec::with_capacity(ssid.len() + password.len() + 2);
    data.push(ssid.len() as u8);
    data.extend_from_slice(ssid.as_bytes());
    data.push(password.len() as u8);
    data.extend_from_slice(password.as_bytes());
    packet(0x01, &data).ok_or("the Wi-Fi settings did not fit in one packet")
}

pub fn error_text(code: u8) -> &'static str {
    match code {
        0x00 => "no error",
        0x01 => "the device rejected the packet",
        0x02 => "the device does not know that command",
        0x03 => "the device could not join that network",
        0x04 => "tap Allow on the device, then try again",
        _ => "the device reported an unknown error",
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn wifi_packet_matches_the_board() {
        let packet = wifi_settings("lab", "secret").unwrap();
        let (command, data) = parse(&packet).unwrap();
        assert_eq!(command, 0x01);
        assert_eq!(data, [3, b'l', b'a', b'b', 6, b's', b'e', b'c', b'r', b'e', b't']);
        let mut broken = packet.clone();
        let last = broken.len() - 1;
        broken[last] ^= 0xff;
        assert!(parse(&broken).is_none());
    }

    #[test]
    fn result_strings_round_trip() {
        let mut data = Vec::new();
        for text in ["KMJ", "0.1.0"] {
            data.push(text.len() as u8);
            data.extend_from_slice(text.as_bytes());
        }
        let packet = packet(0x03, &data).unwrap();
        let (command, body) = parse(&packet).unwrap();
        assert_eq!(command, 0x03);
        assert_eq!(length_strings(&body).unwrap(), ["KMJ", "0.1.0"]);
    }
}
