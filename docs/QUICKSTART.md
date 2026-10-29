# Quick start on the reference host

Read REQUIREMENTS.md before installing privileged code. This release requires a matching Gunyah device port; it is not an Android-app installer.

## Preparation order

1. Enter a real rooted Debian ARM64 chroot with Android's `/dev`, `/proc`, `/sys` and PID-1 mount namespace accessible.
2. Install runtime/image tools listed in REQUIREMENTS.md.
3. Build host modules against the audited Android DDK/ABI (`scripts/build-host.sh`).
4. Build ARM64 userspace shims and the static agent (`scripts/build-shims.sh`).
5. Build the custom guest Image and matching module (`scripts/build-guest.sh`). For legacy profiles also run `scripts/fetch-legacy-kernel.sh`.
6. Configure state and create a new public-package guest. Do not substitute a personal/work disk as a distribution artifact.

```sh
# Work from this repository's root in the Debian chroot.
export DROIDVM_STATE=/var/lib/android-linux-hardware-vm
./droidvm configure \
  --android-rootfs /data/data/com.termux/files/usr/var/lib/proot-distro/containers/debian/rootfs
python3 scripts/create-rootfs.py
python3 scripts/make-initrd.py
./droidvm doctor --profile grow
```

All lifecycle/image commands require real root. Use sudo only if your environment needs it, and preserve `DROIDVM_STATE` if you selected a custom directory.

The image builder bootstraps Debian 13 ARM64 with systemd, SSH, Docker and its separate CLI package from public Debian repositories. It disables service startup during construction, creates per-installation SSH keys, configures the virtio network, and writes new ext4 root/workspace images. It does not import existing source trees or credentials.

The bootstrap directory remains for inspection. If the final configuration/image phase was interrupted **after Debian bootstrap completed**, `python3 scripts/create-rootfs.py --resume` reuses that tree; an existing image is never overwritten. The configuration phase avoids executing guest programs on Android's nodev `/data`, including generating guest SSH host keys with a host-side prefix.

## Start and use Linux

```sh
./droidvm up grow
./droidvm status
./droidvm ssh
```

Inside the guest, ordinary package management, systemd services and Docker work against the Linux guest kernel. For example:

```sh
uname -a
cat /etc/os-release
systemctl status docker
docker info
docker run --rm hello-world
cd /workspace
```

For short commands without SSH:

```sh
./droidvm exec 'uname -a; free -m; docker info'
```

For a different memory strategy, stop the guest first:

```sh
./droidvm down
./droidvm up custom-fixed
```

The five names are `classic`, `split`, `shared`, `custom-fixed` and `grow`. Their initial assignments and kernel families are in `profiles/*.json`. Fixed profiles allocate the full amount before boot. The current profile files record the reference policies; they are not permission to expand the helpers' hard address/descriptor bounds.

## Persist data and connect services

Root changes persist in `rootfs.img`; `/workspace` persists in `workspace.img`. These are guest filesystems. A Docker bind mount uses a guest path, not a path in the Android/Debian management environment.

SSH is forwarded to Android loopback port 22222 by default. `droidvm ssh` uses locally generated credentials and a pinned guest host key. To reach a guest service, create an SSH tunnel using those keys; container ports are not automatically exposed on Android or the LAN.

## Stop

```sh
./droidvm down
```

Keep the supervisor alive while the VM runs; it owns runtime growth and teardown. Normal shutdown returns the entire reservation, stops networking, unloads owned modules and checks offline filesystems. Do not mount the disk image on the host while the guest is active. No automatic suspend/resume, forced module unload or phone reboot is part of the lifecycle.

## Local state

`config.json`, SSH keys, images, `console.log`, `events.jsonl`, `memory.jsonl` and supervisor state are local to DROIDVM_STATE. None belongs in a public source commit. Generated build artifacts stay under `build/` and are ignored by Git.

For a fixed shared guest when Android cannot spare the default 4 GiB, explicitly request a supported smaller reservation:

```sh
./droidvm doctor --profile shared --memory-mib 3072
./droidvm up shared --memory-mib 3072
```

There is no silent reduction of a requested reservation. `classic` is fixed at 1024 MiB, `split` allows 1024–1792 MiB in 64 MiB steps, `shared` and `custom-fixed` allow 3072 or 4096 MiB, and `grow` always starts at 256 MiB. Android headroom checks still apply.
