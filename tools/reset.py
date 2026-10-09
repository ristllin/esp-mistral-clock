#!/usr/bin/env python3
"""Reset the board with an RTS pulse on the USB-Serial/JTAG port (the same
auto-reset path esptool uses), e.g. to capture a fresh boot log afterwards.

Usage: reset.py [--port PORT]
Port: --port, else $CLOCK_PORT, else the single connected Espressif device.
"""
import argparse
import time

from console import open_port


def main():
    ap = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", help="serial port (default: $CLOCK_PORT or auto-detect)")
    args = ap.parse_args()

    ser = open_port(args.port, timeout=0.1)
    try:
        ser.dtr = False
        ser.rts = True
        time.sleep(0.15)
        ser.rts = False
        time.sleep(0.1)
    finally:
        ser.close()
    print("reset sent")


if __name__ == "__main__":
    main()
