# A controller hardware-error event hits assert() in NimBLE and resets the chip.
# Report it to the host instead, so Wi-Fi setup can fail on screen.

Import("env")

from pathlib import Path

OLD_HW = """        if (data[1] == BLE_HCI_EVCODE_HW_ERROR) {
            assert(0);
        }"""
NEW_HW = """        if (data[1] == BLE_HCI_EVCODE_HW_ERROR) {
            ble_hs_sched_reset(BLE_HS_ECONTROLLER);
            return 0;
        }"""
OLD_EVT = """            evbuf = ble_transport_alloc_evt(0);
            assert(evbuf != NULL);"""
NEW_EVT = """            evbuf = ble_transport_alloc_evt(0);
            if (evbuf == NULL) {
                ble_hs_sched_reset(BLE_HS_ENOMEM);
                return 0;
            }"""
OLD_RC = """        rc = ble_hci_trans_ll_evt_tx(evbuf);
        assert(rc == 0);"""
NEW_RC = """        rc = ble_hci_trans_ll_evt_tx(evbuf);
        if (rc != 0) {
            ble_hs_sched_reset(BLE_HS_ECONTROLLER);
            return 0;
        }"""


# Bluetooth start must never hang: a host that does not sync would block the
# calling task forever. Also trace each bring-up step on the debug log.
DEVICE_MARK = "kmj-patched-2"
DEVICE_STEPS = [
    ("err = esp_bt_controller_init(&bt_cfg);", "controller init"),
    ("err = esp_bt_controller_enable(ESP_BT_MODE_BLE);", "controller enable"),
    ("err = esp_nimble_hci_init();", "hci init"),
]
DEVICE_CALLS = [
    ("        nimble_port_init();\n", "port init"),
    ("        nimble_port_freertos_init(NimBLEDevice::host_task);\n", "host task start"),
]
OLD_WAIT = """    while (!m_synced) {
        ble_npl_time_delay(1);
    }
"""
NEW_WAIT = """    uint32_t kmjWaited = 0;
    while (!m_synced) {
        ble_npl_time_delay(1);
        if (++kmjWaited > 6000) {
            kmj_trace("host did not sync with the controller");
            return false;
        }
    }
    kmj_trace("host synced");
"""


def patch_device(path: Path) -> None:
    if not path.is_file():
        return
    text = path.read_text()
    if DEVICE_MARK in text:
        return
    # Arduino compiles ESP_LOGx out of library code, so trace through the app logger.
    if "// kmj-patched\n" in text:
        text = text.replace("// kmj-patched\n#include <esp_log.h>\n", "")
        text = text.replace('ESP_LOGW("kmj", ', "kmj_trace(").replace('ESP_LOGE("kmj", ', "kmj_trace(")
        text = '// %s\nextern "C" void kmj_trace(const char *fmt, ...);\n' % DEVICE_MARK + text
        path.write_text(text)
        print("repatched", path)
        return
    for call, label in DEVICE_STEPS:
        text = text.replace(
            call, 'kmj_trace("ble %s"); %s kmj_trace("ble %s -> %%d", (int)err);' % (label, call, label)
        )
    for call, label in DEVICE_CALLS:
        text = text.replace(call, '        kmj_trace("ble %s");\n%s' % (label, call))
    text = text.replace(OLD_WAIT, NEW_WAIT)
    text = '// %s\nextern "C" void kmj_trace(const char *fmt, ...);\n' % DEVICE_MARK + text
    path.write_text(text)
    print("patched", path)


# Trace the HCI conversation with the controller so a host that never syncs can be diagnosed.
HCI_MARK = "// kmj-trace"
HCI_EDITS = [
    (
        "    *cmd = BLE_HCI_UART_H4_CMD;\n    len = BLE_HCI_CMD_HDR_LEN + cmd[3] + 1;\n",
        "    *cmd = BLE_HCI_UART_H4_CMD;\n    len = BLE_HCI_CMD_HDR_LEN + cmd[3] + 1;\n"
        '    kmj_trace("hci cmd %02x%02x len %d", cmd[2], cmd[1], (int)len);\n',
    ),
    (
        "    } else {\n        rc = BLE_HS_ETIMEOUT_HCI;\n    }\n\n    ble_transport_free(cmd);",
        '    } else {\n        kmj_trace("hci cmd send timed out");\n        rc = BLE_HS_ETIMEOUT_HCI;\n    }\n\n    ble_transport_free(cmd);',
    ),
    (
        "    if(!ble_hs_enabled_state) {\n",
        '    if (len > 2) {\n        kmj_trace("hci rx type %02x code %02x len %d enabled %d", data[0], data[1], (int)len, (int)ble_hs_enabled_state);\n    }\n'
        "    if(!ble_hs_enabled_state) {\n",
    ),
]
DEVICE_TRACE = [
    ("void NimBLEDevice::onReset(int reason) {\n", 'void NimBLEDevice::onReset(int reason) {\n    kmj_trace("host reset reason %d", reason);\n'),
    ("void NimBLEDevice::onSync(void) {\n", 'void NimBLEDevice::onSync(void) {\n    kmj_trace("host sync callback");\n'),
    ("void NimBLEDevice::host_task(void* param) {\n", 'void NimBLEDevice::host_task(void* param) {\n    kmj_trace("host task running");\n'),
]


def patch_trace(path: Path, edits, header: str) -> None:
    if not path.is_file():
        return
    text = path.read_text()
    if HCI_MARK in text:
        return
    for old, new in edits:
        text = text.replace(old, new)
    path.write_text(HCI_MARK + "\n" + header + "\n" + text)
    print("traced", path)


def patch(path: Path) -> None:
    if not path.is_file():
        return
    text = path.read_text()
    updated = text.replace(OLD_HW, NEW_HW).replace(OLD_EVT, NEW_EVT).replace(OLD_RC, NEW_RC)
    if updated != text:
        path.write_text(updated)
        print("patched", path)


root = Path(env["PROJECT_LIBDEPS_DIR"])
for path in root.glob("*/NimBLE-Arduino/src/nimble/esp_port/esp-hci/src/esp_nimble_hci.c"):
    patch(path)

for path in root.glob("*/NimBLE-Arduino/src/NimBLEDevice.cpp"):
    patch_device(path)

TRACE_DECL = 'extern "C" void kmj_trace(const char *fmt, ...);'
for path in root.glob("*/NimBLE-Arduino/src/nimble/esp_port/esp-hci/src/esp_nimble_hci.c"):
    patch_trace(path, HCI_EDITS, "void kmj_trace(const char *fmt, ...);")
for path in root.glob("*/NimBLE-Arduino/src/NimBLEDevice.cpp"):
    patch_trace(path, DEVICE_TRACE, "")
