#!/usr/bin/env python3
"""Check the auto theme across a full day via the debug clock override.

Sets `theme auto`, parses sunrise/sunset from `weather`, then steps
`clock override` through the day and checks the resolved theme:
  mid    within +-45 min of sunrise or sunset (dawn/dusk)
  light  between the dawn and dusk windows
  dark   night
Dumps a framebuffer PNG per step (fb-auto-<HHMM>.png in the output dir).
The UTC offset assumes the Europe/London time zone used by the firmware.

Usage: auto_theme_check.py [YYYY-MM-DD] [--port PORT] [--out DIR]
Port: --port, else $CLOCK_PORT, else the single connected Espressif device.
Exit code 0 = every step resolved to the expected theme.
"""
import argparse
import calendar
import os
import re
import subprocess
import sys
import time

from console import REPO, find_port

TOOLS = os.path.dirname(os.path.abspath(__file__))
CONSOLE = os.path.join(TOOLS, "console.py")
FBDUMP = os.path.join(TOOLS, "fbdump.py")

# minute-of-day steps bracketing night / dawn / day / dusk
STEPS = [("0000", 0), ("0300", 180), ("0600", 360), ("0645", 405),
         ("0730", 450), ("0800", 480), ("1200", 720), ("1730", 1050),
         ("1800", 1080), ("1900", 1140), ("1930", 1170), ("2300", 1380)]


def run(args):
    """Run a sibling tool with this interpreter; exit if it fails."""
    r = subprocess.run([sys.executable] + args, capture_output=True, text=True,
                       check=False)
    if r.returncode != 0:
        sys.exit(f"{' '.join(args)} failed ({r.returncode}): {r.stderr.strip()}")
    return r.stdout


def classify(mins, sr, ss):
    """Mirror theme_mgr.c resolve_auto() (wrap-safe weather_sun_clock)."""
    if sr < 0 or ss < 0 or sr == ss:
        sr, ss = 7 * 60, 19 * 60
    length = (ss - sr) % 1440
    rel = (mins - sr) % 1440
    if rel >= length + (1440 - length) // 2:
        rel -= 1440
    if -45 <= rel < 45 or length - 45 <= rel < length + 45:
        return "mid"
    return "light" if 0 <= rel < length else "dark"


def main():
    ap = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("day", nargs="?", default="2026-10-08",
                    help="date to simulate, YYYY-MM-DD (default 2026-10-08)")
    ap.add_argument("--port", help="serial port (default: $CLOCK_PORT or auto-detect)")
    ap.add_argument("--out", default=os.path.join(REPO, "logs", "auto-theme"),
                    help="directory for the PNG dumps (default: logs/auto-theme)")
    args = ap.parse_args()

    try:
        y, m, d = (int(x) for x in args.day.split("-"))
        e0 = calendar.timegm((y, m, d, 0, 0, 0))
    except ValueError:
        ap.error(f"bad date {args.day!r}, expected YYYY-MM-DD")
    port = ["--port", find_port(args.port)]
    os.makedirs(args.out, exist_ok=True)
    # Europe/London DST for the test dates we use (BST = UTC+1)
    offset = 1 if (3, 29) <= (m, d) <= (10, 25) else 0

    out = run([CONSOLE, *port, "cmd", "weather"])
    mm = re.search(r"sun:\s+(\d+):(\d+)\s*-\s*(\d+):(\d+)", out)
    if not mm:
        print("FAIL: could not parse sun times from `weather` output:")
        print(out)
        return 1
    sr = int(mm.group(1)) * 60 + int(mm.group(2))
    ss = int(mm.group(3)) * 60 + int(mm.group(4))
    print(f"auto-theme full-day test, date {args.day}, UTC offset +{offset}, "
          f"sunrise {sr // 60:02d}:{sr % 60:02d}, sunset {ss // 60:02d}:{ss % 60:02d}")

    ok = True
    run([CONSOLE, *port, "cmd", "theme auto"])
    for label, mins in STEPS:
        e = e0 + (mins // 60 - offset) * 3600 + (mins % 60) * 60
        out = run([CONSOLE, *port, "cmd", f"clock override {e}", "clock", "theme"])
        aline = tline = ""
        for line in out.splitlines():
            s = line.strip()
            if s.startswith("time:   2"):
                tline = s
            elif s.startswith("active:"):
                aline = s
        got = aline.split()[1] if aline else "?"
        expect = classify(mins, sr, ss)
        if got != expect:
            ok = False
        print(f"  {label} local -> active {got} (expect {expect}) "
              f"{'PASS' if got == expect else 'FAIL'}")
        print(f"    {tline}")
        run([FBDUMP, os.path.join(args.out, f"fb-auto-{label}.png"), *port,
             "--no-check"])
        time.sleep(0.5)
    run([CONSOLE, *port, "cmd", "clock override off"])
    print("RESULT:", "PASS" if ok else "FAIL")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
