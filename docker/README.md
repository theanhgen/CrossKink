# docker/

The cross-build environment. Two images, neither of which builds a compiler — koxtoolchain
publishes prebuilt toolchains as release assets, so there is nothing to compile.

| File | Purpose |
|---|---|
| `Dockerfile` | the cross-build image: Debian 12 + koxtoolchain's `arm-kindle-linux-gnueabi` (GCC 14.4.0) |
| `Dockerfile.kindletool` | runtime deps for `kindletool`, used to build and sign FC02 update packages (see `METHOD.md`). Optional — `package/build-package.sh` falls back to `kindle-xc`, which already carries libarchive and nettle |

Both are `linux/amd64` because the toolchain ships x86-64 ELF binaries. On Apple Silicon that
means Rosetta:

```sh
colima start --cpu 6 --memory 12 --vz-rosetta
```

2 CPU / 4 GiB — colima's default — is not enough. 6 / 12 works.

## Fetching the toolchain

`Dockerfile` expects `kindle.tar.zst` in the build context. It is not committed here (95 MB).

```sh
curl -LO https://github.com/koreader/koxtoolchain/releases/download/2026.08/kindle.tar.zst
shasum -a 256 kindle.tar.zst
# 4aa5751f5e29268d91999d4754d14ce0d6db5510e46b26bd7b2a8ce25fe09ad8
```

Release `2026.08`, published 2026-08-02. The **`kindle`** asset is the legacy K2/DX/K3 target.
Not `kindle5`, and there is no `kindle-legacy` — that name belongs to KOReader's build target,
which consumes this toolchain.

```sh
docker build --platform linux/amd64 -t kindle-xc -f docker/Dockerfile .
```

The tarball is extracted **inside** the image, never onto a bind mount: it contains read-only
directories (`r-xr-xr-x`) that tar cannot populate over virtiofs.

## Building

See [`../BUILD.md`](../BUILD.md). Short version — `ulimit -s unlimited` and `-j1` are both
mandatory and both were measured, not guessed:

```sh
docker run --rm --platform linux/amd64 --ulimit stack=-1 -v "$PWD":/proj -w /proj kindle-xc bash -c '
  ulimit -s unlimited
  cmake -S platform/kindle -B build -DCMAKE_TOOLCHAIN_FILE=$PWD/cmake/kindle-toolchain.cmake -DCMAKE_BUILD_TYPE=MinSizeRel
  cmake --build build -j1
'
```
