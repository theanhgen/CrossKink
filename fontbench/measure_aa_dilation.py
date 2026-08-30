#!/usr/bin/env python3
"""How much does each face FATTEN when textAntiAliasing is off?

Turning AA off does not drop the antialiasing. GfxRenderer.cpp:871 reads

    if (renderMode == BW && bmpVal < 3) drawPixel(black)

and bmpVal = 3 - stored (GfxRenderer.cpp:869), so stored 3, 2 AND 1 -- black,
dark grey and light grey alike -- all paint solid black. Only stored 0 survives
as white. The glyph is therefore dilated by its entire AA fringe.

    ink AA-on  = sum(stored)/3     perceived, grey weighted
    ink AA-off = count(stored > 0) every fringe pixel promoted to full ink

The ratio is how much heavier the face gets, and it varies more than 2x across
the candidates -- enough to reorder which font you should pick.

  ./measure_aa_dilation.py            at size 10
  ./measure_aa_dilation.py --size 12
"""
import argparse
from pipeline import PANGRAM, face_at, render, font_paths, name_of


def ink(path, size):
    f = face_at(path, size)
    on = off = 0
    for ch in PANGRAM:
        if ch == " ":
            continue
        g = render(f, ch)
        if not g:
            continue
        for row in g["grid"]:
            for v in row:
                on += v / 3.0
                off += 1 if v > 0 else 0
    return on, off


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--fonts")
    ap.add_argument("--size", type=int, default=10)
    a = ap.parse_args()

    rows = []
    for p in font_paths(a.fonts):
        on, off = ink(p, a.size)
        if on:
            rows.append((off / on, name_of(p), on, off))
    rows.sort()

    print(f"size {a.size}, sorted by least dilation\n")
    print(f"{'font':<20}{'ink AA-on':>11}{'ink AA-off':>12}{'fatten':>9}")
    for r, name, on, off in rows:
        print(f"{name:<20}{on:>11.0f}{off:>12.0f}{(r-1)*100:>8.0f}%")


if __name__ == "__main__":
    main()
