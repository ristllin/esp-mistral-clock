#!/usr/bin/env python3
"""Capture a framebuffer dump from the clock board over serial and save a PNG.

Usage:
  fbdump.py [output.png] [--port PORT] [--scale N] [--timeout S]
            [--retries N] [--no-check]

The output defaults to logs/fb.png (the directory is created if missing).
Port: --port, else $CLOCK_PORT, else the single connected Espressif device.

Sends `fb dump`, reads:
  FB START <w> <h>
  FB <y> <fletcher16> <base64 of w*2 bytes>   (one line per row)
  FB DONE <fletcher16 of the whole buffer>
Validates every row checksum (retries the whole dump on corruption), decodes
RGB565 and writes a PNG. By default it also validates known pixels of the
calibration screen (bars, header/footer, border) and auto-detects the byte
order from them.
"""
import argparse
import base64
import os
import sys
import time

from console import REPO, open_port
from PIL import Image

DEFAULT_OUT = os.path.join(REPO, "logs", "fb.png")

# (x, y, expected RGB565, name) for the calibration image.
# Points are chosen away from label text and ON the 1px border ring.
CAL_CHECKS = [
    (32, 64, 0xF800, "R bar"),
    (96, 64, 0x07E0, "G bar"),
    (160, 64, 0x001F, "B bar"),
    (224, 64, 0xFFFF, "W bar"),
    (288, 64, 0x0000, "K bar"),
    (200, 12, 0x18C3, "header bg 0x1A1A1A"),
    (200, 160, 0x18C3, "footer bg 0x1A1A1A"),
    (0, 86, 0x8410, "border left 0x808080"),
    (319, 86, 0x8410, "border right"),
    (160, 0, 0x8410, "border top"),
    (160, 171, 0x8410, "border bottom"),
]


def fletcher16(data):
    """Fletcher-16 checksum, as computed by the firmware's `fb dump`."""
    s1 = s2 = 0
    for b in data:
        s1 = (s1 + b) % 255
        s2 = (s2 + s1) % 255
    return (s2 << 8) | s1


def read_dump(ser, timeout):
    """Send `fb dump` and return (w, h, fb_bytes); raise on corruption/timeout."""
    ser.write(b"\n")
    time.sleep(0.2)
    ser.reset_input_buffer()
    ser.write(b"fb dump\n")
    buf = b""
    start = None
    rows = {}
    whole = None
    end = time.time() + timeout
    while time.time() < end:
        chunk = ser.read(ser.in_waiting or 1)
        if chunk:
            buf += chunk
        while b"\n" in buf:
            line, buf = buf.split(b"\n", 1)
            line = line.strip()
            if line.startswith(b"FB START"):
                start = tuple(int(p) for p in line.split()[2:4])
            elif line.startswith(b"FB DONE"):
                whole = int(line.split()[2])
            elif line.startswith(b"FB "):
                parts = line.split(b" ")
                y = int(parts[1])
                ck = int(parts[2])
                data = base64.b64decode(parts[3])
                if start is None:
                    raise ValueError("row before FB START")
                if len(data) != start[0] * 2:
                    raise ValueError(f"row {y}: bad length {len(data)}")
                if fletcher16(data) != ck:
                    raise ValueError(f"row {y}: checksum mismatch")
                rows[y] = data
        if whole is not None and start is not None and len(rows) >= start[1]:
            w, h = start
            fb = b"".join(rows[y] for y in range(h))
            if fletcher16(fb) != whole:
                raise ValueError("whole-buffer checksum mismatch")
            return w, h, fb
    raise TimeoutError(f"dump incomplete: {len(rows)} rows, whole={whole}")


def decode(fb, w, h, big_endian):
    """Decode an RGB565 buffer into an RGB PIL image."""
    img = Image.new("RGB", (w, h))
    px = img.load()
    for y in range(h):
        base = y * w * 2
        for x in range(w):
            if big_endian:
                v = (fb[base + x * 2] << 8) | fb[base + x * 2 + 1]
            else:
                v = fb[base + x * 2] | (fb[base + x * 2 + 1] << 8)
            px[x, y] = (((v >> 11) & 0x1F) << 3, ((v >> 5) & 0x3F) << 2, (v & 0x1F) << 3)
    return img


def pixel565(fb, w, x, y, big_endian):
    off = y * w * 2 + x * 2
    if big_endian:
        return (fb[off] << 8) | fb[off + 1]
    return fb[off] | (fb[off + 1] << 8)


def run_checks(fb, w, h, big_endian):
    """Check known calibration-screen pixels; return True if all pass."""
    ok = True
    for x, y, want, name in CAL_CHECKS:
        got = pixel565(fb, w, x, y, big_endian)
        status = "PASS" if got == want else "FAIL"
        if got != want:
            ok = False
        print(f"  [{status}] {name} @({x},{y}): got 0x{got:04X}, want 0x{want:04X}")
    # ramp: left end dark, right end bright
    vl = pixel565(fb, w, 8, 125, big_endian)
    vr = pixel565(fb, w, w - 9, 125, big_endian)
    ramp_ok = vl < 0x1000 and vr > 0xE000
    print(f"  [{'PASS' if ramp_ok else 'FAIL'}] ramp dark->bright: left 0x{vl:04X}, right 0x{vr:04X}")
    return ok and ramp_ok


def main():
    ap = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("output", nargs="?", default=DEFAULT_OUT,
                    help="PNG path (default: logs/fb.png in the repo)")
    ap.add_argument("--port", help="serial port (default: $CLOCK_PORT or auto-detect)")
    ap.add_argument("--scale", type=int, default=1, help="integer upscale factor")
    ap.add_argument("--timeout", type=float, default=60.0,
                    help="seconds per dump attempt (default 60)")
    ap.add_argument("--retries", type=int, default=5,
                    help="dump attempts before giving up (default 5)")
    ap.add_argument("--no-check", action="store_true",
                    help="skip the calibration-screen checks (any screen content)")
    args = ap.parse_args()

    ser = open_port(args.port, timeout=0.1)
    time.sleep(0.2)
    ser.reset_input_buffer()

    w = h = None
    fb = None
    for attempt in range(1, args.retries + 1):
        try:
            w, h, fb = read_dump(ser, args.timeout)
            break
        except (ValueError, TimeoutError) as e:
            print(f"fbdump.py: attempt {attempt} failed: {e}", file=sys.stderr)
            time.sleep(0.5)
    ser.close()
    if fb is None:
        print("fbdump.py: ERROR: could not get a clean dump", file=sys.stderr)
        sys.exit(1)

    if not args.no_check:
        be_ok = run_checks(fb, w, h, big_endian=True)
        le_ok = run_checks(fb, w, h, big_endian=False)
        # The framebuffer holds native LVGL RGB565 (uint16 little-endian in
        # memory), so little-endian decode is correct; big-endian only wins
        # when LE clearly fails and BE clearly passes (byte-swapped dump).
        big_endian = be_ok and not le_ok
        if not be_ok and not le_ok:
            print("fbdump.py: WARNING: neither byte order passes all checks "
                  "(mixed or unexpected buffer content)", file=sys.stderr)
        print(f"byte order: {'big-endian (MSB first)' if big_endian else 'little-endian (native)'}"
              f" (calibration checks {'PASS' if (be_ok or le_ok) else 'FAIL'})")
    else:
        big_endian = False

    img = decode(fb, w, h, big_endian)
    if args.scale > 1:
        img = img.resize((w * args.scale, h * args.scale), Image.NEAREST)
    out_dir = os.path.dirname(os.path.abspath(args.output))
    os.makedirs(out_dir, exist_ok=True)
    img.save(args.output)
    print(f"wrote {args.output} ({w}x{h})")


if __name__ == "__main__":
    main()
