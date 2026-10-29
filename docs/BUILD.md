# Building the reference port

No precompiled privileged modules are distributed. The commands below distinguish **host** Android modules from the **guest** Linux kernel. A successful guest kernel build says nothing about the compatibility of an Android module.

## 1. Android host modules

Use a prepared build/DDK for the exact Android ABI and matching Android Clang. Kernel headers for a generic Debian kernel, or merely another GKI 6.1 release, are insufficient. The reference host uses Android Clang 17 and a GKI/DDK environment with compatible exported symbols, version CRCs and the audited Gunyah structures. Run the compilation on the platform supported by that toolchain; an x86_64-only DDK cannot simply execute natively in an ARM64 chroot.

```sh
export KDIR=/path/to/prepared/android/kernel/build
export PATH=/path/to/matching/android/clang/bin:$PATH
# For cross builds set TARGET_CC and its target libbpf/sysroot appropriately.
scripts/build-host.sh
```

This produces the reserve pool, label adapter, backing-layout helper, runtime-share helper and `kapi_check` under `build/host/`. The checker needs target `libbpf`, `libelf` and zlib. For a native ARM64 build install `libbpf-dev libelf-dev zlib1g-dev`.

`modules/grow-host/vm_mgr.h` is the GPL private Gunyah layout of the reference port. Compile-time offsets and a matching live BTF fingerprint are mandatory. `gunyah_grow_host` resolves an unexported RM function through a temporary kprobe, verifies its CFI type hash using `copy_from_kernel_nofault`, then unregisters the probe. It does not disable CFI. A plain `memcpy` from an address before a function can be compiled into `fortify_panic`; keep the no-fault read and inspect the resulting initialization code.

Reserve module installation in KernelSU is not required by the supervisor: it loads its own compatible `.ko` normally. Do not enable a second autoload/service instance of the reserve module alongside this supervisor. No force-loading flags are used.

## 2. Userspace shims and guest agent

Build on ARM64 Debian, or use an ARM64 cross compiler via `CC`:

```sh
apt-get install build-essential
scripts/build-shims.sh
```

The shims intentionally link without glibc startup files so Android's Bionic loader can supply their libc symbols. The guest agent is a separate, statically linked Linux executable. Mixing x86_64 and ARM64 artifacts will not work.

## 3. Guest Linux kernel

For `custom-fixed` and `grow`, build Linux 6.18.16 with the supplied guest config:

```sh
apt-get install clang lld llvm make gcc g++ bison flex bc libssl-dev libelf-dev
JOBS=4 scripts/build-guest.sh
```

The script downloads a pinned kernel archive and verifies SHA-256 before unpacking. The reference compiler is native Clang/LLD 19. If your distribution's default LLVM differs, put the intended LLVM toolchain first on PATH. The config enables Docker facilities, virtio PCI/storage/network, restricted DMA, modules and memory hotplug with automatic onlining. Disabling the restricted DMA support can prevent a protected guest from booting.

`build/guest/Image` and `gunyah_grow_guest.ko` must always come from the **same build**. The guest module exclusively owns its RM receive queue; do not combine this raw-HVC client with a second native guest RM driver consuming that queue.

For `classic`, `split` and `shared`, obtain the pinned upstream reference kernel:

```sh
scripts/fetch-legacy-kernel.sh
```

The downloader checks its content hash. It links to an upstream artifact; no kernel binary is redistributed here. See THIRD_PARTY.md for provenance and upstream source/configuration information. A new guest kernel is not a replacement Android boot image.

## 4. Fresh root filesystem and initramfs

Configure the state location first, then follow QUICKSTART.md. `scripts/create-rootfs.py` bootstraps public Debian packages and creates new SSH keys locally. It refuses to overwrite an existing image or partial rootfs tree.

```sh
./droidvm configure --android-rootfs /android/path/to/debian/rootfs
sudo python3 scripts/create-rootfs.py
sudo python3 scripts/make-initrd.py
sudo ./droidvm doctor --profile grow
```

The initramfs mounts root/workspace, sets the virtio network address and masks automatic Docker startup until the supervisor is ready. For `grow`, the telemetry/accept agent is copied to guest `/run` and started as a **systemd service**. Leaving it as an initramfs child is not a reliable service lifecycle across switch_root.

All generated material stays under ignored `build/` or the local state directory. Do not add kernel binaries, images, credentials or runtime logs to a source commit.

## Source-only checks

`make check` validates public-source hygiene, Python syntax, CLI import and shell syntax without a phone. `contrib/github-actions-source.yml` is an optional GitHub Actions workflow template; enabling it requires the repository account/token to have workflow permissions. It is not necessary to start a VM.

For Android kernel/module development, consult the [Android kernel documentation](https://source.android.com/docs/core/architecture/kernel) and the [KernelSU module guide](https://kernelsu.org/guide/module.html). KernelSU systemless packaging does not make an ABI-incompatible `.ko` safe to load.
