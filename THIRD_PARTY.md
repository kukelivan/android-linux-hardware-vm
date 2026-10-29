# Upstream sources, licenses and modifications

This integration depends on substantial upstream work. It does not claim those components as new inventions.

## Hugepage reserve module

`modules/reserve/` is the module-source subset of [Droid-VM/gh-hugepage-reserve](https://github.com/Droid-VM/gh-hugepage-reserve), tag **v12**, commit **e15c40d600dd19b2c5bcb220a92f7359c9ee0846**. The annotated tag object is `de25672e2a0403075f6aaaf1e87c971f37a40dab`. License: GPL-2.0. Upstream SPDX notices, ABI tables and design document are retained. Local changes are a module-only top-level Makefile and an ARM64 plain-`char` canonicalization fix in `abi/kapi_check.c`. The latter preserves the distinction between C `char` and `unsigned char` rather than disabling ABI checks. The top-level Makefile here builds only the kernel module; the upstream userspace harness is not vendored. Upstream's source/design comments may refer to that separate harness.

Upstream supplies a KernelSU/Magisk packaging route. This supervisor instead loads an audited module manually and tracks ownership; it does not enable autoload.

## Guest RM transport

The RM framing/raw-HVC transport in `modules/grow-guest/gunyah_grow_guest.c` is adapted from [Droid-VM/droidvm-guest-additions](https://github.com/Droid-VM/droidvm-guest-additions), branch `droidvm`, reference commit **cccd9078ead9bbecc760d5beaae20c26dd4ff5c0**, `gunyah_guest/gunyah_guest.c`. Upstream is GPL-2.0-or-later; this integration selects the GPL-2.0-compatible combined distribution.

Local integration exposes a root-only accept/hot-add ioctl, bounds the GPA window and block size, and uses the transport on a guest kernel with no competing native RM queue consumer. Upstream virtio-gpu functionality and device support are **not** implied or included by that derivation.

## Android common kernel definitions

`modules/grow-host/vm_mgr.h` retains the Qualcomm Innovation Center copyright and GPL-2.0-only SPDX notice. It corresponds to the legacy Android common Gunyah layout documented at:

- [vm_mgr.h](https://android.googlesource.com/kernel/common/+/9f6af9a6c2cc38/drivers/virt/gunyah/vm_mgr.h)
- [vm_mgr_mm.c](https://android.googlesource.com/kernel/common/+/9f6af9a6c2cc38/drivers/virt/gunyah/vm_mgr_mm.c)

It is an internal layout, not a stable public API. A matching host ABI is mandatory. The layout, label and runtime-share helpers in this repository are integration extensions; they do not replace Android's Gunyah driver.

## Linux guest kernel

The guest build script downloads Linux **6.18.16** from kernel.org:

`https://cdn.kernel.org/pub/linux/kernel/v6.x/linux-6.18.16.tar.xz`

SHA-256: `4f21c01f4d04c1d1b3ed794153f8900802c92497be620b07c4869530f2d28ee3`.

The Linux source archive contains its own full licensing and copyright notices. This repository distributes the integration configuration and external guest module, not the Linux source archive or a kernel binary.

The optional legacy Image downloader references [Droid-VM/DroidVM-Prebuilt-Root](https://github.com/Droid-VM/DroidVM-Prebuilt-Root) at commit **82c59c2834cb7e2f0284b03849fc921057804b75**, path `manual-build/arm64-v8a/usr/share/droidvm/vmlinuz`, content hash `7cfd887285ff35fdeca4564ed92a306f6b1b4605876468805d9b2653ad37afaf`. Obtain the source/configuration and corresponding-source information from that upstream project before redistributing a downloaded binary. No legacy Image is committed here.

## libfdt

`third_party/libfdt/` is from Linux 6.18.16's `scripts/dtc/libfdt`. Original David Gibson/IBM and other upstream copyright notices and SPDX identifiers are retained. Files are dual-licensed **GPL-2.0-or-later OR BSD-2-Clause**; this combined integration may use the GPL-compatible option. No claim of original authorship is made.

## Runtime dependencies not redistributed

[crosvm](https://crosvm.dev/book/) is supplied by the compatible Android platform; no APEX binary is redistributed. Debian packages, OpenSSH, Docker and slirp4netns retain their own licenses and are obtained through the user's package repositories. KernelSU/Magisk, firmware and Android are not part of this source distribution.
