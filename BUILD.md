# Building CrossInk for the Kindle 3

**Status: builds and links.** 5.6 MB stripped ARMv6 binary, `GLIBC_2.4` max, no
post-2.6.26 syscalls.

```
build/crossink: ELF 32-bit LSB executable, ARM, EABI5 version 1 (SYSV),
                dynamically linked, interpreter /lib/ld-linux.so.3,
                for GNU/Linux 2.6.22
```

## Quick start

The toolchain is x86-64 Linux ELF, so on Apple Silicon everything runs in a
container under Rosetta.

```sh
colima start --cpu 6 --memory 12 --vz-rosetta
docker build --platform linux/amd64 -t kindle-xc -f docker/Dockerfile .   # see docker/README.md

docker run --rm --platform linux/amd64 --ulimit stack=-1 -v "$PWD":/proj -w /proj kindle-xc bash -c '
  ulimit -s unlimited
  cmake -B build -DCMAKE_TOOLCHAIN_FILE=cmake/kindle-toolchain.cmake -DCMAKE_BUILD_TYPE=MinSizeRel .
  cmake --build build -j1
'
colima stop
```

## The three rules that make it work

**1. `ulimit -s unlimited`.** Without it `cc1plus` segfaults partway through,
reported as `internal compiler error: Segmentation fault`. It is stack
exhaustion in the C++ front end on CrossInk's deeply-templated activity classes,
not a broken toolchain: the same file always compiles cleanly on its own. With
unlimited stack the ICEs disappear. A large *finite* limit (512 MB) is **not**
enough - that was measured, not assumed.

**2. Build serially (`-j1`).** Parallel builds crash the compiler even with a
12 GB VM. Reproduced at `-j4` and `-j5`; a different file dies each time.

**3. Give the VM real resources.** 2 CPU / 4 GiB is the colima default and is
not enough. 6 CPU / 12 GiB works.

A full build takes roughly 20 minutes.

## Retrying

Compiler crashes are occasional and transient. A wrapper that retries on
`internal compiler error` / `Segmentation fault` but stops immediately on
`multiple definition`, `undefined reference` or `fatal error:` gets through.

Do **not** classify with a bare `" error:"` pattern - it matches GCC's own ICE
line and will abort on precisely the failure you meant to retry.

## What CMakeLists.txt has to reproduce

It mirrors `platformio.ini`'s `[simulator-base]`, because PlatformIO's `native`
platform cannot cross-compile. Non-obvious parts:

| Concern | Why |
|---|---|
| `lib/*` include dirs | PlatformIO's LDF adds all 29 automatically; CMake needs them listed |
| `lib_ignore = hal, HalClockSim` | `lib/hal` is the Arduino HAL our `platform/kindle` replaces at link time |
| exclude `lib/*/third_party/` | `miniz_impl.c` does `#include "../third_party/miniz.c"` - compiling it too duplicates every symbol |
| PNGdec `*.c` as well as `*.cpp` | it vendors its own zlib; globbing only C++ leaves `inflate`/`crc32` undefined |
| **not** PNGdec's `.S` | that is ESP32-S3 SIMD assembly |
| `CROSSPOINT_SIMULATOR_PROJECT_WEBSERVER` | the simulator's web server is `#ifndef`-guarded on it; without it you get two `CrossPointWebServer` |
| exclude `firmware_link_stubs.cpp` | duplicates `MySerialImpl` (in `lib/Logging`) and the uzlib checksums (in `lib/uzlib`). Its comments claiming those are missing upstream are stale at our pinned SHA |
| exclude `SimulatorHomeKeyInput.cpp` | includes `<SDL.h>` unconditionally though its only SDL use is behind `SIMULATOR_DEVICE_X4PRO`; `platform/kindle` has a copy without it |
| `-include kindle_compat.h` | glibc 2.5 has no `strlcpy`/`strlcat` |
| `platform/kindle/openssl/md5.h` | the simulator's `MD5Builder_linux.h` wants OpenSSL; we supply just MD5, verified against RFC 1321 vectors |

`platform/kindle` **must** come first on the include path: its `EInkDisplay.h`
shadows the simulator's and sets the panel to 600x800 portrait.

## Portability findings

Across ~230 translation units, CrossInk's own code needed exactly two
accommodations: `strlcpy` and one stray `SDL.h` include. Everything else was
build-system plumbing. C++20, threads, atomics, exceptions and `std::filesystem`
all work against glibc 2.5 - see `gate/results/gate-result-2026-08-28.txt`.

## Runtime dependencies

```
librt.so.1  libpthread.so.0  libm.so.6  libc.so.6  ld-linux.so.3
```

All present on the stock K3. `libstdc++` is statically linked
(`-static-libstdc++ -static-libgcc`), which is what keeps the maximum symbol
requirement at `GLIBC_2.4` against the device's 2.5.
