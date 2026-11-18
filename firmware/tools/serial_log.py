#!/usr/bin/env python3
"""Capture the kmj-debug USB serial log, reconnecting when the device resets.

Run with PlatformIO's interpreter so pyserial is available:
    ~/.platformio/penv/bin/python tools/serial_log.py [out.log] [seconds]
"""
import glob
import sys
import time

import serial

out_path = sys.argv[1] if len(sys.argv) > 1 else "kmj-serial.log"
duration = float(sys.argv[2]) if len(sys.argv) > 2 else 0.0
deadline = time.time() + duration if duration > 0 else None


def stamp(out, text):
    out.write("--- %s %s\n" % (time.strftime("%H:%M:%S"), text))
    out.flush()


with open(out_path, "a", buffering=1) as out:
    stamp(out, "capture start")
    while deadline is None or time.time() < deadline:
        ports = sorted(glob.glob("/dev/ttyACM*"))
        if not ports:
            time.sleep(0.2)
            continue
        try:
            with serial.Serial(ports[0], 115200, timeout=0.5) as ser:
                stamp(out, "connected " + ports[0])
                while deadline is None or time.time() < deadline:
                    data = ser.read(512)
                    if data:
                        out.write(data.decode("utf-8", "replace"))
                        out.flush()
        except (serial.SerialException, OSError):
            stamp(out, "disconnected")
            time.sleep(0.3)
