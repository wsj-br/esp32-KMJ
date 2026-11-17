//! Find a KMJ board over Bluetooth and send it a Wi-Fi network.

use std::time::Duration;

use btleplug::api::{Central, Manager as _, Peripheral as _, ScanFilter, ValueNotification, WriteType};
use btleplug::platform::{Adapter, Manager, Peripheral};
use futures_util::StreamExt;
use kmj_client::improv::{self, error_text};
use serde::Serialize;
use tokio::time::{timeout, Instant};
use uuid::Uuid;

const SERVICE: Uuid = Uuid::from_u128(0x00467768_6228_2272_4663_277478268000);
const STATE: Uuid = Uuid::from_u128(0x00467768_6228_2272_4663_277478268001);
const ERROR: Uuid = Uuid::from_u128(0x00467768_6228_2272_4663_277478268002);
const RPC: Uuid = Uuid::from_u128(0x00467768_6228_2272_4663_277478268003);
const RESULT: Uuid = Uuid::from_u128(0x00467768_6228_2272_4663_277478268004);
const SERVICE_16: Uuid = Uuid::from_u128(0x00004677_0000_1000_8000_00805f9b34fb);

const AUTHORIZED: u8 = 0x02;
const PROVISIONING: u8 = 0x03;
const PROVISIONED: u8 = 0x04;

#[derive(Clone, Serialize)]
pub struct BleDevice {
    pub id: String,
    pub name: String,
}

#[derive(Clone, Serialize)]
pub struct WifiNetwork {
    pub ssid: String,
    pub rssi: String,
    pub auth: String,
}

struct Radio {
    _manager: Manager,
    adapter: Adapter,
}

async fn radio() -> Result<Radio, String> {
    let manager = Manager::new().await.map_err(plain)?;
    let adapter = manager.adapters().await.map_err(plain)?.into_iter().next().ok_or_else(|| {
        "This computer has no Bluetooth adapter.".to_string()
    })?;
    Ok(Radio { _manager: manager, adapter })
}

fn plain(err: impl std::fmt::Display) -> String {
    err.to_string()
}

fn is_kmj(props: &btleplug::api::PeripheralProperties) -> bool {
    props.services.iter().any(|uuid| *uuid == SERVICE)
        || props.service_data.contains_key(&SERVICE)
        || props.service_data.contains_key(&SERVICE_16)
}

pub async fn scan() -> Result<Vec<BleDevice>, String> {
    let radio = radio().await?;
    // BlueZ drops 128-bit service UUIDs when they are used as the discovery
    // filter, so the scan is open and the Improv UUID is checked afterwards.
    radio.adapter.start_scan(ScanFilter::default()).await.map_err(plain)?;
    tokio::time::sleep(Duration::from_secs(6)).await;
    let _ = radio.adapter.stop_scan().await;
    let mut found = Vec::new();
    for peripheral in radio.adapter.peripherals().await.map_err(plain)? {
        let Some(props) = peripheral.properties().await.map_err(plain)? else {
            continue;
        };
        if !is_kmj(&props) {
            continue;
        }
        let name = props.local_name.filter(|name| !name.is_empty()).unwrap_or_else(|| props.address.to_string());
        found.push(BleDevice { id: peripheral.id().to_string(), name });
    }
    found.sort_by(|a, b| a.name.cmp(&b.name));
    Ok(found)
}

async fn connect(adapter: &Adapter, id: &str) -> Result<Peripheral, String> {
    adapter.start_scan(ScanFilter::default()).await.map_err(plain)?;
    let deadline = Instant::now() + Duration::from_secs(8);
    let peripheral = loop {
        let mut matched = None;
        for peripheral in adapter.peripherals().await.map_err(plain)? {
            if peripheral.id().to_string() == id {
                matched = Some(peripheral);
                break;
            }
        }
        if let Some(peripheral) = matched {
            break peripheral;
        }
        if Instant::now() >= deadline {
            let _ = adapter.stop_scan().await;
            return Err("Scan again. The device stopped advertising.".into());
        }
        tokio::time::sleep(Duration::from_millis(250)).await;
    };
    let _ = adapter.stop_scan().await;
    peripheral.connect().await.map_err(plain)?;
    peripheral.discover_services().await.map_err(plain)?;
    Ok(peripheral)
}

fn characteristic(peripheral: &Peripheral, uuid: Uuid) -> Result<btleplug::api::Characteristic, String> {
    peripheral
        .characteristics()
        .into_iter()
        .find(|item| item.uuid == uuid)
        .ok_or_else(|| "The device does not speak Wi-Fi setup.".into())
}

async fn next_note(
    notes: &mut (impl StreamExt<Item = ValueNotification> + Unpin),
    wait: Duration,
) -> Result<Option<ValueNotification>, String> {
    match timeout(wait, notes.next()).await {
        Ok(Some(note)) => Ok(Some(note)),
        Ok(None) => Err("Bluetooth disconnected.".into()),
        Err(_) => Ok(None),
    }
}

async fn wait_until_authorized(
    peripheral: &Peripheral,
    state: &btleplug::api::Characteristic,
    notes: &mut (impl StreamExt<Item = ValueNotification> + Unpin),
) -> Result<(), String> {
    let current = peripheral.read(state).await.map_err(plain)?;
    if current.first() == Some(&AUTHORIZED) {
        return Ok(());
    }
    if current.first() == Some(&PROVISIONED) {
        return Err("This device already joined a network. Tap Wi-Fi setup again to replace it.".into());
    }
    let deadline = Instant::now() + Duration::from_secs(60);
    loop {
        let left = deadline.saturating_duration_since(Instant::now());
        if left.is_zero() {
            return Err("Tap Allow on the device, then try again.".into());
        }
        let Some(note) = next_note(notes, left).await? else {
            return Err("Tap Allow on the device, then try again.".into());
        };
        if note.uuid == ERROR {
            let code = note.value.first().copied().unwrap_or(0);
            if code != 0 {
                return Err(error_text(code).into());
            }
        }
        if note.uuid == STATE && note.value.first() == Some(&AUTHORIZED) {
            return Ok(());
        }
    }
}

async fn wait_joined(
    notes: &mut (impl StreamExt<Item = ValueNotification> + Unpin),
) -> Result<String, String> {
    let deadline = Instant::now() + Duration::from_secs(25);
    loop {
        let left = deadline.saturating_duration_since(Instant::now());
        if left.is_zero() {
            return Err("The device did not finish joining.".into());
        }
        let Some(note) = next_note(notes, left).await? else {
            return Err("The device did not finish joining.".into());
        };
        if note.uuid == ERROR {
            let code = note.value.first().copied().unwrap_or(0);
            if code != 0 {
                return Err(error_text(code).into());
            }
        }
        if note.uuid == STATE && note.value.first() == Some(&PROVISIONED) {
            return Ok("Joined.".into());
        }
        if note.uuid == STATE && note.value.first() == Some(&PROVISIONING) {
            continue;
        }
        if note.uuid == RESULT {
            if let Some((0x01, body)) = improv::parse(&note.value) {
                let url = improv::length_strings(&body).and_then(|parts| parts.into_iter().next());
                if let Some(url) = url {
                    return Ok(format!("Joined. The device is at {url}"));
                }
                return Ok("Joined.".into());
            }
        }
    }
}

pub async fn join(id: &str, ssid: &str, password: &str) -> Result<String, String> {
    let settings = improv::wifi_settings(ssid, password).map_err(str::to_string)?;
    let radio = radio().await?;
    let peripheral = connect(&radio.adapter, id).await?;
    let outcome = async {
        let state = characteristic(&peripheral, STATE)?;
        let error = characteristic(&peripheral, ERROR)?;
        let rpc = characteristic(&peripheral, RPC)?;
        let result = characteristic(&peripheral, RESULT)?;
        let mut notes = peripheral.notifications().await.map_err(plain)?;
        peripheral.subscribe(&state).await.map_err(plain)?;
        peripheral.subscribe(&error).await.map_err(plain)?;
        peripheral.subscribe(&result).await.map_err(plain)?;
        wait_until_authorized(&peripheral, &state, &mut notes).await?;
        peripheral.write(&rpc, &settings, WriteType::WithResponse).await.map_err(plain)?;
        wait_joined(&mut notes).await
    }
    .await;
    let _ = peripheral.disconnect().await;
    outcome
}

pub async fn networks(id: &str) -> Result<Vec<WifiNetwork>, String> {
    let radio = radio().await?;
    let peripheral = connect(&radio.adapter, id).await?;
    let outcome = async {
        let error = characteristic(&peripheral, ERROR)?;
        let rpc = characteristic(&peripheral, RPC)?;
        let result = characteristic(&peripheral, RESULT)?;
        let mut notes = peripheral.notifications().await.map_err(plain)?;
        peripheral.subscribe(&error).await.map_err(plain)?;
        peripheral.subscribe(&result).await.map_err(plain)?;
        let request = improv::packet(0x04, &[]).ok_or_else(|| "could not build the scan request".to_string())?;
        peripheral.write(&rpc, &request, WriteType::WithResponse).await.map_err(plain)?;
        let deadline = Instant::now() + Duration::from_secs(15);
        loop {
            let left = deadline.saturating_duration_since(Instant::now());
            if left.is_zero() {
                return Err("The device did not return a network list.".into());
            }
            let Some(note) = next_note(&mut notes, left).await? else {
                return Err("The device did not return a network list.".into());
            };
            if note.uuid == ERROR {
                let code = note.value.first().copied().unwrap_or(0);
                if code != 0 {
                    return Err(error_text(code).into());
                }
            }
            if note.uuid != RESULT {
                continue;
            }
            let Some((0x04, body)) = improv::parse(&note.value) else {
                continue;
            };
            let strings = improv::length_strings(&body).unwrap_or_default();
            let mut nets = Vec::new();
            for chunk in strings.chunks(3) {
                if chunk.len() < 3 || chunk[0].is_empty() {
                    continue;
                }
                nets.push(WifiNetwork { ssid: chunk[0].clone(), rssi: chunk[1].clone(), auth: chunk[2].clone() });
            }
            return Ok(nets);
        }
    }
    .await;
    let _ = peripheral.disconnect().await;
    outcome
}
