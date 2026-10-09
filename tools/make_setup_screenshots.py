#!/usr/bin/env python3
"""Render the README setup screenshots (docs/setup/*.png) as terminal windows.

The transcripts below are console and esptool output captured from the
device during a fresh install, with the network names and the LAN address
replaced by placeholders and the firmware version taken from platformio.ini.
Re-run after changing them:

  python3 tools/make_setup_screenshots.py [--out DIR] [--font FILE]

Needs Pillow and a monospaced TrueType font: Menlo (macOS) or DejaVu Sans
Mono (most Linux distributions) are found automatically; pass --font to use
another one.
"""
import argparse
import os
import sys

from console import REPO, fw_version
from PIL import Image, ImageDraw, ImageFont

DEFAULT_OUT = os.path.join(REPO, "docs", "setup")
FONT_CANDIDATES = (
    "/System/Library/Fonts/Menlo.ttc",                         # macOS
    "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",     # Debian/Ubuntu
    "/usr/share/fonts/dejavu-sans-mono-fonts/DejaVuSansMono.ttf",  # Fedora
    "/usr/share/fonts/TTF/DejaVuSansMono.ttf",                 # Arch
    "DejaVuSansMono.ttf",               # anywhere on Pillow's font search path
)
SIZE = 24                                   # rendered at 2x for sharp scaling

BG, BAR, TEXT, DIM = "#1b1b1d", "#2e2e31", "#d6d6d6", "#7c7c80"
ORANGE, GREEN, WHITE = "#ff8205", "#7fd66b", "#ffffff"


# Each line: list of (colour, text) runs. Helpers keep the transcripts short.
def sh(cmd):
    """Host shell command line."""
    return [(GREEN, "$ "), (WHITE, cmd)]


def esp(cmd, masked=""):
    """Device console command line. The firmware echoes a fixed `****` in
    place of the separator and password of `wifi add`, so `masked` follows
    `cmd` directly."""
    runs = [(ORANGE, "esp> "), (WHITE, cmd)]
    return runs + [(ORANGE, masked)] if masked else runs


def out(text, colour=TEXT):
    """Plain output line."""
    return [(colour, text)]


PORT = "/dev/cu.usbmodem1101"               # the macOS example port in the README
VERSION = fw_version() or "1.0.0"
BLOCK = "\u2588"                            # esptool's progress-bar glyph

SHOTS = {
    "1-flash.png": ("Terminal - flashing the firmware", [
        sh("python3 -m venv mistral-clock-venv"),
        sh("source mistral-clock-venv/bin/activate"),
        sh("python -m pip install esptool"),
        out("..."),
        sh(f"python -m esptool --chip esp32c6 --port {PORT} write-flash 0x0 mistral-clock-v{VERSION}.bin"),
        out("esptool v5.4.0"),
        out(f"Connected to ESP32-C6 on {PORT}:"),
        out("Chip type:          ESP32-C6FH8 (QFN32) (revision v0.2)"),
        out("Features:           Wi-Fi 6, BT 5 (LE), IEEE802.15.4, Single Core + LP Core, 160MHz, Embedded Flash 8MB"),
        out("USB mode:           USB-Serial/JTAG"),
        out("Stub flasher running."),
        out("Flash will be erased from 0x00000000 to 0x001c6fff..."),
        out(f"Writing at 0x001c67e0 [{BLOCK * 30}] 100.0% 930844/930844 bytes..."),
        out("Wrote 1861632 bytes (930844 compressed) at 0x00000000 in 5.7 seconds (2603.7 kbit/s)."),
        out("Hash of data verified.", GREEN),
        out("Hard resetting via RTS pin..."),
    ]),
    "2-terminal.png": ("Terminal - the clock's console", [
        sh(f"python -m serial.tools.miniterm {PORT} 115200"),
        out(f"--- Miniterm on {PORT}  115200,8,N,1 ---", DIM),
        out("--- Quit: Ctrl+] | Menu: Ctrl+T | Help: Ctrl+T followed by Ctrl+H ---", DIM),
        out(""),
        out(f"mistral-clock v{VERSION} ready - esp32c6 console"),
        out(f"--- mistral-clock v{VERSION} ---"),
        out(" wifi:  scan | add <ssid> <pass> [prio] | remove <ssid> | list | reorder <ssid> <n> | status | connect"),
        out(" theme: dark | light | mid | auto"),
        out(" wx:    weather | loc | loc auto | loc <lat> <lon> <name> | loc default | weather force <cond>|off"),
        out(" scene: scene [city|nature|window|auto]"),
        out(" saver: saver [on|off|now|timeout <s>|scenario <name>]"),
        out(" misc:  settings | fb dump | touch inject <x> <y> [hold_ms] | brightness <0-100>"),
        out(" time:  clock | clock override <epoch|off>"),
        out(" debug: version | orient | rawfill | lcdreg | reboot"),
        out(" 'help' lists all commands"),
        [(ORANGE, "esp> ")],
    ]),
    "3-wifi.png": ("Terminal - adding Wi-Fi", [
        esp("wifi scan"),
        out("scanning..."),
        out("3 network(s):"),
        out("  -60 dBm  ch  9  wpa2      HomeNet"),
        out("  -78 dBm  ch  1  wpa2      NeighbourNet"),
        out("  -87 dBm  ch  9  wpa2      HomeNet"),
        esp("wifi add HomeNet", "****"),
        out("saved HomeNet (prio 1, 1 network(s) total)"),
        esp("wifi status"),
        out("connected to HomeNet", GREEN),
        out("  ip 192.168.1.159  rssi -66 dBm"),
        out("saved networks: 1"),
        [(ORANGE, "esp> ")],
    ]),
    "4-weather.png": ("Terminal - weather and location", [
        esp("weather"),
        out("location: London (51.5074, -0.1278)"),
        out("online:   yes", GREEN),
        out("weather:  18.3C  Partly cloudy  (humidity 72%)"),
        out("cloud:    57%"),
        out("age:      0 min"),
        out("observed: EGLC 17:50, 13 km: no precipitation"),
        out("forecast:  Fri 20/11C Drizzle  Sat 15/11C Light drizzle  Sun 15/10C Overcast"),
        out("sun:      07:14 - 18:20"),
        [(ORANGE, "esp> ")],
    ]),
}


def load_font(path, size):
    """Load the first usable font: `path` if given, else FONT_CANDIDATES."""
    for cand in (path,) if path else FONT_CANDIDATES:
        try:
            return ImageFont.truetype(cand, size)
        except OSError:
            continue
    sys.exit(f"error: cannot load font {path!r}" if path else
             "error: no monospaced font found (tried Menlo and DejaVu Sans "
             "Mono); pass --font /path/to/a/monospace.ttf")


def render(name, title, lines, outdir, font_path):
    """Draw one terminal-window screenshot and save it as outdir/name."""
    font = load_font(font_path, SIZE)
    cw = font.getlength("M")
    lh = int(SIZE * 1.45)
    width = int(max(sum(len(t) for _, t in ln) for ln in lines) * cw) + 64
    width = max(width, 900)
    bar, pad = 56, 28
    height = bar + pad * 2 + lh * len(lines)
    img = Image.new("RGBA", (width + 40, height + 40), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    d.rounded_rectangle((20, 20, 20 + width, 20 + height), 18, fill=BG)
    d.rounded_rectangle((20, 20, 20 + width, 20 + bar), 18, fill=BAR)
    d.rectangle((20, 20 + bar - 18, 20 + width, 20 + bar), fill=BAR)
    for i, c in enumerate(("#ff5f57", "#febc2e", "#28c840")):
        x = 52 + i * 34
        d.ellipse((x - 10, 20 + bar // 2 - 10, x + 10, 20 + bar // 2 + 10), fill=c)
    tf = load_font(font_path, int(SIZE * 0.85))
    tw = d.textlength(title, font=tf)
    d.text((20 + (width - tw) / 2, 20 + bar / 2), title, font=tf, fill=DIM,
           anchor="lm")
    y = 20 + bar + pad
    for ln in lines:
        x = 20 + 32
        for colour, text in ln:
            d.text((x, y), text, font=font, fill=colour)
            x += d.textlength(text, font=font)
        y += lh
    os.makedirs(outdir, exist_ok=True)
    path = os.path.join(outdir, name)
    img.save(path, optimize=True)
    shown = os.path.relpath(path, REPO) if path.startswith(REPO + os.sep) else path
    print("wrote", shown, img.size)


def main():
    ap = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--out", default=DEFAULT_OUT,
                    help="output directory (default: docs/setup in the repo)")
    ap.add_argument("--font", help="monospaced TrueType/OpenType font file")
    args = ap.parse_args()
    for name, (title, lines) in SHOTS.items():
        render(name, title, lines, args.out, args.font)


if __name__ == "__main__":
    main()
