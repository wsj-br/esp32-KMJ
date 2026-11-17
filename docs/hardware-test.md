# Hardware checks

These need a LilyGO T-Display-S3 Pro and a host. CI does not flash a board.

1. Build `firmware` with `pio run -e kmj` and flash it. On Windows, Linux, and macOS, the board enumerates as a composite HID keyboard and mouse named `KMJ Composite`, VID `413C`, PID `250E`.
2. From Configuration, tap Wi-Fi setup and Allow. In the desktop app, find the device and send a network. The status strip shows the SSID, RSSI, and IP. Forget Wi-Fi and confirm the strip returns to `Wi-Fi off`.
3. Pair the desktop app. The six-digit codes match. Cancelling on either side does not store a peer. A second computer that has not paired is rejected.
4. Connect and capture. Type, move, click, and scroll on the desktop. The target computer receives them. The KM screen counts keys and moves and shows the last key.
5. Press Ctrl+Alt+Esc. The desktop overlay clears, the target receives no further keys, and no modifier stays down.
6. Kill the desktop process while a key is held. Within 3 seconds the firmware releases every key and button.
7. Unplug Wi-Fi or walk the desktop off the LAN with a key held. The same 3 second failsafe releases HID.
8. Leave KM for the jiggler. The pointer eases out and back on one axis and returns to its start. Nudge draws the 40 px loop. USB disconnect stops reports and reconnect resumes them. The 8 hour limit stops unattended jiggling.
9. Home key: from KM, Jiggler, or Configuration it returns home and stops that function. On home it blanks the backlight. A press while blank wakes the same screen. A long press blanks the backlight without stopping the function.
10. Soak: leave a connected KM session idle for an hour with Wi-Fi setup not running. The link stays up and the heap reading on Configuration does not collapse.

Latency is the RTT the desktop puts in `PING` and the device shows on the KM screen. A usable LAN session is well under 30 ms. Full-screen LVGL transitions are limited by the 40 MHz SPI panel. Partial updates of the status line are the path that stays responsive.

Record the host OS, the RTT, and whether steps 5 to 7 left any key down.
