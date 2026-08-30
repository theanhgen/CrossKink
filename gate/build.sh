#!/usr/bin/env bash
# Cross-compile the toolchain gate for the Kindle 3. Run INSIDE the Linux container
# (the prebuilt koxtoolchain is x86-64 ELF and cannot run on macOS).
#
#   TC=$HOME/x-tools/arm-kindle-linux-gnueabi
#   gate/build.sh   ->   gate/gate-arm
set -euo pipefail
TC="${TC:-$HOME/x-tools/arm-kindle-linux-gnueabi}"
CXX="$TC/bin/arm-kindle-linux-gnueabi-g++"
OUT="$(dirname "$0")/gate-arm"

[ -x "$CXX" ] || { echo "!! no compiler at $CXX"; exit 1; }
echo "==> $("$CXX" --version | head -1)"

# -static-libstdc++/-static-libgcc are MANDATORY: the device's libstdc++ is GCC-4 era.
# -lrt: clock_gettime lived in librt before glibc 2.17.
"$CXX" -std=gnu++2a -O2 \
  -static-libstdc++ -static-libgcc \
  -o "$OUT" "$(dirname "$0")/gate.cpp" \
  -lpthread -lrt

echo "==> built $OUT"
file "$OUT"

echo
echo "==> GLIBC symbol versions required (must not exceed the device's glibc):"
"$TC/bin/arm-kindle-linux-gnueabi-objdump" -T "$OUT" 2>/dev/null \
  | grep -oE 'GLIBC_[0-9]+\.[0-9]+' | sort -u -V || echo "  (none — fully static?)"

echo
echo "==> forbidden 2.6.27-era syscalls (must be empty):"
"$TC/bin/arm-kindle-linux-gnueabi-objdump" -T "$OUT" 2>/dev/null \
  | grep -oE '\b(accept4|pipe2|epoll_create1|eventfd2|dup3|inotify_init1)\b' | sort -u \
  || echo "  none — good"
