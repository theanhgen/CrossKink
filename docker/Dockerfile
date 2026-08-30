# Cross-build environment for CrossInk on Kindle 3 (ARMv6).
# amd64 because koxtoolchain ships x86-64 ELF binaries; Rosetta translates on Apple Silicon.
FROM --platform=linux/amd64 debian:12

RUN apt-get update -qq && apt-get install -y -qq \
      zstd file binutils make cmake git python3 xz-utils \
    && rm -rf /var/lib/apt/lists/*

# Extract into the image's own fs, NOT a bind mount: the tarball has read-only
# directories (r-xr-xr-x) which tar cannot populate over virtiofs.
COPY kindle.tar.zst /tmp/kindle.tar.zst
RUN mkdir -p /opt && cd /opt \
    && zstd -dc /tmp/kindle.tar.zst | tar -xf - \
    && rm /tmp/kindle.tar.zst \
    && /opt/x-tools/arm-kindle-linux-gnueabi/bin/arm-kindle-linux-gnueabi-g++ --version | head -1

ENV TC=/opt/x-tools/arm-kindle-linux-gnueabi
ENV PATH=/opt/x-tools/arm-kindle-linux-gnueabi/bin:$PATH
WORKDIR /work
