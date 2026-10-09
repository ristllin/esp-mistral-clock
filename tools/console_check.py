#!/usr/bin/env python3
"""Console behaviour checks for the Mistral clock board.

Verifies on the real device over USB-Serial/JTAG:
  - `help` works and lists every command group; `help wifi` shows its usage
  - CR+LF, bare LF and bare CR each count as one line ending (no menu
    reprint after a command)
  - a freshly opened terminal sees the menu (Enter, or the first keystroke
    after >= 3 s of silence); the USB-Serial/JTAG has no DTR/port-open
    signal, so "first input after silence" is the trigger (see src/console.c)

Usage: console_check.py [--port PORT]
Port: --port, else $CLOCK_PORT, else the single connected Espressif device.
Exit code 0 = all checks passed. Takes under a minute; the port is reopened
several times.
"""
import argparse
import re
import sys
import time

from console import drain, find_port, open_port

MENU = b"--- mistral-clock"
VERSION_RE = re.compile(rb"mistral-clock v\d+\.\d+\.\d+")


def rd(ser, secs):
    """Read (silently) for `secs` seconds."""
    return drain(ser, secs, echo=False)


def fresh_open(port):
    """Open the port and stay silent long enough (> 3 s) to arm the
    first-input-after-silence menu trigger."""
    ser = open_port(port)
    time.sleep(0.3)
    ser.reset_input_buffer()
    time.sleep(4.0)
    return ser


def main():
    ap = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", help="serial port (default: $CLOCK_PORT or auto-detect)")
    args = ap.parse_args()
    port = find_port(args.port)
    fails = []

    def check(name, cond, detail=""):
        print(f"[{'PASS' if cond else 'FAIL'}] {name}  {detail}")
        if not cond:
            fails.append(name)

    # --- help ------------------------------------------------------------
    ser = fresh_open(port)
    ser.write(b"help\r\n")
    out = rd(ser, 4)
    ser.write(b"help wifi\r\n")
    out += rd(ser, 3)
    ser.close()
    check("help lists all commands",
          all(k in out for k in (b"wifi", b"theme", b"weather", b"loc",
                                 b"touch", b"fb", b"clock", b"brightness",
                                 b"help")),
          f"{len(out)} bytes")
    check("help wifi shows wifi usage", b"wifi scan" in out and b"reorder" in out)

    # --- CRLF = one line ending ------------------------------------------
    ser = fresh_open(port)
    ser.write(b"wifi status\r\n")
    out1 = rd(ser, 2)
    n1 = out1.count(MENU)
    check("first input after silence shows menu once", n1 == 1, f"menus={n1}")
    check("CRLF command ran", b"esp> " in out1 and b"wifi" in out1.lower())
    # keep gaps < 3 s so the idle trigger does not re-arm between commands
    ser.write(b"wifi status\r\n")
    out2 = rd(ser, 1.5)
    n2 = out2.count(MENU)
    check("CRLF repeat command prints NO menu", n2 == 0, f"menus={n2}")
    check("CRLF repeat command ran", b"esp> " in out2)
    ser.write(b"wifi status\n")          # bare LF is also one line ending
    out2b = rd(ser, 1.5)
    check("bare LF prints NO menu", out2b.count(MENU) == 0,
          f"menus={out2b.count(MENU)}")
    ser.write(b"wifi status\r")          # bare CR is also one line ending
    out2c = rd(ser, 1.5)
    check("bare CR prints NO menu", out2c.count(MENU) == 0,
          f"menus={out2c.count(MENU)}")
    ser.close()

    # --- terminal-open: Enter after silence -------------------------------
    time.sleep(1.0)
    ser = fresh_open(port)
    ser.write(b"\n")
    out3 = rd(ser, 2)
    check("open + Enter shows menu exactly once", out3.count(MENU) == 1,
          f"menus={out3.count(MENU)}")
    ser.close()

    # --- terminal-open: CRLF after silence shows menu exactly once --------
    time.sleep(1.0)
    ser = fresh_open(port)
    ser.write(b"\r\n")
    out3b = rd(ser, 2)
    check("open + CRLF shows menu exactly once", out3b.count(MENU) == 1,
          f"menus={out3b.count(MENU)}")
    ser.close()

    # --- terminal-open: first keystroke after silence ---------------------
    time.sleep(1.0)
    ser = fresh_open(port)
    ser.write(b"version\r\n")
    out4 = rd(ser, 3)
    check("open + typing shows menu then runs command",
          out4.count(MENU) >= 1 and VERSION_RE.search(out4) is not None,
          f"menus={out4.count(MENU)}")
    ser.close()

    print(f"\n{len(fails)} failure(s)" + (": " + ", ".join(fails) if fails else ""))
    sys.exit(1 if fails else 0)


if __name__ == "__main__":
    main()
