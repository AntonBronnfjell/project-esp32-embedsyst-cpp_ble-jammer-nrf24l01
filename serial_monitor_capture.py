#!/usr/bin/env python3
"""Capture serial output from ESP32 to a log file (no TTY required)."""
import serial
import sys
import time

PORT = "/dev/cu.usbmodem1101"
BAUD = 115200
LOG = "serial_monitor.log"
DEFAULT_SECONDS = 20

def main():
    seconds = int(sys.argv[1]) if len(sys.argv) > 1 else DEFAULT_SECONDS
    try:
        ser = serial.Serial(PORT, BAUD, timeout=0.1)
    except Exception as e:
        print(f"Open failed: {e}", file=sys.stderr)
        sys.exit(1)
    with open(LOG, "a") as f:
        f.write("\n--- session ---\n")
        deadline = time.monotonic() + seconds
        try:
            while time.monotonic() < deadline:
                line = ser.readline()
                if line:
                    text = line.decode("utf-8", errors="replace").rstrip()
                    print(text)
                    f.write(text + "\n")
                    f.flush()
        except KeyboardInterrupt:
            pass
    ser.close()

if __name__ == "__main__":
    main()
