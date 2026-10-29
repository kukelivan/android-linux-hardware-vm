# 0.1.0 — reference source release

Full ARM64 Linux and Docker on a supported Android/Gunyah host without replacing its stock boot kernel.

- Five memory approaches: classic fixed, split fixed, large shared fixed, custom-kernel fixed and grow-only.
- Guest-kernel configuration, fresh Debian image builder, initramfs builders and a systemd-managed memory agent.
- Root-only host helpers, guarded shims, ABI checks, Android headroom policy and normal resource teardown.
- `configure`, `doctor`, `up`, `status`, `ssh`, `exec` and `down` commands.
- English/Russian overview, requirements, build notes, architecture, approach trade-offs and limitations.
- Upstream attribution and GPL-compatible source distribution.

Reference-device bring-up: all five profiles booted a new public-package Debian image, ran Docker/ARM64 hello-world, preserved workspace data and shut down normally. See docs/REFERENCE.md for the exact assignments and artifact scope.

This is an early device-specific source port, not a universal APK, a prebuilt module distribution, a hostile-guest isolation product or a replacement Android desktop/telephony stack. No private disks, keys, VPN configuration, proprietary workload or runtime logs are included.
