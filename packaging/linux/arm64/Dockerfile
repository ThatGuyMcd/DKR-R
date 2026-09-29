FROM ubuntu:24.04
# Host tools remain amd64; only target libraries and compiler output are arm64.
RUN dpkg --add-architecture arm64
COPY ubuntu.sources /etc/apt/sources.list.d/ubuntu.sources
RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
    ca-certificates cmake ninja-build python3 pkg-config g++-aarch64-linux-gnu \
    qemu-user-static curl file binutils patchelf squashfs-tools desktop-file-utils \
    libsdl2-dev:arm64 libdbus-1-dev:arm64 zlib1g-dev:arm64 libstdc++6:arm64 \
    && apt-get clean
ENV PKG_CONFIG_LIBDIR=/usr/lib/aarch64-linux-gnu/pkgconfig:/usr/share/pkgconfig
WORKDIR /work
