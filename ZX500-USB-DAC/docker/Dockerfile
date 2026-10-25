# ZX500 USB-DAC build environment.
#
# One image, Ubuntu 16.04, linux/arm64. It compiles the kernel, the bridge and
# the resampler test natively — nothing here is cross-compiled from x86.
#
# Why a release that went end-of-life in 2021, deliberately:
#
#   * GCC 4.9 is the only toolchain whose kernel Image fits Sony's
#     28,940,800-byte slot. A modern compiler produces roughly 32 MB.
#   * Its archive is frozen. Packages are never updated, so a rebuild years from
#     now installs exactly what it installs today: the compiler cannot acquire
#     new warnings that trip -Werror, and no version pin can go stale and fail
#     to resolve. An actively maintained release gives up both of those.
#
# The bridge is built here too, with xenial's own GCC 5.4. It is ordinary C and
# does not need a recent compiler; building it against a frozen archive is worth
# more than building it against a newer one.
#
# The constant `--platform=linux/arm64` is deliberate and BuildKit warns about it
# (FromPlatformFlagConstDisallowed). Do not "fix" it: this image must be arm64
# whatever the host is, because it compiles aarch64 binaries natively.
#
# Build with docker/zxbuild images.

# Xenial's arm64 packages live on ports.ubuntu.com and are still served there.
# When Ubuntu eventually moves end-of-life releases off it, they go to
# old-releases.ubuntu.com and stay indefinitely — that archive still carries
# releases from 2004. Point this at the new location if that day comes:
#   --build-arg APT_MIRROR=http://old-releases.ubuntu.com/ubuntu-ports
ARG APT_MIRROR=http://ports.ubuntu.com/ubuntu-ports

FROM --platform=linux/arm64 ubuntu:16.04 AS kernel
ARG APT_MIRROR
# Native arm64 GCC 4.9. This package also installs the triplet-prefixed name
# aarch64-linux-gnu-gcc-4.9, and xenial's native binutils ships the matching
# aarch64-linux-gnu-* tools, so CROSS_COMPILE works unchanged even though this
# is a native build.
ARG GCC49_VERSION=4.9.3-13ubuntu2

RUN sed -i "s|http://ports.ubuntu.com/ubuntu-ports|${APT_MIRROR}|g" /etc/apt/sources.list

# gcc/libc6-dev cover both the host tools kbuild compiles along the way (fixdep,
# kconfig, dtc) and the bridge itself; gcc-4.9 compiles the kernel.
RUN apt-get update && apt-get install -y --no-install-recommends \
        "gcc-4.9=${GCC49_VERSION}" \
        gcc libc6-dev binutils make bc bison flex libssl-dev \
        cpio kmod git rsync file xz-utils ca-certificates python3 \
    && rm -rf /var/lib/apt/lists/*

# Source trees live in a volume owned by root; keep git from refusing them.
RUN git config --global --add safe.directory '*'

ENV ARCH=arm64 \
    CROSS_COMPILE=aarch64-linux-gnu- \
    KERNEL_CC=aarch64-linux-gnu-gcc-4.9 \
    CC=gcc \
    BUILD_CWD=/tmp
LABEL org.opencontainers.image.title="ZX500 USB-DAC build environment"
WORKDIR /work
RUN aarch64-linux-gnu-gcc-4.9 --version | head -1 \
 && aarch64-linux-gnu-ld --version | head -1 \
 && gcc --version | head -1

# Kept so `--target userspace` still resolves; both stages are the same image.
FROM kernel AS userspace
