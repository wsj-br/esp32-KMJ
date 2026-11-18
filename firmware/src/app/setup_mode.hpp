#pragma once

namespace kmj {

// True when this boot was requested by setup_mode_restart(). Only a software
// reset counts, so a crash, power cycle or watchdog always boots normally.
bool setup_mode_flagged();

// Consumes the request. Returns true once for the boot that should run setup.
bool setup_mode_boot();

// Reboots into Wi-Fi setup: no UI, no HID, no KM server, Bluetooth only.
void setup_mode_restart();

// Runs Wi-Fi setup and reboots into the normal firmware when it ends.
[[noreturn]] void setup_mode_run();

}  // namespace kmj
