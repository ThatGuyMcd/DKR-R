# Linux build image for the fold equivalence gate (scripts/fold/run_equivalence.sh)
# and the Linux reference materialization.
#
# Reconstructed from `docker history` of the local image the v1.0.11 reference
# was built with (dkr-ci-linux-full, image ID
# sha256:a40e107015464b92bff4e383545938f206590f73a6d1302516cef012e4f66439).
# That image's Ubuntu base layer (sha256:d81fb2a7...) no longer matches any
# local ubuntu:24.04, and apt resolves current package versions, so building
# this file gives an equivalent image, not that one. The gate pins the image ID
# above; publish a build of this file to a registry and pin its digest here and
# in the gate to make it reproducible (fold plan 2.2 step 4).
FROM ubuntu:24.04
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential cmake ninja-build git pkg-config python3 ca-certificates \
    libsdl2-dev libgtk-3-dev libvulkan-dev libx11-dev libxext-dev libwayland-dev \
    libcurl4-openssl-dev \
    && rm -rf /var/lib/apt/lists/*
CMD ["/bin/bash"]
