# Fonts on the Kindle 3 port

Research note, 2026-08-30. Evidence is cited `file:line` against this tree — the CrossInk
sources under `lib/` and `src/`, and the Kindle backend under `platform/kindle/`.

Every measurement here is reproducible with `fontbench/run.sh` — one command, no device.
See `fontbench/README.md`.

## TL;DR

1. **The size ladder is wrong for this panel by about two steps.** CrossInk's default is 14 pt,
   tuned for a 217 ppi 4.3" panel. On the K3's 167 ppi 6" panel that renders at 2.43 mm x-height
   and ~38 chars/line — large-print, short measure. The paperback-equivalent step here is **10**.
2. **Anti-aliased text costs two panel refreshes per page turn on this port**, and forces the slow
   waveform on the second. That is a visible two-stage flash the ESP32 does not have.
3. **The 4-level ceiling is a font-format limit, not a panel limit.** Pearl does 16 levels, `/dev/fb0`
   is natively 4bpp, and the converter *already computes* 16-level coverage before throwing it away.
4. Point size is **not comparable across families** in this system — Bitter at 12 has a 17% taller
   'x' bitmap than ChareInk at 12. Switching family visibly changes apparent size.
5. **Measured across 20 candidate faces, LexicaUltralegible at size 10 is the pick.** It is the only
   one that is top-tier whether AA ends up on or off, which makes it safe to land before (2) is
   settled. Bitter, the incumbent, is a legitimate keep at the same size.

## How the pipeline actually works

Glyphs are **pre-rasterized offline**, never at runtime. There is no FreeType on the device.

```
TTF/OTF  --FreeType @ 150 DPI-->  8-bit coverage
         --threshold (3,6,10)-->  2 bits/px, 4 shades   [.cpfont / builtinFonts/*.h]
         --GfxRenderer split--->  1bpp BW + 1bpp LSB plane + 1bpp MSB plane
         --HalDisplay compose-->  4bpp nibbles 0x0/0x5/0xA/0xF  -> /dev/fb0
```

| Fact | Evidence |
|---|---|
| Rasterized at a fixed **150 DPI**, both converters | `lib/EpdFont/scripts/fontconvert.py:314`, `fontconvert_sdcard.py:629` |
| Output is **2 bits/px, 4 shades** | `fontconvert_sdcard.py:4-5`, `EpdFontData.h` `is2Bit` |
| 16-level coverage is computed, then discarded | `fontconvert_sdcard.py:731-745` builds `pixels4g` (4-bit), then downsamples to `pixels2b` |
| AA thresholds, darkened for all reader fonts | `fontconvert_sdcard.py:622`, `fontconvert.py:39`; `--darken-aa` passed at `convert-builtin-fonts.sh:149` and `build-sd-fonts.py:384` |
| Renderer destructively splits 2bpp into three 1bpp planes | `lib/GfxRenderer/GfxRenderer.cpp:851-884` |
| Panel palette, measured on device | `platform/kindle/HalDisplay.cpp:88-107` — `0x0` white, `0x5`, `0xA`, `0xF` black |
| Panel geometry, measured on device | `platform/kindle/EInkDisplay.h:22-23` — 600x800 portrait, 167 ppi |

`--darken-aa` exists because e-ink wants heavier stems than an LCD-tuned outline gives. That is the
same move `nicoverbruggen/ebook-fonts` makes by hand ("Sourcerer — thickened Source Serif for
improved contrast"). Direction is right; it is applied to every reader font already.

## Finding 1 — the size ladder needs shifting down two steps

Measured from the shipped builtin headers (bitmap height of `x` and `H`, mean advance over the 26
lowercase letters), converted to physical units at the K3's 167 ppi:

| setting | em px | `x` bitmap px | `H` px | mean adv px | x-height mm | chars/line @590 | @560 | @520 |
|--:|--:|--:|--:|--:|--:|--:|--:|--:|
| 8  | 16.7 | 9  | 12 | 8.97  | 1.37 | 66 | 62 | 58 |
| 9  | 18.8 | 11 | 14 | 10.09 | 1.67 | 58 | 55 | 51 |
| 10 | 20.8 | 12 | 15 | 11.21 | 1.83 | 53 | 50 | 46 |
| 12 | 25.0 | 14 | 18 | 13.46 | 2.13 | 44 | 42 | 39 |
| **14** (default) | 29.2 | 16 | 21 | 15.71 | **2.43** | **38** | 36 | 33 |
| 16 | 33.3 | 18 | 24 | 17.94 | 2.74 | 33 | 31 | 29 |
| 18 | 37.5 | 21 | 27 | 20.19 | 3.19 | 29 | 28 | 26 |
| 20 | 41.7 | 23 | 30 | 22.44 | 3.50 | 26 | 25 | 23 |

Family: Bitter Regular. Column widths are usable px after margins; `screenMargin` defaults to **5**
(`src/CrossPointSettings.h:412`), so @590 is the shipped default and @560/@520 are what you get at
comfortable margins.

Reference points: mass-market paperback runs ~1.5–1.8 mm x-height at 60–70 chars/line; trade
paperback ~1.8–2.0 mm at 55–65. The comfortable band for justified text bottoms out around 45.

Rendered as whole pages by `fontbench/render_panel_proof.py page`, the two ends of that argument:

| | lines/page | chars/line | **chars/page** |
|---|--:|--:|--:|
| shipped default — Bitter 14, margin 5 | 22 | 40 | **880** |
| proposed — LexicaUltralegible 10, margin 20 | 29 | 59 | **1704** |

Nearly double the text per page, which on e-ink means roughly half the page turns.

**Reading:** setting 9–10 is the paperback equivalent on this panel. 12 is already large print.
The shipped default of 14 (`src/CrossPointSettings.cpp:911-923`, `MEDIUM`) is large print on a
short measure, which will also make justification ragged and hyphenation frequent.

Same numbers on the X4 the default was tuned for: setting 14 gives 1.87 mm x-height across 800 px
landscape — about 48 chars/line. Sane there. The ratio is just `167/217 = 0.77`, so the whole
ladder wants to move down roughly two steps.

**Proposed for K3:** `TINY=8, SMALL=9, MEDIUM=10, LARGE=12`, and raise `screenMargin` default from
5 to ~20. Every one of those sizes is already generated — `{8, 9, 10, 12}` is an existing SD range
preset (`src/CrossPointSettings.cpp:75-81`), and all eight sizes ship as builtins.

## Finding 2 — AA costs two panel refreshes per page turn

With `textAntiAliasing` on (default 1, `src/CrossPointSettings.h:358`) and a black foreground, the
reader takes the grayscale path (`src/activities/reader/EpubReaderActivity.cpp:5754`). On this port
that lands as:

- `displayGrayscaleBase` → blit BW, **panel update** (`platform/kindle/HalDisplay.cpp:457-463`)
- `displayGrayBuffer` → blit with planes, **panel update at `FX_UPDATE_SLOW`** (`HalDisplay.cpp:489-495`)

`supportsAsyncGrayscaleBase()` returns false (`HalDisplay.cpp:423`), so `overlapRefresh` at
`EpubReaderActivity.cpp:5759` is dead and the two updates are strictly serial. The page also
renders twice on a 532 MHz ARM1136 (`composePageBuffer` then `composeGrayscaleBuffer`,
`EpubReaderActivity.cpp:5772-5784`).

So today the choice is: AA off and one fast refresh, or AA on and a visible BW-then-gray flash on
every page turn. Neither is what stock Kindle does.

**This is unnecessary here.** The plane decomposition exists to drive the ESP32's panel controller
one waveform LUT at a time. The Kindle kernel owns waveforms and the framebuffer is *already* the
4bpp target. `blitToPanel` could compose all four levels in a single pass — the only reason it
cannot is that `GfxRenderer` throws the 2bpp shade away at `GfxRenderer.cpp:851-884` before the HAL
ever sees it.

## Finding 3 — 16 levels are available and nearly free

E Ink Pearl is a 16-level panel and `/dev/fb0` is 4bpp, so every nibble value is addressable; the
stock UI's use of exactly four levels (`HalDisplay.cpp:91-95`, measured) is a UI palette choice,
not a hardware limit.

The converter already has the data: `fontconvert_sdcard.py:731-745` builds a 16-level `pixels4g`
buffer and then immediately thresholds it down to 2 bits. Emitting 4bpp glyphs is deleting the
downsample step plus a format flag. The device-side cost is a doubled glyph bitmap — irrelevant
against 256 MB, decisive against the ESP32-C3's ~400 KB, which is why upstream is 2bpp.

The real work is the blit: `GfxRenderer::renderChar` and `KindleBlit.h` both assume 1bpp sources.

## Finding 4 — point size is not portable across families

| font | `x` bitmap px | mean adv px |
|---|--:|--:|
| Bitter 12 | 14 | 13.46 |
| ChareInk 12 | 12 | 12.52 |
| Bitter 14 | 16 | 15.71 |
| ChareInk 14 | 14 | 14.61 |

ChareInk at 14 renders the same `x` height as Bitter at 12. "Point size" here is only em size at
150 DPI, with no optical normalization, so switching family silently changes apparent size by up to
a step. Worth a per-family scale factor if the family list stays as long as upstream's (24 families
in `lib/EpdFont/scripts/sd-fonts.yaml`).

## Typeface selection — measured

Measured 2026-08-30, not reasoned. 20 candidate faces (every regular style in
`lib/EpdFont/scripts/sd-fonts.yaml` that resolves to a static file, plus the four already vendored
under `builtinFonts/source/`) pushed through the exact shipping pipeline — FreeType at 150 DPI,
`>>4`, darken-aa thresholds (3,6,10) — then scored on the panel's measured palette. Reproduce with
`fontbench/run.sh` — one command, builds its own venv and fetches the corpus. See
`fontbench/README.md`.

Nominal point size is not comparable across families (Finding 4), so every face is compared **at
equal x-height** — the pt step that lands closest to a 12 px x-height, 1.83 mm on this panel. What
you then pay for that x-height is horizontal space.

`stem` is the narrowest fully-black vertical stem across `ilnhbdk`, in px. `blk%` is the share of
inked pixels reaching level 3 (true black) rather than one of the two grays.

| font | pt | ch/line @590 | stem | blk% | fatten AA-off |
|---|--:|--:|--:|--:|--:|
| **LexicaUltralegible** | 11 | 53.2 | 2 | **80.7** | **+10%** |
| **NotoSerif** | 10 | 51.6 | 2 | 76.2 | +13% |
| **Bitter** (incumbent) | 10 | 52.6 | 2 | 71.6 | +17% |
| **LexendDeca** | 10 | 51.6 | 2 | 74.2 | +13% |
| SourceSans3 | 11 | **54.2** | 2 | 74.7 | +23% |
| ChareInk | 12 | 47.1 | 2 | 70.6 | +14% |
| SourceSerif4 | 12 | 46.2 | 2 | 68.7 | +13% |
| Lora | 11 | 49.1 | 2 | 66.2 | +17% |
| Literata | 11 | 46.3 | 2 | 68.7 | +21% |
| GentiumBookPlus | 12 | 49.2 | 2 | 67.0 | +22% |
| Domitian | 12 | 46.1 | 2 | 64.9 | +22% |
| AtkinsonHLNext | 11 | 52.9 | 1 | 67.2 | +19% |
| NotoSans | 10 | 53.4 | 1 | 61.7 | +25% |
| Inter | 10 | 53.9 | 1 | 57.7 | +26% |
| IBMPlexSans | 11 | 51.0 | 1 | 74.3 | +28% |
| IBMPlexSerif | 11 | 47.8 | 1 | 66.1 | +19% |
| Merriweather | 10 | 50.8 | 1 | 65.5 | +20% |
| LibreBaskerville | 10 | 48.2 | 1 | 67.7 | +18% |
| Tinos | 12 | 51.4 | 1 | 68.7 | +20% |
| OpenDyslexic | 10 | **35.4** | 1 | 71.6 | +16% |

Alegreya, Vollkorn and the remaining variable fonts in `sd-fonts.yaml` are **not measured** — they
need `fonttools.instancer` to pin an axis first. Bold and italic are not measured either; only
regular.

### The AA decision picks the winner, not taste

`fatten AA-off` is the finding that reorders this table. Turning AA off does **not** drop the
antialiasing — `GfxRenderer.cpp:871` promotes *every* fringe pixel to solid black, so the glyph is
dilated by its whole AA fringe. Measured over a pangram at size 10, that is +10% ink for Lexica and
+28% for IBM Plex Sans. `fontbench/render_panel_proof.py compare` renders it: the low-fatten faces
look almost identical in both modes, the high-fatten sans faces go clotted and heavy.

So given Finding 2 — AA on costs two refreshes and a double page render — the choice is:

- **AA off** (one fast refresh, the sane near-term default): rank by fatten and blk%.
  **LexicaUltralegible** wins outright: lowest dilation, highest black fraction, stem 2, and still
  53 chars/line. Then NotoSerif, LexendDeca, ChareInk.
- **AA on** (4 levels, two refreshes): rank by measure and stem. SourceSans3 edges it on
  chars/line, with Lexica and Bitter within 1.5 chars.

**LexicaUltralegible is the only face that is top-tier in both regimes.** It is also the safe pick
while Finding 2 is unresolved, because it is the least sensitive to how that resolves.

### Corrections to the reasoned ranking

The list in the first draft of this note was inferred from type-design principles and upstream's
own blurbs. Three parts of it were wrong:

- **Atkinson Hyperlegible Next** was recommended. It has a 1 px stem at these sizes and fattens
  19%. It is designed for signage contrast, not for a 12 px bitmap. Its fork **Lexica
  Ultralegible** measures far better on both counts (stem 2, +10%) — same design lineage, and the
  difference is real and repeatable, not noise.
- **Literata** was recommended as "screen-optimized". It is second-worst on measure among the
  stem-2 faces (46.3 ch/line) and fattens 21%.
- **Bitter**, the incumbent, was called correct-but-mis-stepped. That held up: stem 2, 52.6
  chars/line, +17%. It is a legitimate keep — just at size 10, not 14.

Confirmed as predicted: Tinos (1 px stem even at 12 pt — the Times-like high-contrast failure),
Libre Baskerville and Domitian (thin strokes, poor measure), OpenDyslexic (35 chars/line, unusable
as a default; keep it as the accessibility option it is).

This measures rasterization quality, not reading comfort. It rules faces out on hard evidence; it
cannot rule one in on taste.

## Recommended order of work

1. Retune the ladder to `8/9/10/12` and the margin default. Config-only, no new code, testable on
   device today. Biggest perceived improvement per unit effort.
2. Switch the default family to LexicaUltralegible at size 10. It is the least sensitive to how
   the AA question resolves, so it is safe to land before (3) is decided.
3. Decide the AA question. Either accept AA-off as the K3 default (one fast refresh, dilated
   glyphs), or do (4).
4. Single-pass grayscale: give `GfxRenderer` a 2bpp or 4bpp target on this platform and compose in
   one `blitToPanel`. Kills the double refresh *and* the double page render, and opens the door to
   16 levels. This is the real fix and it is a `GfxRenderer` change, not a HAL change.
5. Only then, 4bpp fonts. Drop the downsample in the converter, add the format flag. Cheap once (4)
   exists, pointless before it.

## Open questions

- Which einkfb update mode on this kernel actually resolves 16 levels, and what it costs in ms.
  `FX_UPDATE_SLOW` is assumed to be the full waveform; unverified.
- Whether stock K3 renders its own text with 4 levels or 16. The histogram at `HalDisplay.cpp:91-95`
  sampled the stock *UI*, which may not be representative of the reader.
- Whether `--darken-aa`'s (3,6,10) is still the right cut once the shade actually reaches the panel
  in one pass rather than through the plane split.
- Whether LexicaUltralegible's bold and italic hold up. Only regular was measured, and a reader
  needs all four styles.
- Alegreya and Vollkorn are unmeasured pending variable-font instancing.
