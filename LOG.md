# Engineering log

The chronology of the CrossKink port: every dead end, wrong diagnosis and superseded plan,
kept verbatim in the order it happened. `README.md` holds current state; this holds how it
got there.

Read it for the failure modes. Several entries below are **wrong** — they are marked, not
edited, because the corrections are the useful part. In particular:

| Entry | What it claimed | What was actually true |
|---|---|---|
| [§9-ALERT](#9-alert-device-state-2026-08-27--currently-un-jailbroken) | the device was un-jailbroken | it was never un-jailbroken; yifanlu's 2010 jailbreak was live the whole time |
| [ROOT CAUSE FOUND](#root-cause-found-2026-08-27--jailbreak-013n-requires-fw-343-not-342) | 0.13.N needs FW 3.4.3 | irrelevant — 0.13.N aborts *by design* when a jailbreak is already present |
| [Critical path](#critical-path-correction-2026-08-27--usbnetwork-not-kual) | USBNetwork is the one required flash | no flash chain is required at all; a self-signed FC02 package runs root code directly |
| [Prerequisite chain](#prerequisite-chain-with-real-status) | KUAL needs MKK + renewed dev certs | true, and entirely unnecessary — see `METHOD.md` |
| §4 / §5.5 | framebuffer is 8bpp, geometry unknown | 4bpp packed, 2 px/byte, 600x800 native portrait, inverted polarity |

---

## 2026-08-26 — host environment and the toolchain question

### 5.0 Host environment — resolved 2026-08-26

Before the gate can even be attempted, the build host had to be sorted. Findings:

| Check | Result |
|---|---|
| PlatformIO | installed, 6.1.19 |
| CMake | installed, 4.4.3 |
| SDL2 | `sdl2-compat` 2.32.70 present (SDL2 API over SDL3) |
| **Root filesystem** | **case-insensitive APFS — crosstool-NG refuses to build** |
| Docker | not installed |
| GNU tools (`gsed`/`gawk`/`makeinfo`/`help2man`/`wget`) | missing |
| koxtoolchain host support | *"Only actively tested on Linux hosts"* — no Dockerfile provided |

**Correction:** the koxtoolchain target is **`kindle`** (`arm-kindle-linux-gnueabi`), not
`kindle-legacy`. `gen-tc.sh` accepts kindle, kindle5, kindlepw2, kindlehf, kobo, kobov4, kobov5,
nickel, remarkable, remarkable-aarch64, cervantes, pocketbook, pocketbookhf, bookeen — there is
no `kindle-legacy`. That name is KOReader's *build* target, which consumes this toolchain.

**Chosen route: macOS-native, on a case-sensitive sparse disk image.** A container was the first
instinct but is not actually required — crosstool-NG documents a macOS path, and the standard
fix for case sensitivity is a sparse image rather than a new volume or a VM:

```bash
hdiutil create -size 40g -fs "Case-sensitive APFS" -type SPARSE -volname xtc xtc
hdiutil attach xtc.sparseimage      # -> /Volumes/xtc
```

Created at `vendor/xtc.sparseimage`, mounted at `/Volumes/xtc`, case-sensitivity verified.
Sparse, so it consumes only what it uses; deleting the file undoes everything.

**macOS friction encountered (the "some efforts" the README warns about).** Apple ships 2006-era
GNU tooling; each of these had to be installed from Homebrew before ct-ng would run:

| Tool | Apple's version | Why it fails |
|---|---|---|
| `make` | GNU Make **3.81** | ct-ng needs 4.x |
| `/bin/bash` | **3.2.57** | `gen-tc.sh` is `#!/bin/bash -e`; ct-ng needs bash 4+ |
| `grep`, `tar` | BSD | GNU-only flags |
| `libtool` | Apple's, unrelated tool | needs GNU libtool (`glibtool`) |

plus `gnu-sed`, `gawk`, `texinfo`, `help2man`, `wget`, `gperf`, `ncurses`.

**Fallback if this stalls:** a Linux container (colima/Docker) collapses all of the above into a
tested environment. Revisit if ct-ng fails on host-specific grounds rather than config grounds.

### RESOLVED 2026-08-27 — do not build the toolchain at all

**koxtoolchain publishes prebuilt toolchains as GitHub release assets.** Verified against the
API, not inferred:

```
release 2026.08, published 2026-08-02
  kindle.tar.zst   95.3 MB   <- arm-kindle-linux-gnueabi, the legacy K2/DX/K3 target
```

Downloaded to `/Volumes/xtc/kindle.tar.zst`. Verified contents:

- Ships **GCC 14.4.0** (`libexec/gcc/arm-kindle-linux-gnueabi/14.4.0/`) — full C++20, confirming
  5.1's compiler question is closed.
- Extracts as `x-tools/arm-kindle-linux-gnueabi/...`, i.e. straight into `~/x-tools/`.
- **Host arch: `ELF 64-bit LSB executable, x86-64 ... for GNU/Linux 3.2.0`.** It cannot run on
  macOS (`exec format error`).

**Consequence: the zlib / clang-21 / darwin27 failure above is now irrelevant.** We never build
crosstool-NG. A Linux container is still required, but only to *execute* a prebuilt compiler —
a vastly smaller and more reliable job than building one. On Apple Silicon, x86_64 wants
`colima start --vz --vz-rosetta` for near-native speed.

The sparse case-sensitive image is still useful as the container's build tree. Nothing wasted.

Source: <https://github.com/koreader/koxtoolchain/releases>

### 5.1 Toolchain — C++20 on ARMv6 / glibc / kernel 2.6.26  ← **the gate**

The build requires `-std=gnu++2a`. The simulator uses `std::atomic` and a render-thread /
main-thread split.

**Compiler half: resolved 2026-08-26, in our favour.** The `kindle` target's sample config
(`benoit-pierre/crosstool-ng` @ `34844bc8`, `samples/arm-kindle-linux-gnueabi/crosstool.config`)
is a *minimal defconfig* — it does **not** pin a GCC version, so ct-ng picks its default, and
that fork ships GCC **4.9.4 … 14.4.0**. A GCC with complete C++20 is available. "Can we even get
a new enough compiler" is no longer an open question.

Verified contents of that config:

| Key | Value | Note |
|---|---|---|
| `CT_ARCH_CPU` | `arm1136jf-s` | exact K3 CPU |
| `CT_ARCH_FPU` / float | `vfp`, `CT_ARCH_FLOAT_SOFTFP` | softfp ABI |
| `CT_TARGET_CFLAGS` | `-mno-unaligned-access` | ARMv6 erratum guard |
| `CT_GLIBC_V_2_9` | glibc **2.9** | *build-side* |
| `CT_GLIBC_MIN_KERNEL_VERSION` | `2.6.22` | K3 runs 2.6.26 — inside the window |
| `CT_LINUX_V_2_6_27` | kernel headers 2.6.27 | |
| `CT_CC_LANG_CXX` | `y` | |
| `CT_CC_GCC_STATIC_LIBSTDCXX` | **not set** | §5.1's escape hatch is a knob, currently off |
| `CT_TOOLCHAIN_PKGVERSION` | `"NiLuJe"` | same author as the jailbreak — cf. §8 |

**The risk moved, it did not go away.** It is no longer the compiler; it is the **runtime symbol
surface**. `gen-tc.sh`'s `kindle)` branch exports `glibcxx_cv_utimensat=no` with the comment
*"Prevent libstdc++ from pulling in utimensat@GLIBC_2.6"* — a workaround that exists precisely
because the device's glibc predates 2.6, even though the toolchain builds against 2.9. Exactly
one such symbol is patched by hand. C++20's larger library surface may reach for others, and
each one is a link-time or run-time failure on device.

So the gate's real question is now: **does a C++20 binary using `std::atomic` + threads link and
run on the K3**, not *can we compile it*. Flipping `CT_CC_GCC_STATIC_LIBSTDCXX` is the first
lever if it does not.

**Still the project's go/no-go, and still cheap to answer.**

### GATE: COMPILE HALF PASSED 2026-08-27 — and the symbol audit is clean

Cross-compiled `gate/gate.cpp` (106 lines: C++20 language features, `std::thread` +
`std::atomic` + `condition_variable`, throw/catch + RTTI, `std::filesystem`, `clock_gettime`,
`std::locale`) with the prebuilt toolchain inside an amd64 container:

```
arm-kindle-linux-gnueabi-g++ (crosstool-NG UNKNOWN - NiLuJe) 14.4.0
gate-arm: ELF 32-bit LSB executable, ARM, EABI5 version 1 (SYSV),
          dynamically linked, interpreter /lib/ld-linux.so.3, for GNU/Linux 2.6.22
```

Linked `-static-libstdc++ -static-libgcc -lpthread -lrt`.

| Audit | Result |
|---|---|
| **Max GLIBC symbol version required** | **`GLIBC_2.4`** |
| Forbidden 2.6.27-era syscalls (`accept4`, `pipe2`, `epoll_create1`, `eventfd2`, `dup3`, `inotify_init1`) | **none** |
| Target kernel floor | 2.6.22 — device runs 2.6.26 |

**This is the headline result of the whole investigation.** 5.1 framed "C++20 against 2010-era
glibc" as the project's go/no-go. The binary demands **nothing newer than GLIBC_2.4**, which is
*below* even the most pessimistic estimate of the device's glibc (~2.5), and below the 2.9 the
toolchain builds against. Static-linking libstdc++ did the heavy lifting: all the modern C++
runtime is inside the binary, leaving only ancient, stable libc entry points.

**What remains unproven:** that it *executes*. Symbol analysis is strong evidence, not proof —
runtime behaviour on the actual kernel is the last unknown, and it needs the device. But the
failure mode 5.1 feared (unsatisfiable symbol versions) is now ruled out statically.

### FBInk cross-compiled 2026-08-27 — toolchain validated on a real codebase

`make kindle` (KINDLE=true; `LEGACY=true` is a FW-2.x CLOEXEC workaround, not needed on 3.4.2)
against FBInk @ `886f25f1`:

```
Release/fbink: ELF 32-bit LSB executable, ARM, EABI5, for GNU/Linux 2.6.22, stripped  [1.3 MB]
  max GLIBC symbol version : GLIBC_2.4
  forbidden 2.6.27 syscalls: none
```

Identical clean audit as the gate, but on ~20k lines of real C with aggressive warning flags,
not a 106-line test. This validates the toolchain far more convincingly.

**It is also a diagnostic tool.** FBInk drives legacy einkfb directly, so the moment SSH works it
can prove the display path — geometry, bpp, refresh modes — before a line of `HalDisplay` is
written. Binaries staged at `gate/bin/` (`fbink`, `gate-arm`), ready to `scp`.

**Reproduce:**
```bash
docker build --platform linux/amd64 -t kindle-xc ~/kindle-xbuild   # toolchain baked in
docker run --rm --platform linux/amd64 -v "$PWD/gate":/work/gate kindle-xc \
  bash /work/gate/build.sh
```
Note: extract the toolchain **inside** the image, never onto a bind mount — koxtoolchain's
tarball has read-only directories (`r-xr-xr-x`) that tar cannot populate over virtiofs.

---

## 2026-08-19 to 08-26 — effort estimate and device prerequisites

## 6. Effort

| Stage | Estimate |
|---|---|
| Toolchain + hello-world on device (**go/no-go**) | 1 day |
| CMake cross-build of full tree, links and runs headless | 2–4 days |
| fbdev `HalDisplay`, first pixels | 1–2 days |
| `/dev/input` `HalGPIO` + K3 key mapping | 2–3 days |
| Refresh tuning to not feel worse than stock | iterative, days |
| KUAL packaging | half a day |

**First light: 1–2 weeks of focused evenings. Daily-driver: a month plus.**

---

## 7. Device prerequisites

### Software

All standard, well-documented on MobileRead:

1. **Check firmware version first** — the K3 jailbreak path is version-specific.
2. Jailbreak
3. USBNetwork (gives SSH over the USB cable — this is the dev loop)
4. KUAL (launcher for the packaged app)

### Hardware

**Replacement battery** — the K3 shipped in 2010, so the original cell is ~16 years old.
This matters more for development than for reading: the dev loop keeps the device awake,
driving the panel and holding a USB network link far longer than normal use.

**Part numbers** — the OEM cell is **S11GTSF01A**, 3.7 V 1750 mAh. Also listed as
**170-1032-00** / **170-1032-01** and **GP-S10-346392-0100**; searching those alternates gets
better hits on EU sites. Confirm D00901 compatibility — the Kindle 3 / Keyboard cell differs
from the Kindle 4 and Touch.

**CZ vendors:**

| Vendor | Capacity | Price | Link |
|---|---|---|---|
| **Sunnysoft** ← **chosen** | **1900 mAh** | **320 Kč** incl. VAT | [product](https://www.sunnysoft.cz/z/514PCS-1689/baterie-pro-amazon-kindle-3-1900mah.html) |
| baterie24.cz | 1750 mAh (OEM spec) | ~421 Kč | [product](https://www.baterie24.cz/baterie-pro-ipod-mp3-hry/baterie-pro-amazon-kindle-3-typ-s11gtsf01a/dV903) |
| MediaOutlet.cz | 1900 mAh | — | [product](https://www.mediaoutlet.cz/Informatika/Baterie-pro-tablety/Baterie-pro-Amazon-Kindle-3-Graphite-1900-mAh-p25978c98c103) |
| baterie-pro.cz | 3500 mAh (Cameron Sino CS-ABD003XL) | — | [product](https://www.baterie-pro.cz/CS-ABD003XL.html) |

**Status: INSTALLED 2026-08-26.** Sunnysoft order **č. 3281960474**, paid, delivered via
Zásilkovna. Cell fitted, device powers on and boots to the stock UI. Back cover is clipped, not
glued — repair went as documented.

> TODO: record which variant shipped (1900 mAh @ 320 Kč or 3500 mAh CS-ABD003XL @ 409 Kč) — the
> confirmation page did not list line items, but the label on the installed cell now settles it.
> If it is the 3500: expect the fuel gauge to misreport and charging to take ~8 h (see above).

**All three cells share the OEM footprint: 96 × 67 × 4 mm (25.7 cm³).** Fit is not a
differentiator — any of them drops in.

**Buy from Sunnysoft** — both capacities are stocked there, so it is one vendor and one order.
The `baterie-pro.cz` listing is the identical Cameron Sino part at the identical price; no
reason to split the order.

| | [Sunnysoft 1900](https://www.sunnysoft.cz/z/514PCS-1689/baterie-pro-amazon-kindle-3-1900mah.html) | [Sunnysoft 3500](https://www.sunnysoft.cz/z/514PCS-3174/baterie-pro-amazon-kindle-3-3500mah-velkokapacitni.html) |
|---|---|---|
| Part | Cameron Sino | Cameron Sino **CS-ABD003XL** |
| Capacity | 1900 mAh @ 3.7 V = 7.03 Wh | 3500 mAh @ **3.80 V** = 13.30 Wh |
| Energy density | 273 Wh/L | 517 Wh/L |
| Price | 320 Kč | 409 Kč |
| + Zásilkovna 59 Kč | **379 Kč** | **468 Kč** |
| Warranty | 12-mo capacity guarantee | 24-mo + 12-mo capacity |
| Stock | 5+ | 5+ |

Both list all four part numbers (`S11GTSF01A`, `170-1032-00`, `170-1032-01`,
`GP-S10-346392-0100`). Note the 3500 listing states 3.7 V in one field and 3.8 V in another —
same part number as baterie-pro's, which states 3.80 V, so assume 3.8 V.

**On the 3500 mAh claim.** 517 Wh/L is at the upper edge of achievable — premium silicon-anode
cells reach ~700 Wh/L, good commodity pouches ~400–500 — so it is not impossible, just
optimistic at this price. The real catch is **3.80 V nominal**: such cells are designed to
charge to ~4.35 V, while the K3's 2010 charge controller targets a 4.2 V cell and will not go
higher. Expect materially less than 3500 mAh in this device regardless of label honesty.

**Is the higher capacity safe? Yes.** Capacity is not a stressor — the device draws what it
draws; a larger cell simply supplies it longer. The voltage mismatch errs safe: a 3.8 V cell
charged to only 4.2 V is *under*charged, which is gentler on the chemistry. Both packs carry
integrated protection (overcharge, over-discharge, reverse polarity).

Three practical consequences of going 3500, none harmful:

1. **~2× charge time.** The K3 charges at a fixed ~500 mA over USB. Empty-to-full goes from
   roughly 4 h to 8 h+. Matters less here since the dev loop keeps it plugged in.
2. **The fuel gauge will misreport.** It is calibrated for 1750 mAh @ 3.7 V. Expect "full" to
   persist implausibly long, a nonlinear drop near the end, and possibly a low-voltage cutoff
   while the UI still shows charge remaining.
3. **Realistic yield ≈ 2500–3000 mAh**, not 3500, because of the 4.2 V ceiling. Still beats a
   full 1900.

**Either is defensible.** Reference: OEM is 252 Wh/L, exactly right for 2010 Li-Po — a useful
sanity anchor when judging any other listing's capacity claim.

Replacement guide: [MPF Products, D00901 walkthrough](https://www.mpfproducts.com/blog/how-to-replace-the-s11gtsf01a-battery-for-your-kindle-3-keyboard-d00901/).
The K3 back cover is clipped, not glued — low-risk repair.

> Note: the originally-saved link was `https://www.baterie-pro.cz/kosik` — a **shopping cart**
> URL, i.e. session state, not a product page. Replaced above with the durable product link.

---

## 2026-08-27 — the jailbreak panic (both diagnoses below are wrong)

## 9-ALERT. DEVICE STATE 2026-08-27 — CURRENTLY UN-JAILBROKEN

**Read this before touching the device.**

Sequence attempted this evening:

| # | Action | Result |
|---|---|---|
| 1 | `Update_usbnetwork_0.57.N_k2_dx_k3_install.bin` | **FAILED** — generic "not successful", no U006 |
| 2 | `Update_jailbreak_0.13.N_k3gb_uninstall.bin` | **SUCCESS** — 2010 jailbreak removed |
| 3 | `Update_jailbreak_0.13.N_k3gb_install.bin` | **FAILED** — generic "not successful" |
| 4 | same file, after full Restart | **FAILED** |
| 5 | same file, after Restart **+ `linkss/` renamed to disable the 2010 hack** (verified not running: stale PID, no mount marker) | **FAILED** |
| 6 | `Update_usbnetwork_0.57.N_k2_dx_k3_install.bin` again | **FAILED** — confirms 3-5 did not take |

| 7 | Firmware 3.4.2 -> **3.4.3** (official Amazon B00A file) | **SUCCESS** — confirmed on Settings screen |
| 8 | `Update_jailbreak_0.13.N_k3gb_install.bin` on **3.4.3**, airplane mode on, hash re-verified on device | **FAILED** — generic, no error code |

**Every hypothesis exhausted.** Verified: correct variant (device id `0x0A` = B00A = k3gb, read
from the OTA header at offset 0x0C), correct firmware (3.4.3 on screen), correct file (md5
`6f98c96f...` matching the archive, re-checked *on the device*), correct procedure, wireless off,
no conflicting jailbreak, working updater. Fails silently with no error code.

**Mechanism note from the package source:** `src/build-updates.sh` packs `3.1-install.sh` into the
OTA tarball under deliberately mangled names (`"updatedat"`, `"update\dat"`, `"3.2.1-jb.sig"`),
i.e. the exploit is signature-filename resolution trickery, credited to yifanlu / serge_levin.
It does **not** depend on a pre-installed key, so a stock device should in principle be
jailbreakable — which refutes the earlier worry that uninstalling destroyed the only route in.

**STOPPED FLASHING after step 8.** Three attempts under three distinct conditions is evidence,
not bad luck. Escalated to MobileRead instead — see `MOBILEREAD-POST.md`.

**The device is therefore a stock, un-jailbroken Kindle 3 on FW 3.4.2 right now.** Not bricked;
stock firmware, library and Whispernet all fine. Books backed up at `~/kindle-backup-2026-08-27/`.

**Why step 1 failed:** USBNetwork 0.57.N requires jailbreak **>= 0.11.N** (`README.txt:98`). The
device carried a Nov-2010 jailbreak, below that floor.

**Why the uninstall was attempted:** `README.txt:31` — updating the jailbreak on a 3.x device is
one of the documented cases requiring the uninstaller first, and the v0.9.N changelog confirms
the installer aborts if another jailbreak is present.

**Judgement error to record:** the uninstall was recommended without first confirming the
jailbreak could be *re-applied* on 3.4.2. Surviving firmware updates is not the same as being
installable fresh. The risk window between steps 2 and 3 was called out beforehand, but should
have been closed by verifying re-installability first.

**Hypotheses tested and ELIMINATED:**
- *Reboot needed between uninstall and install* — tested (step 4), failed.
- *Old 2010 hack blocking the installer* — tested (step 5) with the hack verifiably not running
  (`usb-watchdog.pid` stale, no `mounted_*` marker created). Failed.
- *Update files being intercepted* — disproved: the **successful** uninstall's `.bin` was archived
  to `linkss/` too, so archiving happens post-application and blocks nothing.

### ROOT CAUSE FOUND 2026-08-27 — jailbreak 0.13.N requires FW **3.4.3**, not 3.4.2

From [MobileRead t=372804](https://www.mobileread.com/forums/showthread.php?t=372804), a user in
the identical situation (K3, FW 3.4.2, jailbreak failing):

> *"You have to pull up the firmware version all the way to **3.4.3** for the currently available
> jailbreak (0.13N) to install."*

**0.13.N does not install on 3.4.2 from stock.** All three failures were a firmware version
requirement, not device state — restarts and disabling the 2010 hack were never going to help.
The original 2011 jailbreak worked because 3.1 was vulnerable; it then *persisted* across updates.
Persisting is not the same as being installable.

**Corrected sequence:**

| # | Action |
|---|---|
| 1 | Update firmware **3.4.2 -> 3.4.3** (Amazon-signed; the stock updater works, proven by the successful uninstall) |
| 2 | `Update_jailbreak_0.13.N_k3gb_install.bin` |
| 3 | `Update_usbnetwork_0.57.N_k2_dx_k3_install.bin` |

3.4.3 sources: Amazon's official update page (choose the **B00A** file — "Kindle Keyboard 3G for
Europe"), or simply disable airplane mode and let Whispernet deliver it.

**Note the reversal:** airplane mode was enforced all session to *prevent* an OTA to 3.4.3.
3.4.3 turned out to be the prerequisite. The OTA was never the threat.

**Superseded — untested avenue previously worth raising:** K3 had several historical jailbreak methods (LanguageBreak,
WatchThis, KindleBreak) beyond NiLuJe's. If 0.13.N genuinely cannot apply on a stock 3.4.2, one
of those may be the route back. **Do not attempt without confirmation from MobileRead.**

**Superseded hypothesis for step 3's failure:** a reboot is needed between uninstall and install.
Changelog v0.6.N reads *"Fix uninstall so that it immediately switches back to default keys
**(on FW 3.1)**, instead of requiring a reboot"* — that fix is scoped to 3.1, so 3.4.2 may still
require the reboot. **Next action: full Restart, then retry step 3 unchanged.**

Reassurance on feasibility: the package README explicitly supports 3.3.x/3.4.x
(`README.txt:66`), so 3.4.2 is a supported jailbreak target, not a patched-shut one.

**If retry-after-restart fails**, do NOT keep re-flashing. Downgrading is not an escape route —
it requires an already-jailbroken device with KUAL
(<https://kindlemodding.org/firmware-and-flashing/downgrading/>). Ask on MobileRead instead.

---

## 9a. Resume here — state as of 2026-08-26

**Paused waiting on a USB data cable.** The cable on hand is charge-only; the device does not
enumerate (`ioreg -p IOUSB` shows no Amazon `0x1949`). Nothing on-device can proceed without it.

### What exists on disk

| Path | What |
|---|---|
| `vendor/CrossInk` | 356 MB, submodules populated (incl. `freeink-sdk`) |
| `vendor/crossink-simulator` | 1.3 MB |
| `vendor/xtc.sparseimage` | case-sensitive APFS sparse image, mounts at `/Volumes/xtc` |
| `/Volumes/xtc/koxtoolchain` | koxtoolchain clone; build tree under `build/` |
| `~/x-tools` | **symlink** → `/Volumes/xtc/x-tools` (toolchain install prefix) |

`vendor/` is gitignored. If `/Volumes/xtc` is not mounted after a reboot:
`hdiutil attach vendor/xtc.sparseimage`.

### Host tooling installed

PlatformIO 6.1.19, CMake 4.4.3, and via Homebrew: `make` (4.4.1), `bash` (5.3.15), `gnu-sed`,
`gawk`, `grep`, `gnu-tar`, `libtool`, `texinfo`, `help2man`, `wget`, `gperf`, `ncurses`,
`binutils` (2.47). `sdl2-compat` 2.32.70 was already present.

**Build driver:** `scratchpad/build-tc.sh` sets PATH and runs `./gen-tc.sh kindle`.
The PATH ordering is load-bearing: GNU tools first, **`binutils` last**. binutils ships
`ld`/`as`/`ar`/`ranlib`/`nm`/`strip` that would shadow Apple's and break linking macOS host
binaries; `objcopy`/`gobjcopy` have no Apple equivalent so they still resolve from the tail.
Getting this wrong is a confusing mid-build failure.

### Build attempts — 2 failures, 1 fixed, 1 blocking

**#1 — fixed.** `configure: error: missing required tool: gobjcopy objcopy`. macOS ships no GNU
objcopy. `brew install binutils` plus the PATH-tail rule above.

**#2 — BLOCKING, unresolved.** Got much further: ct-ng built, toolchain configured, companion
libs started. Died at **`Build failed in step 'Installing zlib for host'`**:

```
/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk/usr/include/_stdio.h:322:7:
    error: expected identifier or '('
make[1]: *** [Makefile:180: zutil.o] Error 1
```

`_stdio.h:322` is a `__DARWIN_ALIAS_STARTING(...)` declaration of `fdopen`. Confirmed *not* a
stray GNU gcc — `gcc` resolves to `/usr/bin/gcc` = **Apple clang 21.0.0**, target
`arm64-apple-darwin27.0.0`. This is **version skew**: crosstool-NG 1.26's bundled zlib against a
2026 SDK/clang. Not a missing package, so there is no clean "install X" fix; options are
patching zlib, forcing a newer one, or fighting feature-macro flags — all guesswork.

Known-issues list: <https://crosstool-ng.github.io/docs/known-issues/>

**Recommendation: switch to a Linux container** (`colima` + docker, or Docker Desktop) and build
koxtoolchain there. Rationale: the macOS-native route was worth trying — it is documented and it
avoided a VM — but it has now cost seven Homebrew packages, a disk image, and two failures, and
the current one is a version-skew bug rather than a missing dependency. koxtoolchain is
"only actively tested on Linux hosts" and this is what that sentence buys you.

The sparse image is **not wasted** if switching: it can host the container's build tree, and it
already solves case-sensitivity. Nothing above needs undoing.

### USBNetwork runbook — package verified 2026-08-27, NOT yet flashed

Package: `kindle-usbnetwork-0.57.N-r18979.tar.xz`, **md5 `de69e4fc9e826cc13609879dc661f9a7`
verified**. Extracted at `~/kindle-xbuild/usbnet/`.

**The k3g/k3gb question was a false problem.** The archive contains exactly three `.bin` files:

```
Update_usbnetwork_0.57.N_k2_dx_k3_install.bin   <- ONE file for all K2/DX/K3 variants
Update_usbnetwork_0.57.N_k4_install.bin
Update_usbnetwork_0.57.N_uninstall.bin          <- clean uninstall exists
```

No per-variant split at all. Neither earlier source was right.

**Install method: classic, no KUAL/MRPI needed.** `README_FIRST.txt:22` warns that snapshot
packages "need to be installed via MRPI", but that applies to 5.x devices — the current
`README.txt:158` documents the classic flow explicitly for FW 3.x/4.x. **This confirms the
one-flash plan: KUAL stays off the critical path.**

1. Copy `Update_usbnetwork_0.57.N_k2_dx_k3_install.bin` to the **volume root**
2. Eject **and unplug** (README: *"DO NOT reboot your device with a custom .bin in the root"*)
3. `[HOME] -> [MENU] -> Settings -> [MENU] -> Update Your Kindle`
4. May appear to hang ~1-2 min on "Update successful, restarting" on a first install — normal

**Enable, via the search bar — no KUAL required:**

```
;debugOn        <- bring up search with [DEL] on a keyboard Kindle
~usbNetwork     <- toggles usbnet on
;debugOff
```

**CORRECTION — the IPs I quoted earlier were the K4's.** For a K3 the defaults are:

| | |
|---|---|
| HOST (Mac) | `192.168.2.1` |
| KINDLE | `192.168.2.2` |
| connect | `ssh root@192.168.2.2` |

`192.168.15.201/244` is **K4-only**. On macOS the interface must be configured by hand in
Network settings (the README links a MobileRead post for this).

Config lives at `usbnet/etc/config` (UNIX line endings, mandatory). Pubkeys go in
`usbnet/etc/authorized_keys`. Do not enable auto-on-boot until everything is verified working.

### Jailbreak runbook — prepared 2026-08-27, NOT executed and NOT needed

Bundle: **`kindle-jailbreak-0.13.N.zip`** (dated 2015-08-19; this *is* current, the hack is
long-stable). Source: NiLuJe, [MobileRead t=88004](https://www.mobileread.com/forums/showthread.php?t=88004).

**Variant selection — decided by serial number prefix, not by guessing:**

| File | Device | Serial prefix |
|---|---|---|
| `Update_jailbreak_0.13.N_k3g_install.bin` | K3 3G US/Canada | **B006** |
| `Update_jailbreak_0.13.N_k3gb_install.bin` | K3 3G UK/Europe | **B00A** |
| `Update_jailbreak_0.13.N_k3w_install.bin` | K3 WiFi-only | B008 |

**RESOLVED 2026-08-27 — this device is `k3gb`.** Read straight off the USB descriptor when the
cable was first connected, no need for the Settings screen:

```
ioreg -p IOUSB -w0 -l | grep -E '"(USB Product Name|USB Serial Number|idVendor)"'
  "USB Product Name"  = "Amazon Kindle"
  "idVendor"          = 6473          # 0x1949 = Amazon
  "USB Serial Number" = "B00A…"       # prefix B00A -> K3 3G UK/Europe
```

Prefix **B00A** → **`Update_jailbreak_0.13.N_k3gb_install.bin`**. Not `k3g` (that is B006, US).

> **RESOLVED 2026-08-27** from the package's own `README.txt` line 66: *"For a Kindle 3 WiFi
> running firmware 3.3.x or 3.4.x, that would be `Update_jailbreak_0.13.N_k3w_install.bin`"* —
> i.e. **3.4.x takes the PLAIN package**, not the `-3.0-to-3.2` variant. Academic here, since
> this device is already jailbroken and must not be re-flashed.

**Order of operations:**

1. **Airplane mode ON.** Whispernet is live over EDGE and 3.4.3 exists — a mid-prep OTA
   invalidates the staged file.
2. Plug in; confirm enumeration (`ioreg -p IOUSB` for Amazon `0x1949`, `/Volumes/Kindle`).
3. **Copy `/Volumes/Kindle/documents/` off first.** Before anything is written.
4. Read serial + firmware off the device screen. Pick the `.bin`.
5. Copy the chosen `.bin` to the **volume root** (not a subfolder).
6. Eject cleanly, then on-device: `Home → Menu → Settings → Menu → Update Your Kindle`.
7. Then USBNetwork, then KUAL.

**Known 3.4.x failure modes** (t=311502): `U006` error codes, and
"Another Jailbreak found — Aborting" on devices carrying remnants of a previous jailbreak.

Per CLAUDE.md: no flashing, resetting or factory-restoring without explicit approval. Single
device, no spare.

### Next actions, in order

1. **Cable arrives** → confirm enumeration (`ioreg -p IOUSB` for `0x1949`, `/Volumes/Kindle`).
2. Copy books off `/Volumes/Kindle/documents/`.
3. Read the exact 3G jailbreak `.bin` filename off MobileRead t=88004; stage it at volume root.
4. Jailbreak → USBNetwork → KUAL.
5. **Finish the toolchain** — most likely in a Linux container, see failure #2 above.
6. **Run the gate**: cross-compile a C++20 hello-world using `std::atomic` + a thread with
   `arm-kindle-linux-gnueabi-g++ -std=gnu++2a`, scp it over, run it. Watch for missing glibc
   symbols (§5.1).

Device-independent work available any time: `pio run -e simulator` in `vendor/CrossInk`.

---

## 9b. Does this make sense? — research, 2026-08-26

Question asked: is porting CrossInk to the K3 worth doing, and **would anyone else want it?**

### The premise checks out

The motivation in §8 is "KOReader works but its interaction model is bad on a keyboard Kindle."
That is **independently corroborated**, not just a personal gripe:

- KOReader's current wiki lists K3 as supported under **Legacy** ("Kindles with physical
  keyboards. i.e K2, DX, K3 (and all their variants)") but warns *"Certain minor features may
  currently still be unavailable on these devices."*
- A 2025 third-party write-up on reviving early Kindles reaches the same conclusion
  independently: navigating KOReader with button inputs *"certainly works but can be finicky at
  times."* ([vale.rocks](https://vale.rocks/posts/improving-early-kindles))

> A 2017 MobileRead thread ([t=288646](https://www.mobileread.com/forums/showthread.php?t=288646))
> claims legacy builds were dropped and there is "no support for pure keyboard-based devices."
> **That is stale** — the current wiki and 2025 field reports both contradict it. Do not cite it.

So the problem is real. The question is whether *this* is the solution.

### The plan does not deliver on the premise

This is the finding that matters, and it is verifiable in the clone:

**CrossInk's entire input model is seven hardcoded constants.**
`vendor/CrossInk/lib/hal/HalGPIO.h:104-110`:

```cpp
static constexpr uint8_t BTN_BACK = 0;
static constexpr uint8_t BTN_CONFIRM = 1;
static constexpr uint8_t BTN_LEFT = 2;
static constexpr uint8_t BTN_RIGHT = 3;
static constexpr uint8_t BTN_UP = 4;
static constexpr uint8_t BTN_DOWN = 5;
static constexpr uint8_t BTN_POWER = 6;
```

The K3 has roughly **fifty** inputs — full QWERTY, 5-way, dual page bars, Home/Menu/Back/Aa/Sym/Del.
CrossInk is designed for *fewer* buttons than KOReader, not more. Porting it as-is yields a reader
that ignores ~85% of what makes the K3 distinctive.

**And upstream has explicitly rejected the feature class that would use them.** `SCOPE.md`,
Out-of-Scope: *"Complex Annotation: No typed out notes. These features are better suited for
devices with better input capabilities and more powerful chips."* The single thing the K3 has
that the X3/X4 do not is the thing CrossInk has declared out of bounds.

Therefore the anti-KOReader interaction model in §8 is **not inherited from CrossInk — it is
100% net-new work**, built on a UI layer with no concept of text entry or key shortcuts, and
permanently un-upstreamable. §6's effort table does not include any of it.

### Maintenance is worse than §8 assumed

| Metric | Value |
|---|---|
| First commit | 2025-12-03 — the project is **~9 months old** |
| Total commits | 1,564 |
| Commits in last 90 days | **104** (~35/month) |
| Contributors | 5+ (Julia, Dave Allie, Julia Nguyen, Zach Nelson, jpirnay) |
| Last commit | 2026-08-18 |

§8 says "sole maintainer forever." Accurate, but understated: you would be maintaining a
divergent fork of a young, fast-moving project, re-basing a Kindle backend and a bespoke input
layer against ~35 commits/month indefinitely.

### Would anyone else want it? — no evidence of demand

- **No Kindle port request surfaced** anywhere in CrossInk's community. Coverage and discussion
  (eReadersForum, blogs, Android Authority) is uniformly X3/X4.
- CrossInk's own identity is typography and reading analytics on Xteink hardware, not a
  portability project.
- The K3 community is real but small and **already served** — MobileRead threads through 2025-2026,
  hobbyist blog posts, and KOReader support. Nobody is looking for a second reader.
- Revealed preference: KOReader, vastly better resourced, supports K3 only as "Legacy" with
  caveats. If there were meaningful demand for excellent keyboard-Kindle UX, it would show up
  there first.

**Answer: essentially zero external demand.** This is a project of one, for one.

### Cheaper path to the actual goal

The complaint is "KOReader's button navigation is finicky on a keyboard Kindle." The direct fix
is **a KOReader input/UI plugin for the K3 keyboard**, not a new reader:

| | Port CrossInk | KOReader plugin |
|---|---|---|
| Toolchain gate | required | not required |
| New HAL backends | 2 (display, input) | 0 |
| Fights fast-moving upstream | yes, forever | no |
| Text quality | **regresses** to 4 gray levels (§5.4) | unchanged (16) |
| Targets the actual complaint | indirectly | directly |
| Effort | 1-2 weeks → "month plus" (§6) | days |

The vale.rocks author wrote a custom KOReader plugin for exactly this class of problem, so the
extension path is proven on this device.

### Verdict

**As a way to get a better reading experience: no.** It costs a month-plus, regresses text
quality, and the interaction model that justifies it has to be written from scratch anyway —
at which point writing it inside KOReader is strictly cheaper and keeps 16 gray levels.

**As a systems project done for its own sake: yes, and it is well-shaped.** The toolchain gate is
genuinely interesting, the simulator fork is a clean starting point, and §5.1's compiler risk is
already retired. Just do not justify it on reading outcomes or on users who do not exist.

**The honest framing:** this is a craft project. That is a legitimate reason. It is not the
reason §8 currently gives.

---

## 2026-08-27 — deployment planning, since superseded by `METHOD.md`

### Critical path correction 2026-08-27 — USBNetwork, not KUAL

**KUAL gates nothing until packaging.** To run a binary on the device you need a shell, and
USBNetwork gives that for **one flash**; the Kindlet chain (MKK + devcerts + KUAL) is two flashes
plus two copies and buys nothing during development. Independently reached by both reviewers.

Revised order — flashes before first pixels: **exactly one**.

| # | Step | Flash? | Device? |
|---|---|---|---|
| 1 | Prebuilt toolchain in a Linux container | – | no |
| 2 | **Simulator portrait smoke test** (600x800 on macOS/SDL) | – | **no** |
| 3 | USBNetwork `0.57.N`, **k3gb** variant | **1** | yes |
| 4 | On-device recon over ssh | – | yes |
| 5 | Gate binary (see below) — *first pixels* | – | yes |
| 6 | Port HalDisplay/HalGPIO, iterate scp+ssh | – | yes |
| 7 | MKK + devcerts + KUAL — packaging only | 2 | yes |

Spine is 1→5→6→7; steps 2, 3, 4 run in parallel with 1.

**Step 2 is the highest-value thing available right now.** Set geometry to 600x800 portrait and
run the *simulator* on macOS. No device, no cross-compile, and it probes what is plausibly the
biggest real risk: landscape assumptions through 13.5k LOC of FreeInkUI chrome (5.5). It is not
backend code, so it does not violate "no backend before the gate".

### Gate binary contents

Link **`-static-libstdc++ -static-libgcc`** (mandatory — device libstdc++ is GCC-4 era); consider
full `-static`, which erases glibc-version risk entirely since CrossInk needs no NSS or dlopen.
Also `-lrt` (clock_gettime pre-glibc-2.17) and explicit `-lpthread`.

Test: C++20 constructs CrossInk uses; `std::thread` + `atomic` + `condition_variable` roundtrip;
throw/catch + RTTI; `std::filesystem` over `/mnt/us`; **classic "C" locale only** — `std::locale("")`
will likely throw on the device's stripped locale set.

**Concrete syscall traps.** Kernel is 2.6.26 but glibc 2.9 carries wrappers for 2.6.27 syscalls,
so these compile and link but return **ENOSYS at runtime**: `accept4`, `pipe2`, `epoll_create1`,
`eventfd2`, `dup3`, `inotify_init1`. Safe: `timerfd` (2.6.25), `O_CLOEXEC` (2.6.23).
Audit with `objdump -T` against the glibc version found on-device in step 4.

> This corrects an earlier agy review which claimed epoll/eventfd/timerfd themselves are missing.
> They are not — only the `*1`/`*2` variants are.

### einkfb API — verified

From koreader-base `ffi/einkfb_h.lua`
(<https://github.com/koreader/koreader-base/blob/master/ffi/einkfb_h.lua>):

| Constant | Value |
|---|---|
| `FBIO_EINK_UPDATE_DISPLAY` | `18139` (0x46DB) |
| `FBIO_EINK_UPDATE_DISPLAY_AREA` | `18141` (0x46DD), takes `update_area_t {x1,y1,x2,y2,which_fx,buffer}` |
| `fx_update_partial` / `full` / `slow` | `0` / `1` / `3` |
| `SET`/`GET_DISPLAY_ORIENTATION` | `18160` / `18161` |

Mapping: `FAST_REFRESH`/`HALF_REFRESH` → area update + `fx_update_partial`; `FULL_REFRESH` →
`fx_update_full`. Kernel driver owns waveforms, as 5.3 assumed.

**CORRECTION to 4:** legacy einkfb is **4bpp (2 px/byte)**, not 8bpp as stated earlier in this
document. CrossInk's internal buffer is 1bpp, so the existing unpack path retargets to *nibbles*,
not bytes. **Verify at runtime** — read var/fixed screeninfo from `/sys/class/graphics/fb0`
rather than hardcoding geometry, bpp, or polarity.

**Gate step zero: cross-compile NiLuJe's FBInk** (<https://github.com/NiLuJe/FBInk>). It supports
legacy einkfb Kindles (K2+), so it validates the toolchain *and* the display path with zero code
written. Use its legacy paths as the authoritative reference, but write our own ~50 lines of
ioctls — a dependency is not warranted.

### Simulator portrait smoke test — RUN 2026-08-27, PASSED (build/runtime)

Executed on macOS, no device, no cross-compiler.

| Check | Result |
|---|---|
| `pio run -e simulator` at stock 800x480 | **SUCCESS**, 46.6s, 0 errors, 0 warnings |
| Artifact | 14 MB `Mach-O 64-bit executable arm64` |
| Geometry changed to **600x800 portrait** | 2 constants, `FreeInkDisplay.h:47-48`. `600/8 = 75` bytes/row, divides cleanly |
| Rebuild at 600x800 | **SUCCESS**, 47.7s, 0 errors |
| Runtime | ran ~160 s, `Boot` -> `Home` activity, 16 clean periodic memory ticks, exited 0 |

Runtime log highlights:

```
[MAIN] Hardware detect: X4
[MAIN] Starting CrossInk version 1.5.0
[ACT]  Render task started with 16384-byte stack
[ACT]  Entering activity: Boot -> Exiting -> Entering activity: Home
```

Only errors were benign first-run misses (`dictionary.bin`, `global_stats.bin`).

**What this proves:** the geometry change is as cheap as 5.5 predicted, and nothing in the boot
path or the Home activity asserts on landscape dimensions.

**VISUAL INSPECTION 2026-08-27: PASSED.** Owner ran the portrait build and reported the UI
"seems all good" — chrome and layout hold up at 600x800. Only deviation noted: the UI identifies
the device as **Xteink**, which is cosmetic branding (`Hardware detect: X4`), not a layout fault.

**This retires the highest-ranked risk in the project.** Both reviewers independently ranked
"landscape assumptions threaded through 13.5k LOC of FreeInkUI chrome" as the most likely killer
(5.5). It is not. Portrait is two constants and the UI adapts.

Remaining visual work is retuning, not rearchitecting: default font sizes are tuned for a 4.3"
landscape panel and the K3 is a 6" portrait one. That is taste, done later, against the device.

**Device identity:** `DeviceType { X4, X3 }` at `HalGPIO.h:31`. A Kindle target wants a third
enum value plus branding strings — trivial, and deferred until after the gate.

Two incidental findings:

- `Hardware detect: X4` — device identity is `DeviceType { X4, X3 }` (`HalGPIO.h:31`). A Kindle
  target eventually wants its own enum value rather than masquerading as an X4.
- The simulator reports `heap free=1048576` — a **1 MB** fake ESP32-style budget. The K3 has
  256 MB, so `MemoryBudget` pressure is simulated, not real, on the target device (cf. 1).

**Build wiring note:** upstream `platformio.ini` already defines `[env:simulator]` (line 208) and
resolves `symlink://freeink-sdk/...` relative to the project root. Our bootstrap deliberately
leaves `vendor/crossink/freeink-sdk` empty, which breaks those paths — a symlink was created by
hand for this test. **TODO:** make `bootstrap.sh` populate CrossInk's own submodule pinned to our
`freeink-sdk` SHA instead, which satisfies PlatformIO and keeps git quiet.

### Prerequisite chain, with real status

| # | Step | Status |
|---|---|---|
| 1 | Jailbreak | **DONE** — already jailbroken since ~2011, survived to 3.4.2 (see 9a) |
| 2 | KUAL installed | **NOT PRESENT** — no `extensions/` at volume root |
| 3 | USBNetwork (dev loop only) | **NOT PRESENT** |
| 4 | Toolchain `arm-kindle-linux-gnueabi` | **BLOCKED** — zlib vs clang 21 skew, needs Linux container |
| 5 | Gate: C++20 + `std::atomic` + thread runs on device | not run — blocked on 4 |
| 6 | `CMakeLists.txt` (replaces platformio.ini) | not written |
| 7 | `platform/kindle/HalDisplay.cpp` + `HalGPIO.cpp` | not written — deliberately, gate first |
| 8 | Package as KUAL extension | trivial once 4-7 land |

**CORRECTION 2026-08-27** (independent verification, Fable). An earlier version of this section
called steps 2-3 "installable today, low risk". That was wrong on two counts.

**1. Installing KUAL on a K3 is not a folder copy — it is two flashes.** KUAL on legacy Kindles
is a *Kindlet* (Java, `KUAL-KDK-1.0.azw2`, appears as a book on the Home screen and is opened
with the 5-way), not a native binary. It requires:

| # | Component | How it installs |
|---|---|---|
| a | **MKK** (Mobileread Kindlet Kit) | `Update_mkk-*-install.bin` via Menu → Settings → Update Your Kindle |
| b | **Renewed developer certificates** | `Update_mkk-20250419-*_keystore-install.bin`, same mechanism |
| c | `KUAL-KDK-1.0.azw2` | copy into `documents/` |
| d | our extension folder | copy into `extensions/` |

Only (c) and (d) are copies. **(a) and (b) go through the firmware update mechanism** — the same
path as a jailbreak flash, on a device with no spare. Both bins are **variant-specific**: this
device is **B00A**, so do not grab a `B006` file.

**Kindlet developer certificates EXPIRED 2025-04-17.** Without (b), opening KUAL fails with
"This title is not signed by a registered developer". Corroborated independently by an earlier
search and by the 2025 K3 walkthrough at <https://dast.org/posts/2025/08/kindle3-jailbreak-koreader/>.
See also <https://kindlemodding.gitbook.io/kindlemodding/post-jailbreak/installing-kual-mrpi>
and the KUAL thread <https://www.mobileread.com/forums/showthread.php?t=203326>.

Note `rm -rf extensions/crossink` removes *our app*, but not MKK or KUAL.

**2. "Exit returns you to the stock UI" is something we must implement, not something we get.**
KOReader on sysv/legacy Kindles does this explicitly
(<https://github.com/koreader/koreader/blob/master/platform/kindle/koreader.sh>):

```sh
killall -STOP cvm                    # launch: SUSPEND the Amazon UI (not stop)
...
killall -CONT cvm                    # exit: resume it
echo 'send 139' >/proc/keypad        # twice — force the stock UI to repaint
```

CrossInk has no such code and never will upstream — it has no concept of `cvm`. **Our launch
wrapper must replicate it, including a trap so a crash still resumes `cvm`.** Without the trap,
a crash leaves the device apparently frozen until restart. Recoverable, but a real failure mode.

Good news on this one: on K3 a full `stop framework` is **not** required — SIGSTOP/SIGCONT pause
is KOReader's default there, and full stop is only an optional RAM-freeing variant with a slower
exit. CrossInk is far lighter than KOReader, so pausing should be sufficient.

**Charge the battery before attempting either flash.**

Everything from step 4 onward is still the actual project.

Sources: [KUAL wiki](https://wiki.mobileread.com/wiki/KUAL_What's_New),
[config.xml reference](https://kindlemodding.org/wafs-and-mesquite/understanding-config-xml.html).

---

## Superseded plans, kept for the record

### Original plan (superseded, kept for the record)

**Run the toolchain gate.** Nothing else matters until it passes.

Device prerequisites — battery is done, firmware **3.4.3** (an Amazon OTA self-installed 2026-08-27 18:21). Chain to the gate:

1. ~~Read the firmware version.~~ **Done 2026-08-26: 3.4.2.**
2. **Airplane mode until jailbroken — not just "WiFi off".** A registered K3 that reaches Amazon
   can pull an OTA. 3.4.x is jailbreakable, but an unplanned version change invalidates a staged
   package. This unit has cellular, so disabling WiFi alone does not close the path.
3. **Use the 3G jailbreak package, not `k3w`.** The `.bin` is variant-specific and this is the
   3G model. Match the exact filename against what NiLuJe's thread currently attaches; confirm
   the variant from the serial number rather than assuming.
3b. **Capture a stock page-turn baseline.** §5.3's success criterion is "does not feel worse
   than stock", which needs numbers. Record slow-motion video of stock page turns and note
   timings — full refresh and partial. Worth doing early, but *not* urgent: the jailbreak does
   not remove the stock reader (see below), so the baseline stays available.
4. **Get a USB *data* cable.** The one on hand is charge-only — the device does not enumerate
   (`ioreg -p IOUSB` shows no Amazon `0x1949`). No cable, no file transfer, no jailbreak, no
   USBNetwork. This is the current hard blocker.
5. Jailbreak → USBNetwork → KUAL.
6. Then the gate: koxtoolchain `kindle-legacy`, cross-compile a C++20 hello-world using
   `std::atomic` + a thread, scp it over USBNetwork, run it.

### Jailbreak specifics (3.4.2) — UNVERIFIED, confirm against the thread

Canonical source: NiLuJe's thread, [MobileRead t=88004](https://www.mobileread.com/forums/showthread.php?t=88004).
A summarizer reading that thread reported the package as `kindle-jailbreak-0.13.N.zip` →
`Update_jailbreak_0.13.N_k3w_install.bin`, and that **3.3.x/3.4.x use the plain `k3w` package,
not the `-3.0-to-3.2` variant**. That last rule is the load-bearing part and matches
[t=311502](https://www.mobileread.com/forums/showthread.php?t=311502), where NiLuJe says to read
"3.0-to-3.2" as meaning 3.0-to-3.4.

**Do not trust the version number above.** The thread is a living post updated over many years;
`0.13.N` may well be stale. Read the first post directly and take whatever version it currently
attaches. Install is: copy `.bin` to the volume root → eject → `Home → Menu → Settings →
Menu → Update Your Kindle`.

Known 3.4.x failure modes reported in t=311502: `U006` error codes and
"Another Jailbreak found — Aborting" on devices with jailbreak remnants.

**The jailbreak is additive, not a replacement.** Stock firmware, the stock reader, and
Whispernet all remain after jailbreaking; KUAL is a launcher you exit back out of. CrossInk
would ship as a KUAL extension alongside the stock stack, not instead of it. Two consequences:
the stock reader stays available as the §5.3 reference indefinitely, and the working EDGE
Whispernet is not put at risk by the port itself.

**OTA risk is live, not theoretical.** 3.4.3 exists and this device is on 3.4.2, so there is a
real update for Amazon to push over the working Whispernet link. Airplane mode whenever the
device is powered and unattended, until the jailbreak lands.

Unblocked alternative, independent of all of the above: build `pio run -e simulator` on macOS to
have a development target regardless of how the gate goes. Requires PlatformIO + SDL2.

---

## Appendix: the help requests that were never sent

Drafted 2026-08-28, while §9-ALERT above was believed. Both describe a device thought to be
un-jailbroken and a jailbreak installer thought to be broken. Neither was true — the abort was
the installer working correctly. They are kept because they are a good record of how confident
a wrong diagnosis can look once it has a table of eliminated hypotheses under it.

Plain-text variants of both (`*.txt`, hand-wrapped for forum posting) were dropped; these are
the sources.

## Draft post for MobileRead — Kindle Keyboard 3G (B00A) cannot install jailbreak 0.13.N

**Suggested thread:** Font, ScreenSaver & USBNetwork Hacks — https://www.mobileread.com/forums/showthread.php?t=88004
(or the Snapshots thread, t=225030)

---

**Title:** K3 3G UK (B00A) on 3.4.3 — `Update_jailbreak_0.13.N_k3gb_install.bin` fails, no error code

Hi — I've hit a wall and would appreciate a sanity check before I do anything else.

**Device:** Kindle Keyboard 3G, serial prefix **B00A** (UK/Europe variant)
**Firmware:** now **3.4.3** (was 3.4.2)
**Current state:** stock, **un-jailbroken**

**Background.** This device was jailbroken in **November 2010** — it still carried NiLuJe's
ScreenSavers hack **0.18.N**, and `linkss/backups/prettyversion.txt` read "Kindle 3.1". That
jailbreak survived OTAs all the way to 3.4.2 and was demonstrably still live (a fresh
`linkss/run/usb-watchdog.pid` was being written on every USB connect).

I wanted USBNetwork for SSH. That's where it went wrong.

**What I did, in order:**

| # | Action | Result |
|---|---|---|
| 1 | `Update_usbnetwork_0.57.N_k2_dx_k3_install.bin` | **FAILED** — generic "not successful", no error code |
| 2 | `Update_jailbreak_0.13.N_k3gb_uninstall.bin` | **SUCCESS** |
| 3 | `Update_jailbreak_0.13.N_k3gb_install.bin` | **FAILED** — generic, no code |
| 4 | same, after a full Restart | **FAILED** |
| 5 | same, after Restart + `linkss/` renamed to disable the 2010 hack | **FAILED** |
| 6 | `Update_usbnetwork_0.57.N...` again | **FAILED** — confirming 3-5 never took |
| 7 | **Updated firmware 3.4.2 -> 3.4.3** (official Amazon B00A file) | **SUCCESS** |
| 8 | `Update_jailbreak_0.13.N_k3gb_install.bin` on **3.4.3** | **FAILED** — generic, no code |

Step 1 failed because USBNetwork 0.57.N needs jailbreak >= 0.11.N (its `README.txt:98`) and mine
was from 2010. Step 2 was done because `README.txt:31` says updating the jailbreak on a 3.x
device is one of the cases requiring the uninstaller first, and the v0.9.N changelog says the
installer aborts if another jailbreak is present. In hindsight that was the mistake — it left me
with no jailbreak and no way back.

Step 7 was on the strength of https://www.mobileread.com/forums/showthread.php?t=372804
("You have to pull up the firmware version all the way to 3.4.3 for the currently available
jailbreak (0.13N) to install"), and a confirmed success on 3.4.3 in
https://www.mobileread.com/forums/showthread.php?t=352993 . It didn't help.

**Things I have already checked, so you don't have to ask:**

* **Correct variant.** `README.txt:65` says *"k3gb means K3 3G (UK [B00A])"*. My serial starts
  B00A. I used `k3gb`, not `k3g`.
* **Correct FW variant.** Not the `-3.0-to-3.2` file — README says those are for FW <= 3.2 only,
  and I'm on 3.4.3.
* **File integrity.** Archive `kindle-jailbreak-0.13.N-r18833.tar.xz` md5
  `a5605097853b97dbc0aba09df611d5e0` (matches the Snapshots listing). The extracted
  `Update_jailbreak_0.13.N_k3gb_install.bin` is md5 `6f98c96fe96cbbb63eb7784ecbbbbcf2`, 260 KB,
  and I re-verified the copy **on the Kindle's root** byte-for-byte before each attempt.
* **Correct procedure.** File at the volume **root**, ejected **and unplugged**, then
  `[HOME] -> [MENU] -> Settings -> [MENU] -> Update Your Kindle`. Never rebooted with a `.bin`
  present.
* **The updater itself works** — step 2 and step 7 both succeeded, so this isn't a broken update
  mechanism or a bad USB connection.
* **Firmware confirmed 3.4.3 on the Settings screen**, not assumed. The failing attempt was made
  on 3.4.3.
* **Wireless has been off (airplane mode) throughout**, including for every jailbreak attempt.
* **No error code.** Unlike t=372804 (U007/U004/Error 3), I get only "not successful". No U006,
  and never "Another Jailbreak found".
* **Device targeting verified at the byte level.** The OTA header is `FC02`, and the device ID at
  offset `0x0C` differs across the three K3 packages exactly as expected:

  | file | device id | serial prefix |
  |---|---|---|
  | `Update_jailbreak_0.13.N_k3g_install.bin`  | `06 00 00 00` | B006 |
  | `Update_jailbreak_0.13.N_k3w_install.bin`  | `08 00 00 00` | B008 |
  | `Update_jailbreak_0.13.N_k3gb_install.bin` | `0a 00 00 00` | **B00A** |

  My serial starts B00A, so `k3gb` (id `0x0A`) is unambiguously the right package.

**Questions:**

1. **Did I leave the device half-uninstalled?** This is my main suspicion now. I ran the
   **0.13.N** uninstaller against a **2010-era** jailbreak. Your ScreenSavers `info.txt` warns
   *"if you wish to uninstall me, you'll at least need the uninstaller from that version"* — does
   the same apply to the jailbreak? If so the device may now be neither jailbroken nor cleanly
   stock, which would explain a silent failure on a correct file.

   Supporting evidence: **the 2010 hack's system-side hook kept running after the uninstall
   "succeeded"** — `linkss/run/usb-watchdog.pid` was still being refreshed on every USB connect
   afterwards. So something survived. Is there a way to fully clean `/etc/uks/`,
   `/etc/init.d/jailbreak`, `/etc/rc5.d/S64jailbreak`, `/etc/rc3.d/K09jailbreak` without shell
   access?

2. Alternatively: is `0.13.N` expected to install on a genuinely **stock, never-jailbroken** K3 at
   3.4.3 at all? I followed a 2026 step-by-step guide whose author did exactly this successfully
   on a never-jailbroken device, with the same procedure and the same firmware.
3. Does the absence of any error code point at something specific?
4. Should I try the `k3g` file instead of `k3gb`, given USBNetwork ships a single
   `k2_dx_k3` package for all K3 variants? Or is that a bad idea on a B00A?
5. Is there another route for a K3 at 3.4.3 — LanguageBreak, WatchThis, KindleBreak (t=338268
   claims "almost any Kindle <= 5.13.3")?
6. Is a factory reset advisable here, or would that make things worse?

Device is otherwise healthy and all content is backed up. Happy to run diagnostics — I just have
no shell access, which is the whole problem.

Thanks.

---

## Reddit draft

**Where to post:** r/kindle (largest, and where the 2026 K3 guide circulated), cross-post to
r/kindlemodding. If neither bites, r/ereader.

---

**Title:**

> I uninstalled a 15-year-old jailbreak from my Kindle Keyboard and now the current one won't install (B00A, FW 3.4.3, no error code)

---

**Body:**

I think I bricked my jailbreak, not my Kindle. Looking for anyone who's seen this.

**The device:** Kindle Keyboard 3G, serial starts **B00A** (Europe variant → `k3gb`). Currently on **3.4.3**. Works perfectly as a reader — this isn't a brick, it's a self-inflicted wound.

**The backstory:** I replaced the battery on a Kindle that had been sitting in a drawer, and discovered it was jailbroken **in November 2010** — NiLuJe's ScreenSavers hack 0.18.N was still installed, and still *running*. It had survived every OTA from 3.1 all the way to 3.4.2. Kind of beautiful, honestly.

I wanted SSH on it, so I went to install USBNetwork. It failed — turns out USBNetwork 0.57.N needs jailbreak ≥ 0.11.N and mine was ancient. The README says updating the jailbreak on a 3.x device is one of the rare cases where you run the uninstaller first, so I did.

**Uninstall: succeeded. Reinstall: failed. Every time since.**

**What I've tried:**

* `Update_jailbreak_0.13.N_k3gb_install.bin` — fails
* After a full restart — fails
* After disabling the old 2010 hack (renamed `linkss/`, verified it wasn't running) — fails
* **After updating 3.4.2 → 3.4.3** (which a couple of threads say is required) — still fails

Always the same: *"not successful"*. **No error code.** No U006, no U007, never "Another Jailbreak found."

**What I've already ruled out, so you don't have to ask:**

* **Right file for the model.** B00A → `k3gb`. I checked this at the byte level, not just the docs — the OTA header has a device ID at offset `0x0C`: `k3g`=`06`, `k3w`=`08`, `k3gb`=`0A`. My serial is B00A. It's the right package.
* **Right file, not corrupted.** Archive md5 `a5605097853b97dbc0aba09df611d5e0`, extracted bin md5 `6f98c96fe96cbbb63eb7784ecbbbbcf2`, and I re-verified the copy **on the Kindle itself** before flashing.
* **Not the `-3.0-to-3.2` variant** — those are for FW ≤ 3.2, I'm on 3.4.3.
* **Right procedure.** File at volume root, ejected *and* unplugged, then Settings → Menu → Update Kindle. Never rebooted with a .bin sitting there.
* **Wireless off** (airplane mode) the whole time.
* **The updater works** — the uninstall succeeded, and the 3.4.3 firmware update succeeded. So it's not a broken update path or a bad cable.

**My actual theory, and the reason I'm posting:**

I ran the **2023** uninstaller against a **2010** jailbreak. NiLuJe's ScreenSavers hack literally warns *"if you wish to uninstall me, you'll at least need the uninstaller from that version"* — I suspect the same applies to the jailbreak, and I've left the thing **half-uninstalled**: neither jailbroken nor cleanly stock.

Supporting evidence: **the 2010 hack's system-side process kept running after the uninstall "succeeded."** Its PID file was still being refreshed on every USB connect. So something definitely survived on the system partition.

**Questions:**

1. Does a half-removed jailbreak match the silent, error-code-free failure?
2. Any way to fully clean `/etc/uks/` and the leftover init scripts **without shell access**? (Chicken and egg — shell access is what I was trying to get.)
3. Is 0.13.N genuinely expected to install on a never-jailbroken K3 at 3.4.3? The 2026 guide going around says yes, and its author did exactly that.
4. Would a **factory reset** clear the remnants, or make it worse?
5. Any alternative route for a K3 at 3.4.3 — LanguageBreak, WatchThis, KindleBreak?

All my books are backed up and the Kindle works fine as a reader, so nothing's lost except the jailbreak and my dignity. Happy to run diagnostics — I just have no shell, which is the whole problem.
