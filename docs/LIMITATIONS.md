# Limitations and security model

## Supported use

The guest is a full Linux operating environment: persistent storage, its own kernel, standard services and applications. It can be the primary **working environment** for a user who accesses it through SSH or a separately configured remote desktop. Android remains the phone's host operating system, power manager and hardware owner.

This project does not boot Linux directly on the display or replace Android's telephony/media stack. There is no accelerated desktop integration, physical GPU/display/input/camera/modem passthrough, nested virtualization, migration or automatic forwarding of all container ports.

## Privileges and trust

The reference path uses root, vendor/private kernel interfaces and crosvm `--disable-sandbox`. Host extensions run with kernel privileges. Run trusted guest images and trusted workloads. Do not use this configuration as isolation from malicious guests, and do not interpret `--protected-vm-without-firmware` as a confidential-computing guarantee.

The guest serial console is root-autologin for supervision. Its FIFO/log/state directory must remain root-only. Guest SSH is key-only with per-installation keys and a pinned generated host key. No shared credentials are included. Changes to that access model require their own review.

## Memory

- Nominal guest assignment is larger than Linux MemTotal because of kernel/DMA/metadata reservations.
- MemAvailable is an estimate, not a promise of physically contiguous pages. Reservation can fail earlier because of fragmentation.
- The Android reserve is a policy margin, not a proof against all firmware failures or memory spikes.
- Runtime growth is one-way until shutdown; guest OOM is possible at the configured cap or after a refused addition.
- No automatic guest swap is created by the image builder. If you add swap, record it when comparing RAM demand and responsiveness.
- 4 GiB is the supported policy/address-space ceiling of this reference port, not a claim about the SoC's maximum.
- Live page return, ballooning and virtio-mem are not interchangeable with the legacy RM path implemented here.

## Firmware and failure handling

A Gunyah/RM call can block below the VMM. A host driver bug, CFI violation, invalid layout or firmware failure can affect the whole phone. Small guest memory does not make kernel extensions harmless. ABI gates and bounded descriptors reduce known risks but cannot guarantee firmware correctness.

A guest allocation failure normally belongs inside the guest. A phone reset before a VM exists is a host-side problem; do not attribute every reset to guest OOM. Preserve phase markers and both host/guest logs before attempting another launch.

## Device lifecycle

The supervisor holds an Android wake lock for the VM lifetime; expect battery use and heat. Suspend/resume with a live protected VM is not a supported lifecycle. Shut down the guest before suspending, updating modules, updating Android or changing its mount namespace. Android updates can invalidate the release/BTF gate and require a new port.

## Source-release scope

The low-level memory paths come from an exercised reference implementation; the public CLI integrates them around a newly created generic Debian image. Do not extrapolate support to other devices or workloads. Rebuild modules against the audited host ABI and keep guest Image/modules in the same build. The repository intentionally contains no personal workload, disk image, VPN configuration or reusable private key.
