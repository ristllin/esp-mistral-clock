#!/usr/bin/env python3
"""Serial console helper for the Mistral clock board.

Usage:
  console.py [--port PORT] boot                # print the boot log until the prompt
  console.py [--port PORT] cmd "help" "info"   # run console commands, print all output
  console.py [--port PORT] raw "wifi scan" 30  # run a command and read for N seconds
  console.py [--port PORT] monitor 10          # raw monitor for N seconds

The board's console is the native USB-Serial/JTAG peripheral at 115200 baud.
Port selection (shared by every tool in this directory): --port, else the
CLOCK_PORT environment variable, else auto-detect the single connected
Espressif USB device (VID 0x303A).

This module also provides the helpers the other tools import:
find_port(), open_port(), read_until() and fw_version().
"""
import argparse
import os
import re
import sys
import time

BAUD = 115200
PROMPT = b"esp> "
ESPRESSIF_VID = 0x303A
REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def _import_serial():
    """Import pyserial lazily, so tools that only need fw_version() (the
    screenshot renderer) run without it."""
    try:
        import serial
        import serial.tools.list_ports
    except ImportError:
        sys.exit("error: pyserial is required: python3 -m pip install pyserial")
    return serial


def find_port(port=None):
    """Resolve the serial port: explicit `port`, else $CLOCK_PORT, else the
    single connected Espressif USB device. Exits with a clear message when
    auto-detection finds zero or several candidates."""
    if port:
        return port
    env = os.environ.get("CLOCK_PORT", "").strip()
    if env:
        return env
    list_ports = _import_serial().tools.list_ports
    matches = [p.device for p in list_ports.comports() if p.vid == ESPRESSIF_VID]
    if len(matches) == 1:
        return matches[0]
    if not matches:
        sys.exit("error: no Espressif USB device (VID 0x303A) found; plug in "
                 "the clock or pass --port / set CLOCK_PORT")
    sys.exit("error: several Espressif USB devices found ("
             + ", ".join(sorted(matches))
             + "); pick one with --port or CLOCK_PORT")


def open_port(port=None, timeout=0.05):
    """Open the console port (resolved with find_port())."""
    return _import_serial().Serial(find_port(port), BAUD, timeout=timeout)


def fw_version():
    """CLOCK_FW_VERSION from platformio.ini, or None if it cannot be read."""
    try:
        with open(os.path.join(REPO, "platformio.ini"), encoding="utf-8") as f:
            text = f.read()
    except OSError:
        return None
    m = re.search(r'CLOCK_FW_VERSION=\\?"([^"\\]+)\\?"', text)
    return m.group(1) if m else None


def redact(cmd):
    """Hide the password of `wifi add <ssid> <pass> [prio]` in messages."""
    parts = cmd.split()
    if len(parts) >= 4 and parts[:2] == ["wifi", "add"]:
        parts[3] = "<redacted>"
        return " ".join(parts)
    return cmd


def _echo(data):
    sys.stdout.buffer.write(data)
    sys.stdout.buffer.flush()


def drain(ser, seconds, echo=True):
    """Read whatever arrives for `seconds`; return it (and print it if `echo`)."""
    end = time.time() + seconds
    out = b""
    while time.time() < end:
        out += ser.read(ser.in_waiting or 1)
    if echo:
        _echo(out)
    return out


def read_until(ser, needle, timeout, echo=False):
    """Read until `needle` appears (plus a short grace period for trailing
    output) or `timeout` expires. Return (data, found)."""
    buf = b""
    end = time.time() + timeout
    while time.time() < end:
        chunk = ser.read(ser.in_waiting or 1)
        if chunk:
            buf += chunk
            if echo:
                _echo(chunk)
        if needle and needle in buf:
            grace = time.time() + 0.15
            while time.time() < grace:
                extra = ser.read(ser.in_waiting or 1)
                if extra:
                    buf += extra
                    if echo:
                        _echo(extra)
                    grace = time.time() + 0.15
            return buf, True
    return buf, False


def main():
    ap = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", help="serial port (default: $CLOCK_PORT or auto-detect)")
    ap.add_argument("--boot-wait", type=float, default=3.0,
                    help="seconds to read pending output after opening (default 3)")
    sub = ap.add_subparsers(dest="mode", required=True)
    sub.add_parser("boot", help="print the boot log until the prompt")
    p_cmd = sub.add_parser("cmd", help="run console commands, wait for the prompt")
    p_cmd.add_argument("commands", nargs="+")
    p_cmd.add_argument("--timeout", type=float, default=15.0,
                       help="seconds to wait for the prompt per command (default 15)")
    p_raw = sub.add_parser("raw", help="run one command and read for N seconds")
    p_raw.add_argument("command")
    p_raw.add_argument("seconds", type=float)
    p_mon = sub.add_parser("monitor", help="print raw output for N seconds")
    p_mon.add_argument("seconds", type=float)
    args = ap.parse_args()

    ser = open_port(args.port)
    try:
        time.sleep(0.2)
        ser.reset_input_buffer()
        if args.mode != "monitor":
            drain(ser, args.boot_wait)

        if args.mode == "boot":
            read_until(ser, PROMPT, 5, echo=True)
        elif args.mode == "monitor":
            drain(ser, args.seconds)
        elif args.mode == "raw":
            ser.write(args.command.encode() + b"\n")
            drain(ser, args.seconds)
        elif args.mode == "cmd":
            for cmd in args.commands:
                # wake the REPL and wait for a fresh prompt first
                ser.write(b"\n")
                read_until(ser, PROMPT, 5, echo=True)
                ser.write(cmd.encode() + b"\n")
                _, ok = read_until(ser, PROMPT, args.timeout, echo=True)
                if not ok:
                    print(f"\n[console.py] WARNING: no prompt after {redact(cmd)!r}",
                          file=sys.stderr)
    finally:
        ser.close()


if __name__ == "__main__":
    main()
