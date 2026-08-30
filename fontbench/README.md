# fontbench

Measures candidate typefaces on what actually reaches the Kindle 3 panel, by replaying CrossInk's
real offline glyph pipeline on the host. No device needed. Findings live in [`../FONTS.md`](../FONTS.md).

## Run it

```sh
./run.sh
```

Builds `.venv` (needs `uv`), fetches the corpus if missing, writes everything into `out/`.
Takes about a minute. Re-runs are cheap — the venv and corpus are kept.

`fonts/`, `out/` and `.venv/` are gitignored; `download.sh` reproduces the corpus.

## What each tool answers

| | question |
|---|---|
| `measure_fonts.py ladder` | at each pt step, how big is this face and how many chars fit? |
| `measure_fonts.py equal` | **at the same x-height**, what does each face cost horizontally? |
| `measure_aa_dilation.py` | how much heavier does each face get with AA off? |
| `render_panel_proof.py compare` | the AA on/off difference, as a picture |
| `render_panel_proof.py page` | a full 600×800 page at 1:1 — count real lines, judge the measure |

Nominal point size is **not** comparable across families here (`../FONTS.md`, Finding 4), so `equal`
is the mode that actually ranks them. `ladder` is for picking a step once you have chosen a face.

## Reading the columns

- **x-h / cap** — bitmap height of `x` and `H` in px. `x-h mm` converts at the panel's 167 ppi.
- **adv** — mean advance over the 26 lowercase letters. **ch/line** is usable width ÷ adv.
- **stem** — narrowest *fully black* vertical stem across `ilnhbdk`, in px. **1 is a warning**: the
  stems are rendering as grey, and the face will look thin and washed out on e-ink.
- **blk%** — share of inked pixels reaching level 3 (true black) rather than one of the two greys.
  Higher is crisper.
- **lgt%** — share landing on the lightest grey (`0x5`). These are the pixels that turn black when
  AA is off.
- **fatten** — how much more ink the glyph carries with AA off. See below.

## Why `fatten` is the column that decides things

Turning AA off does not remove antialiasing. `GfxRenderer.cpp:871` reads

```c
if (renderMode == BW && bmpVal < 3) drawPixel(black)
```

and `bmpVal = 3 - stored`, so black, dark grey **and** light grey all paint solid black. Only pure
white survives — the glyph is dilated by its whole AA fringe. Measured across the corpus that ranges
from +10% (Lexica Ultralegible) to +28% (IBM Plex Sans).

That matters because AA-on costs two panel refreshes and a double page render on this port
(`../FONTS.md`, Finding 2). So the AA decision is live, and it reorders the ranking: Source Sans 3
wins on measure but is a +23% fattener, while Lexica is top-tier either way.

## Useful invocations

```sh
# the shortlist, side by side, AA on vs off
.venv/bin/python render_panel_proof.py compare

# the proposed default vs what ships today
.venv/bin/python render_panel_proof.py page --font LexicaUltralegible --size 10 --margin 20
.venv/bin/python render_panel_proof.py page --font Bitter --size 14 --margin 5

# same font with AA off, to see the dilation on a real page
.venv/bin/python render_panel_proof.py page --font Inter --size 10 --no-aa

# your own text, and a tighter x-height target
.venv/bin/python render_panel_proof.py page --font NotoSerif --size 10 --text mybook.txt
.venv/bin/python measure_fonts.py equal --target 11 --margin 20
```

`--zoom N` on either render mode magnifies with nearest-neighbour, for looking at individual pixels.

## Corpus

20 faces: every regular style in `sd-fonts.yaml` that resolves to a static file, plus the four
already vendored under `builtinFonts/source/`. Alegreya and Vollkorn are **absent** — they are
variable fonts and freetype-py cannot pin an axis, so they need `fonttools.instancer` first
(`build-sd-fonts.py` does this). Only regular is measured; bold and italic are not.

## Fidelity

Everything in `pipeline.py` is traced to a shipping source line and cited there:
150 DPI rasterization (`fontconvert_sdcard.py:629`), the `>>4` downsample and `--darken-aa`
thresholds (3, 6, 10) that every reader font is built with (`:622`, `convert-builtin-fonts.sh:149`),
and the panel's measured 4-level palette (`platform/kindle/HalDisplay.cpp:104-107`).

What it does **not** model: the actual einkfb waveform, ghosting, temperature compensation, or the
real layout engine's justification and hyphenation. Line breaking here is a greedy wrap on real
advances — close enough to count lines, not the same code as `Epub/`.
