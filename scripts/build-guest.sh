#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu
repo=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
version=6.18.16
mkdir -p "$repo/build/downloads" "$repo/build/guest" "$repo/build/linux"
archive="$repo/build/downloads/linux-$version.tar.xz"
if [ ! -f "$archive" ]; then curl --fail --location --retry 4 -o "$archive" "https://cdn.kernel.org/pub/linux/kernel/v6.x/linux-$version.tar.xz"; fi
printf '%s  %s\n' 4f21c01f4d04c1d1b3ed794153f8900802c92497be620b07c4869530f2d28ee3 "$archive" | sha256sum -c -
if [ ! -d "$repo/build/linux/linux-$version" ]; then tar -xJf "$archive" -C "$repo/build/linux"; fi
src="$repo/build/linux/linux-$version"
obj="$repo/build/guest/obj"
make -C "$src" O="$obj" ARCH=arm64 LLVM=1 KCONFIG_ALLCONFIG="$repo/guest/kernel.config" allnoconfig
make -C "$src" O="$obj" ARCH=arm64 LLVM=1 -j"${JOBS:-4}" Image modules
make -C "$src" O="$obj" ARCH=arm64 LLVM=1 M="$repo/modules/grow-guest" modules
cp "$obj/arch/arm64/boot/Image" "$repo/build/guest/Image"
cp "$obj/.config" "$repo/build/guest/config"
cp "$repo/modules/grow-guest/gunyah_grow_guest.ko" "$repo/build/guest/"
