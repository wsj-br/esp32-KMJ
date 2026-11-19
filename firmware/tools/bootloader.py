#!/usr/bin/env python3
"""Reboot a kmj-debug device into the ROM bootloader (firmware upload mode).

The firmware's USB serial port reboots into download mode when the host opens
it at 1200 baud and closes it again. After this the board shows up as
"USB JTAG/serial debug unit" and stays there until it is flashed or reset.

    ~/.platformio/penv/bin/python tools/bootloader.py [port]
"""
import glob
import sys
import time

import serial

port = sys.argv[1] if len(sys.argv) > 1 else None
if port is None:
    ports = sorted(glob.glob("/dev/ttyACM*"))
    if not ports:
        sys.exit("no /dev/ttyACM* port found")
    if len(ports) > 1:
        sys.exit(
            "multiple serial ports found (%s); pass the port explicitly"
            % ", ".join(ports)
        )
    port = ports[0]

serial.Serial(port, 1200).close()
time.sleep(2.0)
print("sent 1200 baud touch to %s; board should be in upload mode" % port)
