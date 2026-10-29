# Architecture and resource ownership

## Control plane

The root Debian chroot is only the management environment. crosvm executes in Android's mount namespace and the VM's dedicated network namespace. The guest runs a different ARM64 kernel through Gunyah. Android's driver mediates firmware Resource Manager operations.

`droidvm up` starts a supervisor. It holds a lifecycle lock, refuses another crosvm process, verifies host release/BTF, reserves backing, loads owned helper modules, creates network helpers, launches the VMM, waits for the guest and then starts Docker. `ssh` provides a general Linux shell; `exec` uses the private serial console for short commands. The supervisor remains alive for memory growth and cleanup.

## Data plane

- Root filesystem: fresh ext4 image on virtio block `vda`.
- Workspace: optional ext4 image on `vdb`, mounted at `/workspace`.
- Networking: virtio NIC → isolated TAP/bridge → slirp4netns → Android networking.
- SSH: loopback host forwarding only; port 22222 by default.
- Console: private FIFO plus serial log. Growth uses a separate virtio console channel.

Guest container ports are guest ports. The project automatically forwards SSH, not every published Docker port. Use an SSH tunnel or implement an explicit forward. Bind-mount paths passed to the guest Docker daemon refer to the guest filesystem; the chroot filesystem is not a virtiofs share.

## Host memory

The reserve module from Droid-VM maintains a pool of physically suitable 2 MiB pages and tracks ownership of pages served to a VMM. Pinning prevents Android from reclaiming that backing while the guest can still access it. Every parcel is checked against the conservative 512-entry bound before the RM operation. No force loading, descriptor-limit bypass or unbounded append is part of this port.

For grow-only memory, the host helper locates an already registered, fresh slot in the VM's normal mapping list and calls the existing RM share operation. It never frees the slot's pins independently. The driver owns final reclamation after VM reset/shutdown.

A protected guest must accept the new parcel into its stage-2 view. The guest module owns the raw RM queue on the supplied guest kernel, performs MEM_ACCEPT, and calls `add_memory_driver_managed`. Automatic onlining makes it ordinary System RAM. An ACK is successful only if both stages succeed.

## Address layout of grow

Initial RAM starts at IPA `0x80000000`; 256 MiB includes a 64 MiB restricted DMA reservation. Hot-add starts at `0x90000000`, proceeds in 128 MiB blocks and ends at `0x180000000` for a 4 GiB total assignment. The advertised high PCI range starts at `0x200000000`.

A PCI range originally adjacent to initial RAM can cause Linux hot-add to fail with EEXIST even after RM acceptance succeeds. Moving the guest window is necessary; repeating the same accept or freeing the accepted host pages is not a recovery.

## Shutdown

1. Ask Linux/systemd to power off and wait for the VMM to exit.
2. Stop namespace/network helpers.
3. Disarm the PID-scoped label adapter when owned.
4. Wait until served pages, active VMs and acquisition workers are idle.
5. Set the reserve target to zero and verify the pool is empty.
6. Unload only modules loaded by this supervisor, normally and with zero references.
7. Run offline filesystem checks; never mount or inspect a guest disk while a VMM owns it.

A blocked RM request or D-state VMM prevents further starts. No SIGKILL, force-unload or automatic Android reboot is used as a substitute for resource ownership.
