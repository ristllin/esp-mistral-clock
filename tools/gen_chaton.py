#!/usr/bin/env python3
"""Generate src/assets/chaton.h: palette-indexed sprites for the Mistral pixel
cat (screensaver character, src/scene_chaton.c, and the little cat on the
window sill in src/scene.c).

The cat is based on a 13x11 pixel reference: a sitting cat, white, dark
eyes and nose, a red-orange collar with a yellow bell and an upright tail.
Every sprite starts from that native grid; the screensaver version is scaled
3x (6x on screen, the scene is half resolution) and gets a 1-px dark outline
at the scaled resolution so it reads on light skies. Faces (eyes, nose) are
drawn at the scaled resolution so they can blink, look around and sleep.

Native char map:  '.' transparent  'W' white  'k' dark  'C' collar  'Y' bell
Scaled char map:  '.' transparent  'w' white  'k' dark  'c' collar  'y' bell
                  'a' accent (orange)  'g' glow (yellow)
Sprite maps hold P_* palette slots from src/scene.h; 0 = transparent.

Usage:
  python3 tools/gen_chaton.py                 write the header
  python3 tools/gen_chaton.py --preview PNG   also write a sprite sheet
                                              (needs Pillow)
"""
import argparse
import os

# P_* slots (src/scene.h): P_BLD2 5, P_GLOW 6, P_WHITE 12, P_DARK 13,
# P_COLLAR 14, P_ACC 15
CH = {'.': 0, 'W': 12, 'w': 12, 'k': 13, 'K': 13, 'C': 14, 'c': 14,
      'Y': 5, 'y': 5, 'a': 15, 'g': 6}
SCALE = 3

GRIDS = {}


def grid(name, rows):
    w = len(rows[0])
    for r in rows:
        assert len(r) == w, f"{name}: row width {len(r)} != {w}"
    GRIDS[name] = (w, rows)


# ---- native cat (the reference, 14 cols to leave room for the tail) ----
SIT = [
    "..W...W.......",
    "..WW.WW.......",
    "..WWWWW.......",
    "..WkWkW.......",
    ".WWWkWWWW.....",
    "...CCCCWW...W.",
    "....YWWWWW.W..",
    "....WWWWWW.W..",
    "...WWWWWWW.W..",
    "...WWWWWWW.W..",
    "WWWWWWWWWWW...",
]
# Lying down (night "curl"): same head, long body, tail tucked along the floor.
LIE = [
    "..W...W..........",
    "..WW.WW..........",
    "..WWWWW..........",
    "..WkWkW..........",
    ".WWWkWWWWWWWWW...",
    "...CCCCWWWWWWWWW.",
    "....YWWWWWWWWWWWW",
    "WWWWWWWWWWWWWWWWW",
]


def set_px(rows, x, y, ch):
    r = list(rows[y])
    r[x] = ch
    rows[y] = "".join(r)


def tail_variant(rows, kind):
    rows = list(rows)
    for y in range(4, 10):              # clear the reference tail columns
        for x in (11, 12, 13):
            set_px(rows, x, y, ".")
    if kind == 0:                       # reference: tip leans right
        set_px(rows, 12, 5, "W")
        for y in range(6, 10):
            set_px(rows, 11, y, "W")
    elif kind == 1:                     # sway right
        set_px(rows, 13, 5, "W")
        set_px(rows, 12, 6, "W")
        for y in range(7, 10):
            set_px(rows, 11, y, "W")
    else:                               # tip up and curled forward
        set_px(rows, 12, 4, "W")
        set_px(rows, 12, 5, "W")
        for y in range(6, 10):
            set_px(rows, 11, y, "W")
    return rows


def ear_twitch(rows):
    rows = list(rows)
    set_px(rows, 6, 0, ".")             # right ear folds down for a beat
    return rows


def scale(rows, s=SCALE):
    out = []
    for r in rows:
        line = "".join(ch * s for ch in r)
        out.extend([line] * s)
    return out


def outline(rows):
    """Pad by 1 and add a dark 1-px outline around every opaque pixel."""
    h, w = len(rows), len(rows[0])
    src = ["." * (w + 2)] + ["." + r + "." for r in rows] + ["." * (w + 2)]
    out = [list(r) for r in src]
    for y in range(h + 2):
        for x in range(w + 2):
            if src[y][x] != ".":
                continue
            for dy in (-1, 0, 1):
                for dx in (-1, 0, 1):
                    yy, xx = y + dy, x + dx
                    if 0 <= yy < h + 2 and 0 <= xx < w + 2 and src[yy][xx] != ".":
                        out[y][x] = "k"
    return ["".join(r) for r in out]


def lower(rows):
    m = {'W': 'w', 'C': 'c', 'Y': 'y'}
    return ["".join(m.get(ch, ch) for ch in r) for r in rows]


def big(rows):
    return outline(lower(scale(rows)))


# Scaled-space face anchors (after the +1 outline padding), shared with
# src/scene_chaton.c through the emitted CAT_* defines.
EYE_L = (1 + 3 * SCALE, 1 + 3 * SCALE)     # native (3,3)
EYE_R = (1 + 5 * SCALE, 1 + 3 * SCALE)     # native (5,3)
NOSE = (1 + 4 * SCALE, 1 + 4 * SCALE)      # native (4,4)
BELL = (1 + 4 * SCALE, 1 + 6 * SCALE)      # native (4,6)
COLLAR = (1 + 3 * SCALE, 1 + 5 * SCALE)    # native (3,5), 4 cells wide

# ---- the cat: 3 tail positions + ear twitch, and the lying pose ----
grid("cat_sit_0", big(tail_variant(SIT, 0)))
grid("cat_sit_1", big(tail_variant(SIT, 1)))
grid("cat_sit_2", big(tail_variant(SIT, 2)))
grid("cat_sit_ear", big(ear_twitch(tail_variant(SIT, 0))))
grid("cat_lie", big(LIE))

# ---- eyes and nose (3x3 at the scaled resolution, stamped on the face) ----
grid("eye_open",   ["kkk", "kkk", "kkk"])
grid("eye_shine",  ["kkw", "kkk", "kkk"])      # catch-light, idle default
grid("eye_left",   ["kkw", "kkw", "kkw"])
grid("eye_right",  ["wkk", "wkk", "wkk"])
grid("eye_down",   ["www", "kkk", "kkk"])
grid("eye_closed", ["www", "www", "kkk"])
grid("eye_happy",  ["wkw", "kwk", "www"])      # ^ sleeping contentedly
grid("eye_wide",   ["kkk", "kwk", "kkk"])      # startled by thunder
grid("nose",       ["kkk", "wkw", "www"])

# ---- props ----
# Umbrella: symmetric striped dome over the head, handle down behind the cat.
def umbrella():
    half = [1, 5, 8, 10, 11, 12, 13, 13]     # half-width per row, top -> hem
    w, cx = 27, 13
    rows = [["."] * w for _ in half]
    for y, hw in enumerate(half):
        for x in range(cx - hw, cx + hw + 1):
            rows[y][x] = "a" if ((x + 2) // 5) % 2 == 0 else "w"
    hem = rows[-1]
    for x in range(w):                      # scalloped hem
        if hem[x] != "." and (x + 2) % 5 == 0:
            hem[x] = "."
    rows.insert(0, ["."] * w)
    rows[0][cx] = "k"                       # finial
    canopy = outline(["".join(r) for r in rows])
    cw = len(canopy[0])
    stick = []
    for i in range(22):
        line = ["."] * cw
        line[cw // 2] = "k"
        stick.append("".join(line))
    return canopy + stick

grid("umbrella", umbrella())

grid("scarf", outline([
    "aaaaaaaaaaaaaa",
    "cacacacacacaca",
    "aaaaaaaaaaaaaa",
    ".aaa..........",
    ".cac..........",
    ".aaa..........",
    ".cac..........",
    ".aaa..........",
    ".k.k..........",
]))

grid("bowtie", [
    "kk.....kk",
    "kkkk.kkkk",
    "kkkkkkkkk",
    "kkkk.kkkk",
    "kk.....kk",
])

grid("shades", [
    "kkkkkkkkkkkkkkk",
    "kkwkkkk.kkwkkkk",
    ".kkkkk...kkkkk.",
    "..kkk.....kkk..",
])

grid("yarn_a", outline([
    "..aaaaa..",
    ".aacaaaa.",
    "aaaacaaaa",
    "acaaacaaa",
    "aacaaacaa",
    "aaacaaaca",
    "aaaacaaaa",
    ".aaaacaa.",
    "..aaaaa..",
]))
grid("yarn_b", outline([
    "..aaaaa..",
    ".aaaacaa.",
    "aaaacaaaa",
    "aaacaaaca",
    "aacaaacaa",
    "acaaacaaa",
    "aaaacaaaa",
    ".aacaaaa.",
    "..aaaaa..",
]))

grid("croissant_a", outline([
    "...yyyyyyyy....",
    ".yyayyayyayyy..",
    "yyyayyayyayyyy.",
    ".yyayyayyayyyyy",
    "...yyyyyyyyyy..",
]))
grid("croissant_b", outline([
    "...yyyyyy......",
    ".yyayyayy......",
    "yyyayyayyy.....",
    ".yyayyayyy...y.",
    "...yyyyyy..y...",
]))

# Mini pixel M (the Mistral mark): yellow top, orange middle, red base.
grid("mblock", outline([
    "yy...yy",
    "yyy.yyy",
    "aaaaaaa",
    "aa.a.aa",
    "cc...cc",
]))

# "z z z": one Z glyph rising (3 frames)
ZB_BIG = ["aaaa", "..a.", ".a..", "aaaa"]
ZB_SML = ["aaa", ".a.", "aaa"]


def zbub(place):
    rows = [["."] * 7 for _ in range(7)]
    for zx, zy, glyph in place:
        for j, gr in enumerate(glyph):
            for i, ch in enumerate(gr):
                rows[zy + j][zx + i] = ch
    return ["".join(r) for r in rows]

grid("zbub_0", zbub([(0, 3, ZB_BIG)]))
grid("zbub_1", zbub([(2, 1, ZB_BIG)]))
grid("zbub_2", zbub([(4, 0, ZB_SML)]))

# ---- mini cat for the window sill (native 1x + outline), blink frame ----
grid("minicat_0", outline(lower(tail_variant(SIT, 0))))
_blink = tail_variant(SIT, 0)
set_px(_blink, 3, 3, "W")
set_px(_blink, 5, 3, "W")
grid("minicat_1", outline(lower(_blink)))


# ---- emit ----
def emit():
    out = ["/* Generated by tools/gen_chaton.py -- do not edit by hand.",
           " * Palette-indexed sprites for the Mistral pixel cat (screensaver",
           " * character + window-sill cat). 0 = transparent, other values are",
           " * P_* slots from src/scene.h. */",
           "#ifndef CLOCK_CHATON_H",
           "#define CLOCK_CHATON_H",
           "",
           "#include <stdint.h>",
           "",
           "/* face anchors inside art_cat_sit_* / art_cat_lie (scaled pixels) */",
           f"#define CAT_EYE_L_X {EYE_L[0]}",
           f"#define CAT_EYE_L_Y {EYE_L[1]}",
           f"#define CAT_EYE_R_X {EYE_R[0]}",
           f"#define CAT_EYE_R_Y {EYE_R[1]}",
           f"#define CAT_NOSE_X {NOSE[0]}",
           f"#define CAT_NOSE_Y {NOSE[1]}",
           f"#define CAT_BELL_X {BELL[0]}",
           f"#define CAT_BELL_Y {BELL[1]}",
           f"#define CAT_COLLAR_X {COLLAR[0]}",
           f"#define CAT_COLLAR_Y {COLLAR[1]}",
           ""]
    for name, (w, rows) in GRIDS.items():
        out.append(f"#define ART_{name.upper()}_W {w}")
        out.append(f"#define ART_{name.upper()}_H {len(rows)}")
        out.append(f"static const uint8_t art_{name}[{w * len(rows)}] = {{")
        vals = [CH[ch] for r in rows for ch in r]
        for i in range(0, len(vals), 20):
            out.append("    " + ", ".join(str(v) for v in vals[i:i + 20]) + ",")
        out.append("};")
        out.append("")
    out.append("#endif")
    return "\n".join(out) + "\n"


# ---- preview (dark-theme palette, must match s_grade[THEME_DARK]) ----
PAL = {0: None, 5: 0xFFCD00, 6: 0xFF8205, 12: 0xF5EDE0, 13: 0x1A120B,
       14: 0xE75D2D, 15: 0xFF8205}


def preview(path):
    from PIL import Image, ImageDraw
    s, pad, cols = 4, 10, 4
    names = list(GRIDS)
    cw = max(w for w, _ in GRIDS.values()) * s + pad
    ch = max(len(r) for _, r in GRIDS.values()) * s + pad + 12
    rows_n = (len(names) + cols - 1) // cols
    img = Image.new("RGB", (cols * cw, rows_n * ch), (36, 30, 46))
    d = ImageDraw.Draw(img)
    for i, n in enumerate(names):
        _w, rows = GRIDS[n]
        ox, oy = (i % cols) * cw + pad // 2, (i // cols) * ch + 12
        d.text((ox, oy - 11), n, fill=(230, 230, 230))
        for y, r in enumerate(rows):
            for x, c in enumerate(r):
                v = PAL.get(CH[c])
                if v is not None:
                    d.rectangle([ox + x * s, oy + y * s, ox + x * s + s - 1,
                                 oy + y * s + s - 1],
                                fill=((v >> 16) & 255, (v >> 8) & 255, v & 255))
    img.save(path)


if __name__ == "__main__":
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--preview", metavar="PNG",
                    help="also write a sprite sheet (needs Pillow)")
    args = ap.parse_args()
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    with open(os.path.join(root, "src", "assets", "chaton.h"), "w") as f:
        f.write(emit())
    print(f"wrote src/assets/chaton.h ({len(GRIDS)} sprites)")
    if args.preview:
        preview(args.preview)
        print(f"wrote {args.preview}")
