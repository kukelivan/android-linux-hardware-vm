# Requirements and compatibility

## Host: requirements root cannot replace

The phone must already expose a usable Gunyah implementation: firmware capabilities, vendor RM interface, `/dev/gunyah`, and a crosvm build with the `gunyah` backend and the legacy memory ioctls used by this port. Android AVF/pKVM or an ARM CPU with virtualization instructions alone is not sufficient. There is no software-emulation fallback in this project.

The reference is ASUS ROG Phone 8 / AI2401, Android 16, 12 GB RAM, kernel release and BTF recorded in `devices/reference.json`. The record is an explicit ABI gate, not a list of approximately compatible kernels. Do not make an incompatible kernel appear compatible by copying its current fingerprint into the reference file.

Real root is required for module loading, namespace entry, memory locking, pagemap access and network devices. KernelSU was the root/module-management environment of the reference port; compatible root and module-loading support are the underlying requirements. A PRoot UID of zero does not provide those capabilities.

The host needs loadable modules, kprobes/vendor hooks used by the reserve module, readable kernel BTF, procfs/sysfs, network namespaces, TUN/TAP and the Android crosvm dependencies. `CONFIG_CFI_CLANG`, symbol versions and private structure layouts must match the module build. The helper ABI checks are intentionally strict.

## Chroot

Use a real ARM64 Debian chroot with `/dev`, `/dev/pts`, `/proc` and `/sys` visible. The supervisor enters Android's mount namespace and chroots through `/proc/1/root` before executing the APEX crosvm. The chroot therefore needs access to Android's PID 1 mount namespace; a separate container PID namespace without that view is not equivalent.

`--android-rootfs` is the Android-visible absolute path of the Debian rootfs, **not** the rootfs path inside the chroot. The same file must have the same inode from both views. `doctor` checks this for the guest root image.

Example preparation from an already-root Android shell (adapt paths; avoid stacking duplicate mounts):

```sh
ROOTFS=/data/data/com.termux/files/usr/var/lib/proot-distro/containers/debian/rootfs
/system/bin/mount --bind "$ROOTFS" "$ROOTFS"
/system/bin/mount --bind /dev "$ROOTFS/dev"
/system/bin/mount --bind /dev/pts "$ROOTFS/dev/pts"
/system/bin/mount -t proc proc "$ROOTFS/proc"
/system/bin/mount --bind /sys "$ROOTFS/sys"
/system/bin/chroot "$ROOTFS" /usr/bin/env -i \
  HOME=/root USER=root LOGNAME=root TERM=xterm-256color \
  PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin \
  /bin/bash --login
```

The rootfs must exist before this step. Do not unmount the chroot while a guest is running.

## Debian tools

Runtime: Python 3, `iproute2`, `util-linux` (`unshare`, `nsenter`), `slirp4netns`, `e2fsprogs`, `openssh-client` and core GNU utilities. Image preparation additionally needs `debootstrap`; initramfs needs **statically linked** `busybox-static`.

```sh
apt-get install python3 iproute2 util-linux slirp4netns e2fsprogs \
  openssh-client debootstrap busybox-static curl ca-certificates
```

The host does not need a working Docker daemon. Docker is installed in the guest.

## RAM and disk space

For fixed profiles, budget at least guest RAM plus `max(2048 MiB, 20% of Android MemTotal)` plus 512 MiB for the supervisor, metadata and transient allocations. Physical fragmentation can still prevent reservation even when MemAvailable is sufficient. The guard must refuse such a start.

The grow-only profile starts at 256 MiB, including 64 MiB of reserved DMA memory; initial Linux MemTotal is consequently smaller. Each 128 MiB addition requires Android headroom before reservation and again afterwards. Its guest free-memory target is 256 MiB to absorb bursts. The 4 GiB limit can be reached only if Android can spare it.

Default sparse disks have logical sizes 8 GiB (root) and 12 GiB (workspace). Real usage grows with packages, images and user data. Source trees and kernel builds require substantial additional storage. No disk images are downloaded from a personal environment or shipped in this repository.

## Porting another device

1. Establish Gunyah firmware/driver/crosvm compatibility.
2. Obtain the exact vendor kernel source, config, symbols and compiler/DDK.
3. Audit the live BTF, callbacks, private structures and migratetype values.
4. Rebuild all host helpers; preserve CFI and module-version checks.
5. Audit guest IPA layout, reserved DMA and PCI windows.
6. Start with bounded, small fixed memory; verify normal shutdown and reserve release before enabling runtime growth.

Creating a new `devices/*.json` is the final record of that audit, not a substitute for it.
