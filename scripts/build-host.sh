#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu
repo=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
: "${KDIR:?Set KDIR to the prepared Android kernel/DDK build directory matching the target ABI}"
mkdir -p "$repo/build/host"
for module in reserve labels prepare grow-host; do
 make -C "$KDIR" M="$repo/modules/$module" ARCH=arm64 LLVM=1 modules
 case "$module" in
  reserve) name=gh_hugepage_reserve;; labels) name=gunyah_ram_labels;; prepare) name=gunyah_ram_prepare;; grow-host) name=gunyah_grow_host;;
 esac
 cp "$repo/modules/$module/$name.ko" "$repo/build/host/"
done
# A target-native libbpf executable. Cross builds need the target libbpf/sysroot.
"${TARGET_CC:-cc}" -O2 -I"$repo/modules/reserve/abi" -o "$repo/build/host/kapi_check" "$repo/modules/reserve/abi/kapi_check.c" -lbpf -lelf -lz
