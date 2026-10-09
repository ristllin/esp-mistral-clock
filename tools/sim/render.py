#!/usr/bin/env python3
"""Build and drive the host scene simulator (tools/sim/sim.c).

  render.py png  OUT.png  [sim args...]          first frame as PNG (2x upscale)
  render.py gif  OUT.gif  FRAMES [sim args...]   animated GIF, 100 ms per frame
  render.py sheet OUT.png FRAMES [sim args...]   every frame side by side

sim args (after FRAMES for gif/sheet):
  screen theme phase wx layer scenario [hour] [temp_c] [neutral] [chrome.rgb565]
Example:
  render.py gif cat.gif 60 saver dark night drizzle city sleep 23 9
Requires Pillow. The binary is rebuilt automatically when sources change.
"""
import os
import subprocess
import sys
import tempfile

from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
W, H = 320, 172
BIN = os.path.join(tempfile.gettempdir(), "mistral-clock-sim")
SOURCES = ["tools/sim/sim.c", "src/scene.c", "src/scene_chaton.c",
           "src/assets/chaton.h", "src/assets/scene_art.h", "src/scene.h"]


def build():
    newest = max(os.path.getmtime(os.path.join(ROOT, s)) for s in SOURCES)
    if os.path.exists(BIN) and os.path.getmtime(BIN) >= newest:
        return
    cmd = ["cc", "-std=c11", "-O1", "-w", "-I" + os.path.join(HERE, "stubs"),
           "-I" + os.path.join(ROOT, "src"),
           "-I" + os.path.join(ROOT, "src", "assets"),
           "-I" + os.path.join(ROOT, "src", "fonts"),
           "-o", BIN, os.path.join(HERE, "sim.c"), "-lm"]
    subprocess.run(cmd, check=True)


def frames(n, args):
    build()
    with tempfile.NamedTemporaryFile(suffix=".rgb565", delete=False) as t:
        raw = t.name
    subprocess.run([BIN, raw, str(n)] + list(args), check=True)
    with open(raw, "rb") as f:
        data = f.read()
    os.unlink(raw)
    out = []
    step = W * H * 2
    for i in range(n):
        chunk = data[i * step:(i + 1) * step]
        img = Image.new("RGB", (W, H))
        px = []
        for j in range(0, step, 2):
            v = chunk[j] | (chunk[j + 1] << 8)
            r, g, b = (v >> 11) & 31, (v >> 5) & 63, v & 31
            px.append((r * 255 // 31, g * 255 // 63, b * 255 // 31))
        img.putdata(px)
        out.append(img)
    return out


def main():
    mode, path = sys.argv[1], sys.argv[2]
    if mode == "png":
        img = frames(1, sys.argv[3:])[0]
        img.resize((W * 2, H * 2), Image.NEAREST).save(path)
    elif mode in ("gif", "sheet"):
        n = int(sys.argv[3])
        imgs = frames(n, sys.argv[4:])
        if mode == "gif":
            imgs = [i.resize((W * 2, H * 2), Image.NEAREST) for i in imgs]
            imgs[0].save(path, save_all=True, append_images=imgs[1:],
                         duration=100, loop=0, optimize=True)
        else:
            sheet = Image.new("RGB", (W * min(n, 4), H * ((n + 3) // 4)))
            for k, im in enumerate(imgs):
                sheet.paste(im, ((k % 4) * W, (k // 4) * H))
            sheet.save(path)
    else:
        sys.exit(__doc__)


if __name__ == "__main__":
    main()
