#!/usr/bin/env python3
"""Score every candidate face on what actually lands on the K3 panel.

  ./measure_fonts.py ladder                       per-font size ladder
  ./measure_fonts.py equal                        fair comparison at equal x-height
  ./measure_fonts.py equal --target 11 --margin 20
"""
import argparse
import sys
from pipeline import PANEL_W, LOWER, PANGRAM, face_at, render, font_paths, name_of, mm


def stem_width(g):
    """Narrowest fully-black vertical stem, measured on the glyph's middle row.

    Level 3 only. A stem that renders as grey is exactly what this metric is
    meant to catch, so counting grey would defeat it."""
    if not g or g["h"] == 0:
        return 0
    best = cur = 0
    for v in g["grid"][g["h"] // 2]:
        cur = cur + 1 if v == 3 else 0
        best = max(best, cur)
    return best


def measure(path, size):
    f = face_at(path, size)
    gx, gH = render(f, "x"), render(f, "H")
    if not gx or not gH:
        return None

    advs = [g["adv"] for g in (render(f, c) for c in LOWER) if g]
    hist = [0, 0, 0, 0]
    for ch in PANGRAM:
        if ch == " ":
            continue
        g = render(f, ch)
        if not g:
            continue
        for row in g["grid"]:
            for v in row:
                hist[v] += 1

    inked = hist[1] + hist[2] + hist[3]
    stems = [s for s in (stem_width(render(f, c)) for c in "ilnhbdk") if s]
    return dict(size=size, xh=gx["h"], cap=gH["h"],
                adv=sum(advs) / len(advs),
                blk=hist[3] / inked if inked else 0,
                lgt=hist[1] / inked if inked else 0,
                ink=inked / len([c for c in PANGRAM if c != " "]),
                stem=min(stems) if stems else 0)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("mode", choices=["ladder", "equal"])
    ap.add_argument("--fonts")
    ap.add_argument("--sizes", default="10,12")
    ap.add_argument("--target", type=int, default=12,
                    help="equal mode: x-height in px to match (12px = 1.83mm)")
    ap.add_argument("--margin", type=int, default=5,
                    help="screenMargin px per side; 5 is the shipped default "
                         "(src/CrossPointSettings.h:412)")
    a = ap.parse_args()
    usable = PANEL_W - 2 * a.margin
    paths = font_paths(a.fonts)

    if a.mode == "ladder":
        print(f"600px panel, margin {a.margin} -> {usable}px usable\n")
        print(f"{'font':<20}{'pt':>4}{'x-h':>5}{'cap':>5}{'adv':>7}{'stem':>6}"
              f"{'blk%':>7}{'lgt%':>7}{'ink/ch':>8}{'ch/line':>9}{'x-h mm':>8}")
        for p in paths:
            for size in (int(s) for s in a.sizes.split(",")):
                m = measure(p, size)
                if not m:
                    continue
                print(f"{name_of(p):<20}{size:>4}{m['xh']:>5}{m['cap']:>5}"
                      f"{m['adv']:>7.2f}{m['stem']:>6}{m['blk']*100:>7.1f}"
                      f"{m['lgt']*100:>7.1f}{m['ink']:>8.1f}"
                      f"{usable/m['adv']:>9.1f}{mm(m['xh']):>8.2f}")
            print()
        return

    print(f"at x-height ~= {a.target}px ({mm(a.target):.2f} mm), "
          f"margin {a.margin} -> {usable}px usable")
    print("nominal pt is not comparable across families, so match x-height "
          "first and compare what it costs horizontally\n")
    print(f"{'font':<20}{'pt':>4}{'x-h':>5}{'adv':>7}{'ch/line':>9}{'stem':>6}"
          f"{'blk%':>7}{'lgt%':>7}{'ink/ch':>8}")
    rows = []
    for p in paths:
        best = None
        for size in range(6, 25):
            m = measure(p, size)
            if not m:
                continue
            d = abs(m["xh"] - a.target)
            if best is None or d < best[0]:
                best = (d, m)
        if best and best[0] <= 1:
            rows.append((name_of(p), best[1]))
        else:
            print(f"{name_of(p):<20}  -- no size within 1px of target", file=sys.stderr)
    rows.sort(key=lambda r: -usable / r[1]["adv"])
    for name, m in rows:
        print(f"{name:<20}{m['size']:>4}{m['xh']:>5}{m['adv']:>7.2f}"
              f"{usable/m['adv']:>9.1f}{m['stem']:>6}{m['blk']*100:>7.1f}"
              f"{m['lgt']*100:>7.1f}{m['ink']:>8.1f}")


if __name__ == "__main__":
    main()
