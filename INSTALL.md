# Running CrossKink on your own Kindle 3

End to end: from a Kindle in a drawer to CrossInk on the screen.

**This is not a polished install.** CrossKink currently runs as a bounded session launched from
an update package — good enough to see it work, not good enough to read a book on. See
[README](README.md). If you want a daily reader on a K3 today, install
KOReader instead; this is a systems project.

**Read [`METHOD.md`](METHOD.md) before running anything.** It explains what each step actually
does. This page is the sequence; that one is the reasoning.

---

## 0. Before you start

| | |
|---|---|
| **Device** | Kindle 3 / Keyboard (D00901). Nothing else — the framebuffer format, keycodes and update format are all specific to it. |
| **Jailbroken** | required, any era. Checking is four files, below. |
| **Host** | macOS or Linux with Docker. The toolchain and `kindletool` are x86-64 Linux binaries; on Apple Silicon they run under Rosetta. |
| **Cable** | a USB **data** cable. Charge-only cables are common and the device simply will not enumerate. |
| **Risk** | this writes nothing outside `/mnt/us` and `/tmp`, and modifies no system files. The main way to lose an afternoon is a hung updater — see [If it hangs](#if-it-hangs). |

> **Do not uninstall an existing jailbreak.** If yours is old, leave it. It works. This is the
> single most expensive mistake in this project's history — see [`LOG.md`](LOG.md).

### Find your model

The `.bin` is variant-specific and picked from the **serial prefix**, not from what the box said:

```sh
ioreg -p IOUSB -w0 -l | grep "USB Serial Number"     # macOS, device plugged in
```

| Prefix | Device | Use |
|---|---|---|
| `B006` | K3 3G, US/Canada | `k3g` |
| `B008` | K3 WiFi only | `k3w` |
| `B00A` | K3 3G, UK/Europe | `k3gb` |

### Are you jailbroken?

All four files must exist. This is the exact condition NiLuJe's own installer tests.

```
/etc/init.d/jailbreak
/etc/rc5.d/S64jailbreak
/etc/rc3.d/K09jailbreak
/etc/uks/pubprodkey01.hack.pem
```

Chicken-and-egg: checking them needs a shell, which is what you are trying to get. Two ways
without one:

- **Look for the aftermath.** A `linkss/` or `linkfonts/` folder at the root of the USB volume
  means a NiLuJe hack was installed at some point, which means the device was jailbroken.
- **Just try.** Build the recon package in step 3 and run it. If it produces output, you are
  jailbroken. If the update fails, you are not.

**If you are not jailbroken:** get one first, from NiLuJe's thread on MobileRead
([t=88004](https://www.mobileread.com/forums/showthread.php?t=88004)). That is out of scope
here and well covered there. Come back after.

**If your jailbreak install fails with no error code, you are probably already jailbroken.**
`linkjail: E def:install::Another jailbreak is already installed, aborting.` is the installer
working correctly. Do not chase it, and do not run the uninstaller to "fix" it.

---

## 1. Get the tools

Two x86-64 Linux binaries and two container images. Neither binary is redistributed here.

```sh
git clone --recurse-submodules https://github.com/theanhgen/CrossKink.git
cd CrossKink

# kindletool — builds and signs the update packages
#   https://github.com/NiLuJe/KindleTool/releases  (Linux x86-64 build)
cp ~/Downloads/kindletool package/kindletool && chmod +x package/kindletool

# libotautils — NiLuJe's logging/progress-bar helper that the scripts source.
# Lift it from any of his hack packages: unpack the .bin and take the file verbatim.
cp ~/somewhere/libotautils package/libotautils

docker build --platform linux/amd64 -t kindle-ktool -f docker/Dockerfile.kindletool .
```

Skippable if you are doing step 2 anyway — `build-package.sh` falls back to the `kindle-xc`
cross-build image, which already has the libraries kindletool needs.

On Apple Silicon, start the VM first — the defaults are not enough:

```sh
colima start --cpu 6 --memory 12 --vz-rosetta
```

---

## 2. Build the binary

Full instructions and the reasoning are in [`BUILD.md`](BUILD.md) and
[`docker/README.md`](docker/README.md). Short version:

The CrossInk sources are in this tree already — this is a fork, not an overlay, so
there is nothing to vendor.

```sh
curl -LO https://github.com/koreader/koxtoolchain/releases/download/2026.08/kindle.tar.zst
shasum -a 256 kindle.tar.zst  # 4aa5751f5e29268d91999d4754d14ce0d6db5510e46b26bd7b2a8ce25fe09ad8
docker build --platform linux/amd64 -t kindle-xc -f docker/Dockerfile .

docker run --rm --platform linux/amd64 --ulimit stack=-1 -v "$PWD":/proj -w /proj kindle-xc bash -c '
  ulimit -s unlimited
  cmake -S platform/kindle -B build -DCMAKE_TOOLCHAIN_FILE=$PWD/cmake/kindle-toolchain.cmake -DCMAKE_BUILD_TYPE=MinSizeRel
  cmake --build build -j1
'
```

Takes about 20 minutes. `ulimit -s unlimited` and `-j1` are both mandatory: without them
`cc1plus` segfaults partway through and reports it as an internal compiler error. A large
*finite* stack limit is not enough — that was measured.

You should end up with:

```
build/crossink: ELF 32-bit LSB executable, ARM, EABI5 version 1 (SYSV),
                dynamically linked, interpreter /lib/ld-linux.so.3,
                for GNU/Linux 2.6.22
```

---

## 3. Prove the toolchain on *your* device first

Do not skip this. It is five minutes and it separates "my build is wrong" from "my device is
different from theirs". It runs a C++20 binary that exercises threads, atomics, exceptions,
RTTI and `std::filesystem`, and writes a report to the USB volume.

```sh
docker run --rm --platform linux/amd64 -v "$PWD":/proj -w /proj kindle-xc \
  bash -c 'TC=/opt/x-tools/arm-kindle-linux-gnueabi gate/build.sh'

package/build-package.sh --device k3gb --name ckgate gate/gate.sh   # your device code
```

Copy `gate/gate-arm` **and** the one `.bin` to the volume root, then install (step 5). Read
`gate-result.txt` off the volume afterwards. Compare against
[`gate/results/gate-result-2026-08-28.txt`](gate/results/gate-result-2026-08-28.txt).

Expect `glibc 2.5`, `__cplusplus = 202002`, lock-free `atomic<int>`, and zero failures.

---

## 4. Build the package

```sh
package/build-package.sh --device k3gb --name crosskink package/crosskink.sh
```

It stages the files flat, signs with kindletool's default jailbreak key, then verifies its own
output before handing it to you — magic, device ID, signatures, and that nothing ended up in a
subdirectory:

```
==> dist/Update_crosskink_k3gb.bin
    magic FC02, device 0x0A (k3gb), signed, 6 entries at root
```

That device ID check matters. The byte at offset `0x0C` is what the updater compares against
your hardware, and a mismatch is one of the ways you get a silent failure with no error code.

---

## 5. Install

```sh
cp build/crossink         /Volumes/Kindle/crosskink   # binary is built as 'crossink'
cp dist/Update_crosskink_k3gb.bin /Volumes/Kindle/

# macOS makes AppleDouble twins that also end in .bin and break the one-bundle rule
find /Volumes/Kindle -maxdepth 1 \( -name '._*' -o -name '.DS_Store' \) -delete
```

Then:

1. **Eject *and* unplug.** While the cable is connected the device has handed `/mnt/us` to your
   computer and genuinely cannot see the file.
2. `[HOME]` → `[MENU]` → Settings → `[MENU]` → **Update Your Kindle**.
3. Missing or greyed out? **Restart the Kindle.** It only scans for update bundles on boot.

Exactly **one** `.bin` at the volume root. More than one and it refuses.

The screen will show CrossKink, then `HOLD MENU for 2s to exit`. The stock framework is
suspended while it runs and resumed on every path out, including a crash.

Afterwards, plug back in and read `crosskink-run.txt` from the volume — stdout and stderr are
captured there, so a blank screen still gives you something to read.

The updater deletes the `.bin` on success *and* on failure. **If the file is gone, it ran.**

---

## If it hangs

The update script is `source`d by the updater, so anything that blocks forever blocks the
update. `crosskink.sh` guards this with `CROSSINK_RUN_SECONDS=1800` as a backstop, but if you
modify it:

- **Never background a process and `wait`.** It is BusyBox `ash` inside the updater; this hung
  the device indefinitely and needed a power-slider reset to recover.
- **End with `return`, not `exit`.** The script is sourced, not executed.
- **Recovery:** hold the power slider for ~30 seconds. This is a reset, not a reflash — nothing
  is lost.

## When something fails silently

The Kindle keeps full system logs and they say plainly what the updater did:

```
/Volumes/Kindle/documents/all_logs_as_of_*.txt
```

Read them **first**. Every failure in this project's history was already explained in there —
usually `update image checksum OK` immediately followed by the actual reason. Days were spent
guessing at things the device had already written down.

Other traps, each of which cost real time:

| Symptom | Cause |
|---|---|
| `_otaupexec: version is "SP01"` / `update version mismatch` | That package is a modern `SP01` bundle and can only be installed via MRPI. The legacy updater only handles `FC02`. Nothing is broken. |
| Binary "not found" or won't execute | `/mnt/us` is `noexec` — a FUSE overlay over a vfat loop mount. Copy to `/tmp` and `chmod +x` there. The scripts already do this. |
| `eips: paint_char> character "'" not available` | `eips` cannot render apostrophes. Avoid them in prompts. |
| Update fails, no error code | Wrong device variant (check offset `0x0C`), more than one `.bin` at the root, an AppleDouble twin, or you are already jailbroken and tried to install a jailbreak. |
| Rootfs full | 646 MB with ~109 MB free. Install to `/mnt/us`, never `/`. |

---

## Doing something else with this

The packaging tool is not CrossKink-specific. Any shell script becomes a root-executing update
package:

```sh
package/build-package.sh --device k3gb --name mything myscript.sh some-binary
```

Script shape:

```sh
#!/bin/sh
[ -f ./libotautils ] && source ./libotautils
HACKNAME="mything"

otautils_update_progressbar
# ... your work here, foreground only ...

return 0
```

`platform/kindle/tools/` has seven working examples — framebuffer probes, a polarity sampler, a
keymap prompter. `keyguide.c` is the fullest one: cross-compiled C that paints prompts via
`eips`, reads `/dev/input/event*`, and writes results back to the USB volume for the host to
collect. No shell, no network, no KUAL.
