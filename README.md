# CrossKink

> **A kink in [CrossInk](https://github.com/uxjulia/CrossInk)** — the ESP32
> e-reader firmware, bent onto the **Kindle 3 Keyboard** (D00901, 2010):
> ARM1136JF-S @ 532 MHz, 256 MB RAM, Linux 2.6.26, glibc 2.5.
> Upstream's README is kept as [`README-upstream.md`](README-upstream.md).

It boots, renders, and turns pages on real hardware.

`crosspoint` → `crossink` → `crosskink`  *(one letter further out each time)*

![status](https://img.shields.io/badge/status-runs%20on%20hardware-success)
![target](https://img.shields.io/badge/target-armv6%20%C2%B7%20glibc%202.4-blue)

## What's here that isn't upstream

Two things, and they're independently useful:

1. **[`METHOD.md`](METHOD.md) — running your own code as root on a jailbroken
   Kindle 3.** The usual advice is jailbreak → MKK keystore → KUAL → MRPI. You
   need none of it. A jailbroken Kindle bind-mounts a custom public key whose
   private half ships as `kindletool`'s default signing key, so a package you
   sign yourself runs as root straight from the stock *Update Your Kindle* menu.
   This isn't in any guide we could find, and it's useful for **any**
   legacy-Kindle project, not just this one.

2. **The port** — a Kindle backend for CrossInk's HAL, plus a CMake cross-build,
   because PlatformIO's `native` platform cannot cross-compile.

## Measured hardware facts

Everything below came off the device, not from a datasheet. Raw captures are in
[`gate/results/`](gate/results/).

| | |
|---|---|
| `/dev/fb0` | `eink_fb`, 600x800, **4 bpp packed, 2 px/byte**, `line_length=300` |
| Polarity | **inverted** — nibble `0x0` is white, `0xF` is black |
| Greys | 16 on the panel; CrossInk renders 4 (1bpp base + LSB/MSB planes) |
| Update ioctls | `FBIO_EINK_UPDATE_DISPLAY=0x46DB`, `..._AREA=0x46DD` |
| Waveforms | `partial=0`, `full=1`, `slow=3` |
| Input | `/dev/input/event0..2` — `mxckpd`, `fiveway`, `volume` |
| Toolchain | koxtoolchain `kindle` target → `arm-kindle-linux-gnueabi`, GCC 14.4.0 |

Full keymap: [`gate/results/keymap-measured-2026-08-29.txt`](gate/results/keymap-measured-2026-08-29.txt).

## What we added to upstream's tree

```
platform/kindle/        the port — HalDisplay (framebuffer), HalGPIO (evdev), entry point
cmake/                  cross toolchain file
CMakeLists.txt          replaces platformio.ini; PlatformIO cannot cross-compile
gate/                   the toolchain gate: does C++20 + atomics + threads run on 2.6.26?
patches/                the one patch still needed, against the simulator submodule
scripts-kindle/         bootstrap (submodule pinning + patching), mirror
METHOD.md               the root-execution route
BUILD.md                build environment requirements, including two non-obvious ones
UPSTREAM.lock           the fork point and the two submodule pins
vendor/simulator/       crossink-simulator, pinned — the repo this port substitutes into
freeink-sdk/            upstream CrossInk's own submodule, unchanged
```

Changes to upstream's own files are ordinary commits, so
[the diff against `uxjulia/CrossInk`](https://github.com/uxjulia/CrossInk/compare/main...theanhgen:CrossKink:main)
is the honest record of how much had to change.

## Build

Requires Linux or a container — koxtoolchain does not build on macOS, and
crosstool-NG refuses a case-insensitive filesystem.

```sh
git clone https://github.com/theanhgen/CrossKink
cd CrossKink
scripts-kindle/bootstrap.sh            # pins submodules, applies the simulator patch
ulimit -s unlimited                    # required: 512 MB is not enough for cc1plus
cmake -S platform/kindle -B build -DCMAKE_TOOLCHAIN_FILE=$PWD/cmake/kindle-toolchain.cmake
cmake --build build -j1                # required: parallel builds exhaust memory
```

Produces a ~5.6 MB stripped ARMv6 binary, max symbol version `GLIBC_2.4`.
Both `ulimit -s unlimited` and `-j1` are load-bearing — see [`BUILD.md`](BUILD.md).

## How the port works

CrossInk's "HAL" is not an interface: zero virtual functions, concrete Arduino
and ESP-IDF types, `#include <Arduino.h>` at the top. Platform substitution
happens at **include-path and link time** — `platformio.ini` sets
`lib_ignore = hal` and supplies same-named classes from elsewhere.

So this port substitutes the **simulator's** backends, not the firmware's. The
simulator had already solved POSIX; only two files carry real Kindle work:

| File | Simulator | Kindle |
|---|---|---|
| `HalDisplay.cpp` | SDL window + texture | `mmap` `/dev/fb0`, einkfb update ioctls |
| `HalGPIO.cpp` | `SDL_PollEvent` | `/dev/input/event*` |

Across roughly 230 translation units, **CrossInk's own code needed exactly two
accommodations**: a `strlcpy`/`strlcat` shim, and one stray `#include <SDL.h>`.
That is a remarkably portable codebase, and the credit is upstream's.

## Status

Works: boots, renders 600x800 portrait, page turns, navigation, EPUB rendering
with Czech diacritics, refresh policy driven by CrossInk's own page-count setting.

Known gaps:
- Text entry uses the on-screen picker; the K3's physical QWERTY isn't wired up yet.
- The 5-way d-pad is mapped onto a button model designed for a device without one.
  Shared UI aliases Left/Right onto Up/Down (`src/components/OptionPopup.h`) and
  cycles Settings tabs with Confirm (`src/activities/settings/SettingsActivity.cpp`),
  both of which read wrong on a real d-pad.
- 4 grey levels, not the panel's 16.
- EPUBs that lay text out in very large HTML tables render only the first 64 rows
  (`MAX_TABLE_ROWS_PER_FRAGMENT`). Some Project Gutenberg editions do this.
- No SSH/USBNetwork yet; deployment is via a signed `FC02` package.

## Scope and etiquette

**Nothing here is proposed upstream.** A Kindle target is explicitly out of scope
for both CrossInk and CrossPoint, so please don't open PRs against `uxjulia/*` or
`Free-Ink/*` on account of this fork. Every change here is ours to maintain.

## Credits

This is someone else's work with a Kindle backend bolted on.

- [CrossInk](https://github.com/uxjulia/CrossInk) — MIT, © Dave Allie —
  itself a fork of [crosspoint-reader](https://github.com/crosspoint-reader/crosspoint-reader)
- [crossink-simulator](https://github.com/uxjulia/crossink-simulator) — MIT, © Julia Nguyen
- [freeink-sdk](https://github.com/Free-Ink/freeink-sdk) — MIT, © FreeInk
- [KindleTool](https://github.com/NiLuJe/KindleTool) and
  [koxtoolchain](https://github.com/koreader/koxtoolchain)
- yifanlu's 2010 Kindle jailbreak, still doing its job 16 years later

## Licence

MIT throughout, including the port additions — see [`LICENSE`](LICENSE),
which is upstream's and which we have not modified.
