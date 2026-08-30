# Running your own code as root on a jailbroken Kindle 3

**Device:** Kindle 3 Keyboard (D00901) · firmware 3.4.3 · verified 2026-08-29

This documents a route onto a legacy Kindle that is not in any guide I could find.
The usual advice is a four-step chain — jailbreak → MKK keystore → KUAL → MRPI —
before you can run anything. **You don't need any of it.** A jailbroken Kindle will
run an arbitrary root shell script from a package you build and sign yourself,
through the stock *Update Your Kindle* menu.

Everything below was measured on hardware, not inferred.

---

## 1. Why the usual chain isn't needed

A jailbreak on these devices does exactly one thing: it bind-mounts a **custom
public key** over the stock one, so the updater accepts packages Amazon didn't sign.

```
/dev/root /etc/uks/pubprodkey01.pem ext3 ro,noatime,nodiratime,data=ordered 0 0
```

The private half of that key is **public knowledge** — it ships as the default
signing key in `kindletool`:

```
-k, --key <file>   PEM file containing RSA private key to sign update.
                   Default is popular jailbreak key.
```

So anything you sign with `kindletool` is something a jailbroken Kindle will run
as root. KUAL and MRPI are convenience layers on top; they are not gatekeepers.

**Why USBNetwork appears to fail.** NiLuJe's modern hack packages are `SP01`
format and can only be installed through MRPI. The legacy updater only handles
`FC02`, so flashing one gives:

```
_otaupexec: version is "SP01"
_otaupexec: update version mismatch
```

That is a *format* rejection, not a signature problem, and it misleads you into
thinking the jailbreak is broken. It isn't.

## 2. Prerequisites

- A **jailbroken** Kindle 3. Any era works — this device carries yifanlu's 2010
  jailbreak, files dated 2011-04-06.
- `kindletool` ([NiLuJe/KindleTool](https://github.com/NiLuJe/KindleTool)). It is
  Linux; on macOS run it in a container.
- Optional: a cross toolchain if you want to ship binaries as well as scripts.
  [koxtoolchain](https://github.com/koreader/koxtoolchain) `kindle` target →
  `arm-kindle-linux-gnueabi`. Prebuilt tarballs are on its releases page. Note it
  does **not** build on macOS: crosstool-NG refuses a case-insensitive filesystem.

### Checking whether you are actually jailbroken

Four files, all four required. This is the exact condition NiLuJe's own installer
tests (`3.2.1-jb.sig:128`):

```sh
/etc/init.d/jailbreak
/etc/rc5.d/S64jailbreak
/etc/rc3.d/K09jailbreak
/etc/uks/pubprodkey01.hack.pem
```

If all four exist you are jailbroken — **and NiLuJe's 0.13.N jailbreak will refuse
to install**, by design:

```
linkjail: E def:install::Another jailbreak is already installed, aborting.
```

That message means success, not failure. Do not chase it. (I lost days to this,
including uninstalling a working 2010 jailbreak to try to "fix" it.)

## 3. Building a package

Put your script and `libotautils` (lift it from any NiLuJe package) in a directory:

```sh
kindletool create ota -d k3gb myscript.sh libotautils Update_mything_k3gb.bin
```

`-d` takes `k3g` (B006, US 3G), `k3w` (B008, WiFi), `k3gb` (B00A, UK/intl 3G).
**Check your serial**, don't guess — the first four characters are the model:

```sh
ioreg -p IOUSB -w0 -l | grep "USB Serial Number"    # macOS
```

Any file ending `.sh` or `.ffs` is treated as an update script. Keep files at the
**archive root** — passing `dir/script.sh` archives the directory too, and the
script's `source ./libotautils` then fails.

### Script shape

```sh
#!/bin/sh
[ -f ./libotautils ] && source ./libotautils
HACKNAME="mything"

otautils_update_progressbar

# ... your work here ...

return 0
```

The script is **sourced**, so it ends with `return`, not `exit`. Returning 0 makes
the update report success.

### Verifying before you flash

Deobfuscate and check the signature yourself. The payload is nibble-swapped and
XORed with `0x7A`, after a 64-byte header:

```python
d = open("Update_mything_k3gb.bin","rb").read()
body = bytes(((b^0x7a)>>4 | (b^0x7a)<<4) & 0xFF for b in d[64:])   # -> gzip tar
```

Header layout: `FC02` magic at 0, **device ID at offset 0x0C** (`06`/`08`/`0A`),
and an MD5 of the deobfuscated body at 0x10–0x2F, stored with the same
obfuscation. Verify a signature against the public jailbreak key:

```sh
openssl dgst -sha256 -verify pubhackkey01.pem -signature myscript.sh.sig myscript.sh
```

## 4. Installing

1. Copy **exactly one** `.bin` to the root of the USB volume.
2. Eject **and unplug**. While the cable is connected the device has handed
   `/mnt/us` to the host and cannot see the file.
3. `[HOME]` → `[MENU]` → Settings → `[MENU]` → **Update Your Kindle**.
4. If that entry is missing or greyed out, **restart the Kindle** — it only scans
   for update bundles on boot.

The updater deletes the `.bin` afterwards, on success *and* on failure. If the
file is gone, it ran.

## 5. Gotchas that cost real time

**`/mnt/us` is `noexec`.** It is a FUSE overlay (`fuse.fsp`) over a vfat loop
mount, and the backing mount is `noexec`. You cannot execute a binary from the USB
volume. Copy it to `/tmp` (tmpfs) and `chmod +x` there:

```sh
cp -f /mnt/us/mybinary /tmp/mybinary && chmod +x /tmp/mybinary && /tmp/mybinary
```

**Never background a process in the update script.** It is sourced under BusyBox
`ash` inside the updater. Backgrounding a probe and calling `wait` hung the update
indefinitely and needed a 30-second power-slider reset to recover. Foreground only.

**macOS creates AppleDouble twins.** `._Update_foo.bin` also ends in `.bin`, which
violates the one-bundle rule. Delete them, and `.DS_Store`, immediately before
ejecting.

**`eips` works during the update**, which is how you prompt the user with no
framework running. It **cannot render apostrophes** — you get
`eips: paint_char> character "'" not available`. Avoid them.

**Rootfs is nearly full**: 646 MB, ~109 MB free (82% used). Install to `/mnt/us`.

**Read the device's own logs.** The Kindle keeps full system logs, and they state
plainly what the updater did:

```
documents/all_logs_as_of_*.txt
```

Every failure I spent days guessing at was already explained in there — `update
image checksum OK` immediately followed by the real reason. Read them first.

## 6. Kindle 3 hardware reference

Measured via `FBIOGET_VSCREENINFO` / `FBIOGET_FSCREENINFO` and `EVIOCGNAME`.

### Framebuffer — `/dev/fb0`, driver `eink_fb`

| Field | Value |
|---|---|
| resolution | **600 × 800** portrait, `rotate = 0` |
| `bits_per_pixel` | **4** (2 px per byte) |
| `line_length` | **300** |
| `smem_len` | 483328 (visible plane 300 × 800 = 240000) |
| `grayscale` | 1 |

`/proc/eink_fb/` also exposes a writable `update_display`, plus `waveform_version`,
`temperature` and `recent_commands` — a refresh path that avoids the ioctls.

### Input — three devices, all `EV_KEY`

There is **no separate page-bar device**; the bars are on the keypad.

| Key | Device | Code |
|---|---|---|
| Left bar, upper | `event0` `mxckpd` | 193 |
| Left bar, lower | `event0` `mxckpd` | 104 |
| Right bar, upper | `event0` `mxckpd` | 109 |
| Right bar, lower | `event0` `mxckpd` | 191 |
| 5-way up / down / left / right | `event1` `fiveway` | 103 / 108 / 105 / 106 |
| 5-way centre | `event1` `fiveway` | 194 |
| Home / Menu / Back | `event0` `mxckpd` | 102 / 139 / 158 |
| Volume down | `event2` `volume` | 114 |

Note the bars do not follow a clean rule: `104` (`KEY_PAGEUP`, i.e. back) is the
**lower** left bar while `109` (`KEY_PAGEDOWN`, forward) is the **upper** right
one, so keycode meaning and physical position disagree. `191`/`193`/`194` are
vendor codes with no standard meaning. Pick a convention and document it.

### System

```
Linux 2.6.26-rt-lab126 armv6l   glibc 2.5   BusyBox v1.7.2   /bin/sh -> busybox
ARMv6-compatible rev 3, 512 BogoMIPS, 256 MB RAM
```

BusyBox 1.7.2 is from 2007: no `grep -E`, no bash-isms.

### Modern C++ works

A C++20 binary built with GCC 14.4.0 against glibc 2.5 runs fine. Verified on
device: `__cplusplus = 202002`, `std::thread` + `condition_variable`,
`std::atomic` (`atomic<int>` genuinely **lock-free** on ARMv6), exceptions + RTTI,
`clock_gettime(MONOTONIC)`, `std::filesystem`, `std::locale("C")`.

Link with `-static-libstdc++ -static-libgcc` to keep the maximum symbol
requirement at `GLIBC_2.4`. Avoid syscalls newer than 2.6.26 — `accept4`, `pipe2`,
`epoll_create1`, `eventfd2`, `dup3`, `inotify_init1` all `ENOSYS`. `timerfd`
(2.6.25) and `O_CLOEXEC` (2.6.23) are fine.

## 7. Worked example

`tools/keyguide.c` in this repo is a complete instance: a C program cross-compiled
for ARMv6, shipped in a self-signed `FC02` package, that paints prompts on the
e-ink display via `eips`, reads `/dev/input/event*`, and writes results back to the
USB volume for the host to collect. It produced the keymap in §6 — no shell, no
network, no KUAL.

## 8. Credits

The jailbreaks and packaging tools are other people's work: **yifanlu** and
**serge_levin** for the original exploit, **NiLuJe** for the jailbreak packages,
`libotautils` and `kindletool`, and the **KOReader** project for einkfb reference.
This document only maps a path through what they built.
