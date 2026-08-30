#!/usr/bin/env bash
#
# Build a signed FC02 update package for a legacy Kindle.
#
# The package installs through the stock "Update Your Kindle" menu on a
# JAILBROKEN device and runs its script as root. See ../METHOD.md for why this
# works and what a jailbreak actually does.
#
#   package/build-package.sh --device k3gb --name crosskink package/crosskink.sh
#
# Every file listed after the flags goes into the archive ROOT. kindletool
# treats any *.sh or *.ffs as the update script; everything else is payload.
#
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
IMAGE="${KINDLE_KTOOL_IMAGE:-kindle-ktool}"
DEVICE=""
NAME=""
OUTDIR="$ROOT/dist"

die() { echo "!! $*" >&2; exit 1; }

while [ $# -gt 0 ]; do
  case "$1" in
    --device) DEVICE="${2:-}"; shift 2 ;;
    --name)   NAME="${2:-}";   shift 2 ;;
    --outdir) OUTDIR="${2:-}"; shift 2 ;;
    -h|--help) sed -n '2,14p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
    --*) die "unknown flag $1" ;;
    *) break ;;
  esac
done

[ -n "$DEVICE" ] || die "--device is required (k3g | k3w | k3gb)"
[ -n "$NAME" ]   || die "--name is required"
[ $# -gt 0 ]     || die "no files given"

case "$DEVICE" in
  k3g|k3w|k3gb) ;;
  *) die "unknown device '$DEVICE'. k3g = B006 (US 3G), k3w = B008 (WiFi), k3gb = B00A (UK/intl 3G).
   Read YOUR serial, do not guess:  ioreg -p IOUSB -w0 -l | grep 'USB Serial Number'" ;;
esac

# libotautils is NiLuJe's, not ours, so it is not committed here. It provides
# logmsg() and the progress-bar calls the scripts use.
LIBOTA="${LIBOTAUTILS:-$ROOT/package/libotautils}"

# Staged inside the repo on purpose. Docker Desktop and colima share $HOME but
# NOT macOS's mktemp location (/var/folders/...), which mounts as an empty
# directory inside the container and fails with a confusing "Cannot stat".
STAGE="$(mktemp -d "$ROOT/.pkg-stage.XXXXXX")"
trap 'rm -rf "$STAGE"' EXIT

for f in "$@"; do
  [ -f "$f" ] || die "no such file: $f"
  # Flat by design: passing dir/script.sh archives the directory too, and the
  # script's `source ./libotautils` then fails on device.
  cp "$f" "$STAGE/$(basename "$f")"
done

if [ -f "$LIBOTA" ]; then
  cp "$LIBOTA" "$STAGE/libotautils"
elif grep -q 'libotautils' "$STAGE"/*.sh 2>/dev/null; then
  die "your script sources ./libotautils but it was not found at
     $LIBOTA
   It is NiLuJe's, so it is not redistributed here. Lift it from any of his
   hack packages (jailbreak, usbnetwork, …) — unpack the .bin and take the
   file verbatim — or set LIBOTAUTILS=/path/to/libotautils."
fi

# macOS AppleDouble twins also end in .bin/.sh and would violate the
# one-bundle rule on the volume root. Kill them before they are archived.
find "$STAGE" -name '._*' -delete
find "$STAGE" -name '.DS_Store' -delete

mkdir -p "$OUTDIR"
OUT="$OUTDIR/Update_${NAME}_${DEVICE}.bin"

command -v docker >/dev/null || die "docker not found — kindletool is an x86-64 Linux binary"
# Distinguish "daemon is down" from "image is missing"; otherwise a stopped VM
# reports itself as a missing image and sends you off building one.
docker info >/dev/null 2>&1 || die "the docker daemon is not running.
   colima start --cpu 6 --memory 12 --vz-rosetta"
if ! docker image inspect "$IMAGE" >/dev/null 2>&1; then
  # The cross-build image already carries libarchive and nettle, so it can run
  # kindletool too. Use it rather than making people build a second image.
  if [ "$IMAGE" = "kindle-ktool" ] && docker image inspect kindle-xc >/dev/null 2>&1; then
    echo "==> kindle-ktool not built; using kindle-xc instead" >&2
    IMAGE=kindle-xc
  else
    die "image '$IMAGE' missing. Build it:
     docker build --platform linux/amd64 -t $IMAGE -f docker/Dockerfile.kindletool ."
  fi
fi

[ -x "$ROOT/package/kindletool" ] || die "package/kindletool not found or not executable
   Download the Linux x86-64 build: https://github.com/NiLuJe/KindleTool/releases"

BASE="$(basename "$OUT")"
# kindletool refuses any output name not matching update*.bin, because that is
# what the Kindle's updater scans for. Build into a separate dir so the output
# never lands among the inputs.
DEST="$(mktemp -d "$ROOT/.pkg-out.XXXXXX")"
trap 'rm -rf "$STAGE" "$DEST"' EXIT

echo "==> packaging $(cd "$STAGE" && ls | tr '\n' ' ')"
# shellcheck disable=SC2046  # deliberate word split: one argument per input file
if ! docker run --rm --platform linux/amd64 \
  -v "$STAGE":/stage -v "$DEST":/out \
  -v "$ROOT/package/kindletool":/usr/local/bin/kindletool:ro \
  -w /stage "$IMAGE" \
  kindletool create ota -d "$DEVICE" $(cd "$STAGE" && ls) "/out/$BASE" >/dev/null
then
  # "executable file not found" or "Cannot stat" here almost always means the
  # bind mounts came up EMPTY because this checkout sits outside the paths your
  # Docker VM shares. colima and Docker Desktop share $HOME by default and not
  # much else.
  die "kindletool failed. If it could not see its input, this checkout
     $ROOT
   is probably outside the directories your Docker VM shares. Move it under
   \$HOME, or add its parent in Docker Desktop > Settings > Resources > File
   sharing (colima: restart with --mount)."
fi

cp "$DEST/$BASE" "$OUT"
echo "==> $OUT"

# Verify rather than trust. The payload is nibble-swapped and XORed with 0x7A
# after a 64-byte header; the device ID lives at offset 0x0C.
python3 - "$OUT" "$DEVICE" <<'PY'
import sys, gzip, io, tarfile
path, device = sys.argv[1], sys.argv[2]
want = {"k3g": 0x06, "k3w": 0x08, "k3gb": 0x0A}[device]
d = open(path, "rb").read()
assert d[:4] == b"FC02", f"bad magic {d[:4]!r} — not an FC02 OTA V1 package"
got = d[0x0C]
assert got == want, f"device id 0x{got:02X} in header, expected 0x{want:02X} for {device}"
body = bytes(((b ^ 0x7A) >> 4 | (b ^ 0x7A) << 4) & 0xFF for b in d[64:])
assert body[:2] == b"\x1f\x8b", "payload is not gzip — deobfuscation failed"
names = tarfile.open(fileobj=io.BytesIO(gzip.decompress(body))).getnames()
assert not any("/" in n for n in names), f"files must be at archive root, got {names}"
assert any(n.endswith(".sig") for n in names), "unsigned — kindletool did not sign"
print(f"    magic FC02, device 0x{got:02X} ({device}), signed, {len(names)} entries at root")
print("    " + " ".join(sorted(names)))
PY

cat <<EOF

Install (device must already be JAILBROKEN — see ../METHOD.md):
  1. Copy EXACTLY ONE .bin to the root of the Kindle USB volume.
  2. Eject AND unplug. While the cable is connected the device has handed
     /mnt/us to the host and cannot see the file.
  3. [HOME] -> [MENU] -> Settings -> [MENU] -> Update Your Kindle
     Missing or greyed out? Restart the Kindle — it only scans on boot.

The updater deletes the .bin afterwards, on success and on failure alike.
If the file is gone, it ran.
EOF
