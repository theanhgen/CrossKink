# platform/kindle/

The Kindle backend. **Unblocked 2026-08-28 — the gate passed, 0 failures.**

Run on-device via a self-built FC02 update package signed with the jailbreak key (no SSH needed).
Full output: `gate/results/gate-result-2026-08-28.txt`.

Confirmed on the device: kernel `2.6.26-rt-lab126` armv6l, **glibc 2.5**, `__cplusplus = 202002`,
`std::thread`/`condition_variable`, `std::atomic` (lock-free `atomic<int>`), exceptions + RTTI,
`clock_gettime(MONOTONIC)`, `std::filesystem` over `/mnt/us`, `std::locale("C")`.

Per README.md: *"The toolchain gate decides everything. Do not write backend code before it
passes."* It passed. Backend code may now be written.

**Written 2026-08-28.** Both backends compile clean for `arm-kindle-linux-gnueabi` with
`-std=gnu++2a -Wall -Wextra` (zero warnings from these files):

| File | Lines | Object |
|---|---|---|
| `HalDisplay.cpp` | 482 | 12,284 B |
| `HalGPIO.cpp` | ~250 | 7,884 B |
| `EInkDisplay.h` | 63 | geometry stub, shadows the simulator's |
| `KindleKeys.h` | — | keycode map, **codes unverified** |
| `tools/keydump.c` | — | on-device keycode probe, built as `keydump-arm` |

They implement the **simulator's** `HalDisplay.h` / `HalGPIO.h` (a superset of CrossInk's
`lib/hal` headers), since the fork carries the simulator's shims and main loop.

All three unknowns are now settled on hardware:
- **Panel polarity** — INVERTED. The palette is the panel's own `0x0/0x5/0xA/0xF`, sampled from
  the stock UI rather than guessed. `grayscale=1` in `fb_var_screeninfo` implies higher = brighter
  and is misleading here.
- **Nibble order** — high nibble is the left pixel; confirmed by clean glyph rendering.
- **Keycodes** — measured one key at a time with `tools/keyguide.c`.

Also fixed: `GfxRenderer` defaults to `Orientation::Portrait`, which rotates 90 deg to get a
portrait UI from a landscape panel. This panel is natively portrait, so `kindle_main.cpp` selects
the identity mapping (`LandscapeCounterClockwise`) instead.

Original plan follows.

When it passes, this holds same-named replacements for the simulator's SDL-backed sinks:

| File | Replaces | Job |
|---|---|---|
| `HalDisplay.cpp` | simulator SDL window/texture | mmap `/dev/fb0`, einkfb update ioctls |
| `HalGPIO.cpp` | simulator `SDL_PollEvent` | `/dev/input/event*` -> QWERTY, 5-way, page bars |

The other seven `Hal*` classes come from the simulator unchanged. Public APIs must match
byte-for-byte: substitution is link-time, not polymorphic.

## Device facts, measured 2026-08-28

Full dump: `gate/results/recon-result-2026-08-28.txt`.

### Framebuffer — `/dev/fb0`, driver id `eink_fb`

| Field | Value |
|---|---|
| `xres` x `yres` | **600 x 800** (portrait, `rotate = 0`) |
| `bits_per_pixel` | **4** — 2 px per byte |
| `line_length` | **300** (= 600 / 2) |
| `smem_len` | 483328 (visible plane is 300 x 800 = 240000) |
| `grayscale` | 1 |
| `type` / `visual` | 0 / 5 |

Confirms the 4bpp correction in PROJECT.md against real hardware. `/proc/eink_fb/` also exposes
a writable `update_display`, plus `waveform_version`, `temperature` and `recent_commands` —
a refresh path that does not require the einkfb ioctls.

### Input — all `EV_KEY`

| Node | Name | Role |
|---|---|---|
| `/dev/input/event0` | `mxckpd` | QWERTY keypad **and** the page-turn bars (one device, wide KEY bitmask) |
| `/dev/input/event1` | `fiveway` | 5-way controller |
| `/dev/input/event2` | `volume` | volume rocker |

There is no separate page-bar device — `HalGPIO` reads the bars off `event0`.

### Constraints that shape the port

- **`/mnt/us` is `fuse.fsp`** over `/dev/loop/0` (vfat), and the backing mount `/mnt/base-us` is
  **`noexec`**. Binaries cannot be executed from the USB volume; copy to `/tmp` (tmpfs) and
  `chmod +x` there. Both update packages already do this.
- Rootfs `/dev/mmcblk0p1` is **82% full — 109 MB free**. Install to `/mnt/us` (2.3 GB free).
- 256 MB RAM, ~70 MB free at idle.
- Userland is **BusyBox v1.7.2**; `/bin/sh -> busybox`. No `grep -E`, no bash-isms.

### Correction to the reuse plan

CLAUDE.md says the simulator's unpack path at `crossink-simulator/src/HalDisplay.cpp:207-281`
is "reusable verbatim — only the final write target changes." **That is wrong.** That path
unpacks CrossInk's 1bpp buffer to **ARGB32** for an SDL texture. The Kindle wants **4bpp packed,
2 px/byte**. The bit-extraction half (`getBit`, the plane walk) still applies; the pixel-emit
half must be rewritten to pack nibbles. Not hard, but it is not a no-op.

### Jailbreak — confirmed by direct inspection, not inference

All four files `3.2.1-jb.sig:128` guards on are present, all dated **2011-04-06**:
`/etc/init.d/jailbreak`, `/etc/rc5.d/S64jailbreak`, `/etc/rc3.d/K09jailbreak`,
`/etc/uks/pubprodkey01.hack.pem`. And the custom key is bind-mounted over the production key
right now:

```
/dev/root /etc/uks/pubprodkey01.pem ext3 ro,noatime,nodiratime,data=ordered 0 0
```
