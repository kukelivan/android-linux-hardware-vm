#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu
repo=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
mkdir -p "$repo/build/shims" "$repo/build/guest"
# Run on ARM64 Debian, or supply an ARM64 cross compiler with CC.
for profile in classic split shared custom-fixed grow; do
 if [ "$profile" = classic ]; then
  "${CC:-cc}" -O2 -fPIC -shared -nostdlib -fno-stack-protector -o "$repo/build/shims/$profile.so" "$repo/shims/$profile/shim.c"
 else
  "${CC:-cc}" -O2 -fPIC -shared -nostdlib -fno-stack-protector -I"$repo/third_party/libfdt" -o "$repo/build/shims/$profile.so" "$repo/shims/$profile/shim.c" "$repo"/third_party/libfdt/*.c
 fi
done
"${CC:-cc}" -O2 -static -o "$repo/build/guest/agent" "$repo/guest/agent.c"
