#!/usr/bin/env python3
"""Build docs/demo.gif for the README from the host scene simulator.

Every frame is rendered by the real firmware scene code (tools/sim/sim.c).
Home-screen segments composite the animated scene under real widgets captured
from the device with `fb dump raw` (tools/sim/chrome/home-<theme>.rgb565).
"""
import os

import render  # tools/sim/render.py (the script's directory is on sys.path)
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))

KEYS = {"dark": 0x1081, "light": 0xF77C, "mid": 0x3964}   # theme bg in RGB565

# (frames, screen, theme, phase, wx, layer, scenario, hour, temp_c)
SEGMENTS = [
    (22, "home", "dark", "night", "drizzle", "city", "auto", 23, 9),
    (24, "saver", "dark", "night", "clear", "city", "loaf", 23, 12),
    (24, "saver", "dark", "night", "rain", "city", "sleep", 23, 9),
    (26, "saver", "mid", "sunset", "clear", "nature", "chase", 18, 12),
    (20, "saver", "light", "day", "clear", "city", "loaf", 11, 22),
    (22, "saver", "dark", "day", "snow", "nature", "mchase", 10, -2),
    (20, "home", "light", "day", "clear", "window", "auto", 15, 22),
]
FADE = 3          # crossfade frames between segments
SCALE = 2         # 320x172 -> 640x344


def segment(n, screen, theme, phase, wx, layer, scen, hour, temp):
    args = [screen, theme, phase, wx, layer, scen, str(hour), str(temp), "0"]
    if screen == "home":
        args.append(os.path.join(HERE, "chrome", f"home-{theme}.rgb565"))
        os.environ["SIM_KEY"] = str(KEYS[theme])
    return render.frames(n, args)


def main():
    out = os.path.join(ROOT, "docs", "demo.gif")
    os.makedirs(os.path.dirname(out), exist_ok=True)
    frames = []
    for seg in SEGMENTS:
        imgs = segment(*seg)
        if frames:
            last = frames[-1]
            for k in range(1, FADE + 1):
                frames.append(Image.blend(last, imgs[0], k / (FADE + 1)))
        frames.extend(imgs)
    big = [f.resize((f.width * SCALE, f.height * SCALE), Image.NEAREST)
           for f in frames]
    # The scene uses few colours: build one exact global palette from every
    # frame (crisp pixel art, no colour shifts); fall back to per-frame
    # adaptive palettes if the union ever exceeds 256 colours.
    colours = sorted({c for f in frames
                      for _, c in f.getcolors(maxcolors=1 << 16)})
    if len(colours) <= 256:
        pal = Image.new("P", (1, 1))
        flat = [v for c in colours for v in c]
        pal.putpalette(flat + [0] * (768 - len(flat)))
        q = [im.quantize(palette=pal, dither=Image.Dither.NONE) for im in big]
    else:
        q = [im.quantize(colors=256, dither=Image.Dither.NONE) for im in big]
    print(f"{len(colours)} distinct colours")
    q[0].save(out, save_all=True, append_images=q[1:], duration=100, loop=0,
              optimize=True, disposal=1)
    print(f"wrote {out}: {len(q)} frames, {os.path.getsize(out) // 1024} KB")


if __name__ == "__main__":
    main()
