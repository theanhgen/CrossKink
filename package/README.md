# package/

Turning a shell script into something a jailbroken Kindle 3 will run as root.

| File | |
|---|---|
| `build-package.sh` | assembles, signs and **verifies** an FC02 update package |
| `crosskink.sh` | the launch script — stages the binary, suspends `cvm`, runs, restores |
| `kindletool` | **not committed.** NiLuJe's, x86-64 Linux. [Releases](https://github.com/NiLuJe/KindleTool/releases) |
| `libotautils` | **not committed.** NiLuJe's, lifted from any of his hack packages |

Neither of the two binaries is redistributed here — they are someone else's work with their own
terms. `build-package.sh` tells you where to get each one when it cannot find it.

You do not need a dedicated image if you already built `kindle-xc` for cross-compiling: it
carries libarchive and nettle, so `build-package.sh` falls back to it automatically.

## Why this works

A jailbreak on these devices does exactly one thing: it bind-mounts a **custom public key** over
the stock one, so the updater accepts packages Amazon did not sign. The private half of that key
is public knowledge — it ships as the default signing key in `kindletool`. So anything you sign
with `kindletool` is something a jailbroken Kindle runs as root, through the stock *Update Your
Kindle* menu.

KUAL and MRPI are convenience layers on top. They are not gatekeepers. Full reasoning:
[`../METHOD.md`](../METHOD.md).

## Use

```sh
package/build-package.sh --device k3gb --name crosskink package/crosskink.sh
```

`--device` is `k3g` (B006, US 3G), `k3w` (B008, WiFi) or `k3gb` (B00A, UK/intl 3G), chosen by
your **serial prefix** — `ioreg -p IOUSB -w0 -l | grep "USB Serial Number"`. It is written into
the package header at offset `0x0C` and the updater checks it, so a wrong guess is one of the
ways you get a silent failure with no error code.

Every file after the flags lands at the archive **root**. That is not cosmetic: passing
`dir/script.sh` archives the directory too, and the script's `source ./libotautils` then fails
on device.

The tool verifies its own output before handing it over — FC02 magic, device ID, gzip payload,
signatures present, nothing in a subdirectory:

```
==> dist/Update_crosskink_k3gb.bin
    magic FC02, device 0x0A (k3gb), signed, 6 entries at root
```

## Writing your own script

```sh
#!/bin/sh
[ -f ./libotautils ] && source ./libotautils
HACKNAME="mything"

otautils_update_progressbar
# ... your work here ...

return 0
```

Four rules, each of which cost real time to learn:

1. **End with `return`, not `exit`.** The updater *sources* the script.
2. **Foreground only.** Backgrounding a process and calling `wait` hangs the update
   indefinitely — recoverable only with a ~30-second power-slider reset.
3. **Bound anything interactive.** An unbounded reader never returns, so the update never
   finishes. `crosskink.sh` uses `CROSSINK_RUN_SECONDS` as a backstop.
4. **Stage binaries into `/tmp`.** `/mnt/us` is `noexec` — a FUSE overlay over a vfat loop
   mount whose backing mount forbids execution. You cannot run anything from the USB volume.

If you suspend `cvm` so the framework stops repainting over you, restore it on **every** path
out including a crash, and nudge the UI to repaint:

```sh
killall -STOP cvm
...
killall -CONT cvm
echo 'send 139' > /proc/keypad    # twice
```

Without that, a crash leaves the device looking frozen until a restart.

`eips` works during the update — it is how you prompt with no framework running. It cannot
render apostrophes (`eips: paint_char> character "'" not available`), so avoid them.

## Worked examples

`../platform/kindle/tools/` has seven, all cross-compiled C shipped this way: framebuffer
geometry probes, a polarity sampler, a grayscale ramp, and `keyguide.c`, which paints prompts
via `eips`, reads `/dev/input/event*` one key at a time, and writes the result back to the USB
volume. That is where the keymap in `../METHOD.md` came from — no shell, no network, no KUAL.
