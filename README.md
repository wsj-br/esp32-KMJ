# KMJ

<img src="brand/mark.svg" alt="KMJ" width="152" height="94">

KMJ turns a [LilyGO T-Display-S3 Pro](https://wiki.lilygo.cc/products/t-display-series/t-display-s3-pro/) or a [Waveshare ESP32-S3-Touch-LCD-1.85B](https://www.waveshare.com/esp32-s3-touch-lcd-1.85b.htm) into a network-controlled USB keyboard and mouse.

Run the KMJ desktop app on the **source** computer and plug the board into the **target** computer. The app captures input on the source, sends it over the local network, and the board presents it to the target as a standard USB keyboard and mouse. KMJ carries input only; it does not provide video or remote access over the internet.

## Requirements

- A supported board running the KMJ firmware: the LilyGO T-Display-S3 Pro or the Waveshare ESP32-S3-Touch-LCD-1.85B.
- A source computer running the desktop app. Linux capture is complete; see the [platform status](dev/DEVEL.md#capture) for Windows and macOS.
- A target computer with USB HID support.
- A local network that lets the source reach the board. The board uses 2.4 GHz Wi-Fi.

## Quick start

1. [Build and flash the firmware](dev/DEVEL.md#firmware), then connect the board's USB port to the target computer.
2. [Build and run the desktop app](dev/DEVEL.md#desktop-client) on the source computer.
3. On the board, open **Configuration** → **Wi-Fi setup** → **Allow**. In the app, select **Find device**, choose the board, enter the Wi-Fi credentials, and send them. Bluetooth turns off after setup.
4. Open **KM Emulator** on the board and select **Pair**. Start pairing in the app, verify that both six-digit codes match, then confirm on the board.
5. Connect in the app and select **Capture**. Press **Ctrl+Alt+Esc** to release the source keyboard and pointer. This default release chord is handled locally and is never sent to the target; it can be changed in the app.

Pairing stores each side's identity key, so it normally needs to be completed only once. Session traffic is encrypted. The board can remember up to four paired source computers.

## Device functions

- **KM Emulator** accepts an encrypted session from a paired desktop and injects its input into the USB target.
- **Mouse Jiggler** generates eased pointer movement, a manual nudge, and an eight-hour unattended limit.
- **Configuration** manages Wi-Fi, USB output, display settings, and system status.

The red-circle home key wakes the display, returns to the home screen, or turns the backlight off. A long press turns off the backlight without stopping the active function. On the Waveshare 1.85B, which has no touch home key, the physical **BOOT** button plays that role.

## Host permissions

- **Linux/Wayland:** capture requires read/write access to `/dev/input/event*`. See the [Linux instructions](dev/DEVEL.md#linux).
- **Windows:** no additional capture permission is normally required, but the backend still needs release validation.
- **macOS:** grant Accessibility and Input Monitoring to the KMJ binary, then quit and reopen the app. A debug binary and an installed app need separate grants. See the [macOS notes](dev/DEVEL.md#macos).
- **Linux/X11:** no additional capture permission is normally required.

Build instructions, platform details, hardware checks, and release steps are in the [development guide](dev/DEVEL.md). The wire format is documented in the [protocol reference](dev/proto/PROTOCOL.md).
