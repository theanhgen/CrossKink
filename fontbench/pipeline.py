"""CrossInk's offline glyph pipeline, replicated exactly. Shared by every tool here.

Traced from lib/EpdFont/scripts/fontconvert_sdcard.py against the fork point in UPSTREAM.lock:

  face.set_char_size(size<<6, size<<6, 150, 150)   :629   fixed 150 DPI, never the panel's
  face.load_glyph(idx, FT_LOAD_RENDER)             :649   8-bit coverage
  bm = coverage >> 4                               :731-745  -> 0..15
  bm>=10 -> 3 | >=6 -> 2 | >=3 -> 1 | else 0       :622   --darken-aa (3,6,10)

Every reader font is built with --darken-aa (convert-builtin-fonts.sh:149,
build-sd-fonts.py:384), so (3,6,10) is the shipping cut, not the (4,8,12) default.

Stored level -> panel nibble, via GfxRenderer.cpp:869 (bmpVal = 3 - stored) and
platform/kindle/HalDisplay.cpp:104-107:
    3 -> 0xF black    2 -> 0xA dark    1 -> 0x5 light    0 -> 0x0 white
"""
import os
import freetype

DPI = 150
THRESH = (3, 6, 10)
PPI_K3 = 167.0                       # platform/kindle/EInkDisplay.h:22, measured on device
PANEL_W, PANEL_H = 600, 800          # ditto
PANEL_GREY = {0: 255, 1: 170, 2: 85, 3: 0}

HERE = os.path.dirname(os.path.abspath(__file__))
FONTS = os.path.join(HERE, "fonts")
OUT = os.path.join(HERE, "out")

LOWER = "etaoinshrdlucmfwypvbgkjqxz"
PANGRAM = ("The quick brown fox jumps over the lazy dog. "
           "Portez ce vieux whisky au juge blond qui fume. "
           "Sphinx of black quartz, judge my vow.")


def shade(cov8):
    """8-bit FreeType coverage -> stored 2-bit level."""
    b = cov8 >> 4
    return 3 if b >= THRESH[2] else 2 if b >= THRESH[1] else 1 if b >= THRESH[0] else 0


def face_at(path, size):
    f = freetype.Face(path)
    f.set_char_size(size << 6, size << 6, DPI, DPI)
    return f


def render(face, ch):
    """One glyph as a grid of stored levels, plus its metrics."""
    idx = face.get_char_index(ord(ch))
    if idx == 0:
        return None
    face.load_glyph(idx, freetype.FT_LOAD_RENDER)
    g = face.glyph
    bm = g.bitmap
    buf, pitch = bm.buffer, abs(bm.pitch)
    grid = []
    for y in range(bm.rows):
        off = y * pitch if bm.pitch >= 0 else (bm.rows - 1 - y) * pitch
        grid.append([shade(buf[off + x]) for x in range(bm.width)])
    return dict(grid=grid, w=bm.width, h=bm.rows, top=g.bitmap_top,
                left=g.bitmap_left, adv=g.linearHoriAdvance / 65536.0)


def font_paths(d=None):
    d = d or FONTS
    out = []
    for fn in sorted(os.listdir(d)):
        if fn.lower().endswith((".ttf", ".otf")):
            out.append(os.path.join(d, fn))
    if not out:
        raise SystemExit(f"no fonts in {d} — run ./download.sh first")
    return out


def name_of(path):
    return os.path.basename(path).rsplit(".", 1)[0]


def mm(px):
    """Pixels on the K3 panel -> millimetres."""
    return px * 25.4 / PPI_K3
