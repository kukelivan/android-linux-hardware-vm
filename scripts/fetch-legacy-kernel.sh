#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
set -eu
repo=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
mkdir -p "$repo/build/guest"
curl --fail --location --retry 4 -o "$repo/build/guest/legacyImage" https://raw.githubusercontent.com/Droid-VM/DroidVM-Prebuilt-Root/82c59c2834cb7e2f0284b03849fc921057804b75/manual-build/arm64-v8a/usr/share/droidvm/vmlinuz
printf '%s  %s\n' 7cfd887285ff35fdeca4564ed92a306f6b1b4605876468805d9b2653ad37afaf "$repo/build/guest/legacyImage" | sha256sum -c -
