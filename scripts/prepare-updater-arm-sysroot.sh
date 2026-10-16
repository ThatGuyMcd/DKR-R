#!/usr/bin/env bash
set -euo pipefail
project_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
environment="${DKR_ARM64_ENVIRONMENT:?Set the private ARM64 environment directory}"
sysroot="${DKR_ARM64_SYSROOT:-$environment/sysroot}"
archives="$environment/updater-archives"
mkdir -p "$archives"
cd "$archives"
apt-get -o Dir::State::lists="$environment/lists" -o APT::Architecture=arm64 \
  -o Dir::Etc::sourcelist="$project_root/packaging/linux/arm64/ubuntu.sources" \
  -o Dir::Etc::sourceparts=- download libcurl4-openssl-dev:arm64 libcurl4t64:arm64 \
  libnghttp2-14:arm64 librtmp1:arm64 libssh-4:arm64 libpsl5t64:arm64 libldap2:arm64 libsasl2-2:arm64
for package in "$archives"/*.deb; do
  dpkg-deb -x "$package" "$sysroot"
done
readelf -d "$sysroot/usr/lib/aarch64-linux-gnu/libcurl.so.4" | sed -n '/NEEDED/p'
