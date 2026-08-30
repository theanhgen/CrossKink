#!/usr/bin/env python3
"""Render text through the full pipeline onto a simulated K3 panel, as a PNG.

Two modes:

  ./render_panel_proof.py compare
      Side-by-side AA-on vs AA-off for the shortlist. This is the picture that
      shows why 'fatten' reorders the ranking -- see measure_aa_dilation.py.

  ./render_panel_proof.py page --font LexicaUltralegible --size 10
      A full 600x800 page at 1:1, so you can count real lines and judge the
      measure before committing to a default. Feed it real prose with --text.

Nothing here is approximated: 150 DPI rasterization, darken-aa thresholds, the
panel's measured 4-level palette and the AA-off promotion rule all come from
pipeline.py, which cites the shipping source line by line.
"""
import argparse
import os
import textwrap
from PIL import Image, ImageDraw

from pipeline import (PANEL_W, PANEL_H, PANEL_GREY, OUT, FONTS, face_at,
                      render, name_of, font_paths)

SHORTLIST = [("LexicaUltralegible", 11), ("SourceSans3", 11), ("NotoSerif", 10),
             ("Bitter", 10), ("LexendDeca", 10), ("Inter", 10), ("IBMPlexSans", 11)]

SPECIMEN = "The quick brown fox jumps over"

PROSE = ("It was a bright cold day in April, and the clocks were striking "
         "thirteen. Winston Smith, his chin nuzzled into his breast in an "
         "effort to escape the vile wind, slipped quickly through the glass "
         "doors of Victory Mansions, though not quickly enough to prevent a "
         "swirl of gritty dust from entering along with him. The hallway "
         "smelt of boiled cabbage and old rag mats. At one end of it a "
         "coloured poster, too large for indoor display, had been tacked to "
         "the wall. It depicted simply an enormous face, more than a metre "
         "wide: the face of a man of about forty-five, with a heavy black "
         "moustache and ruggedly handsome features.")


def resolve(name):
    for ext in (".ttf", ".otf"):
        p = os.path.join(FONTS, name + ext)
        if os.path.exists(p):
            return p
    raise SystemExit(f"no font {name!r} in {FONTS} — have: "
                     + ", ".join(name_of(p) for p in font_paths()))


def blit(img, x0, baseline_top, face, text, aa, clip_w=None):
    """Draw text at stored-level fidelity. Returns the pen x after the run."""
    px = img.load()
    asc = face.size.ascender >> 6
    pen = x0
    for ch in text:
        g = render(face, ch)
        if not g:
            continue
        if clip_w and pen + g["adv"] > clip_w:
            break
        for y, row in enumerate(g["grid"]):
            for x, v in enumerate(row):
                if v == 0:
                    continue
                if not aa:
                    v = 3                      # GfxRenderer.cpp:871
                X = pen + g["left"] + x
                Y = baseline_top + asc - g["top"] + y
                if 0 <= X < img.width and 0 <= Y < img.height:
                    px[X, Y] = PANEL_GREY[v]
        pen += round(g["adv"])
    return pen


def cmd_compare(a):
    W, LH, ZOOM = 420, 26, a.zoom
    img = Image.new("L", (W * 2 + 30, LH * len(SHORTLIST) + 30), 255)
    d = ImageDraw.Draw(img)
    for n, (name, size) in enumerate(SHORTLIST):
        f = face_at(resolve(name), size)
        y = 18 + n * LH
        blit(img, 8, y, f, SPECIMEN, aa=True)
        blit(img, W + 22, y, f, SPECIMEN, aa=False)
    d.text((8, 4), "AA ON (4 levels, 2 refreshes)", fill=0)
    d.text((W + 22, 4), "AA OFF (1 refresh)", fill=0)
    out = os.path.join(OUT, "panel.png")
    img.resize((img.width * ZOOM, img.height * ZOOM), Image.NEAREST).save(out)
    print(f"wrote {out}  ({img.width*ZOOM}x{img.height*ZOOM}, {ZOOM}x nearest)")
    print("fonts, top to bottom: " + ", ".join(f"{n} {s}" for n, s in SHORTLIST))


def cmd_page(a):
    path = resolve(a.font)
    f = face_at(path, a.size)
    text = a.text or PROSE
    if a.text and os.path.exists(a.text):
        text = open(a.text, encoding="utf-8").read()

    img = Image.new("L", (PANEL_W, PANEL_H), 255)
    usable = PANEL_W - 2 * a.margin
    line_h = (f.size.height >> 6) + a.leading

    # Greedy wrap using real advances, the same thing the layout engine does.
    widths = {}
    def adv(ch):
        if ch not in widths:
            g = render(f, ch)
            widths[ch] = g["adv"] if g else 0
        return widths[ch]

    # Fill the page: repeat the sample if it runs short, so what you are
    # looking at is a full page rather than a half-empty one.
    capacity = (PANEL_H - 2 * a.margin) // line_h
    words = text.split()
    est = max(1, int(usable / max(1, sum(adv(c) for c in "n") or 8) / 6))
    while len(words) < capacity * est * 2:
        words = words + text.split()

    lines, cur = [], ""
    for word in words:
        trial = (cur + " " + word).strip()
        if sum(adv(c) for c in trial) > usable and cur:
            lines.append(cur)
            cur = word
        else:
            cur = trial
    if cur:
        lines.append(cur)

    y = a.margin
    drawn = 0
    for ln in lines:
        if y + line_h > PANEL_H - a.margin:
            break
        blit(img, a.margin, y, f, ln, aa=not a.no_aa)
        y += line_h
        drawn += 1

    tag = f"{a.font}-{a.size}{'-noaa' if a.no_aa else ''}"
    out = os.path.join(OUT, f"page-{tag}.png")
    if a.zoom > 1:
        img = img.resize((PANEL_W * a.zoom, PANEL_H * a.zoom), Image.NEAREST)
    img.save(out)
    body = lines[:drawn]
    avg = sum(len(l) for l in body) / drawn if drawn else 0
    print(f"wrote {out}")
    print(f"  {drawn} lines/page, {avg:.0f} chars/line, "
          f"~{drawn*avg:.0f} chars/page, line height {line_h}px, "
          f"margin {a.margin}px, AA {'off' if a.no_aa else 'on'}")


def main():
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest="cmd", required=True)

    c = sub.add_parser("compare", help="AA on vs off, shortlist")
    c.add_argument("--zoom", type=int, default=4)
    c.set_defaults(fn=cmd_compare)

    p = sub.add_parser("page", help="full 600x800 page, one font")
    p.add_argument("--font", default="LexicaUltralegible")
    p.add_argument("--size", type=int, default=10)
    p.add_argument("--margin", type=int, default=20)
    p.add_argument("--leading", type=int, default=0,
                   help="extra px added to the font's own advanceY")
    p.add_argument("--no-aa", action="store_true",
                   help="apply GfxRenderer.cpp:871 -- every fringe pixel to black")
    p.add_argument("--text", help="literal text, or a path to a .txt file")
    p.add_argument("--zoom", type=int, default=1)
    p.set_defaults(fn=cmd_page)

    a = ap.parse_args()
    os.makedirs(OUT, exist_ok=True)
    a.fn(a)


if __name__ == "__main__":
    main()
