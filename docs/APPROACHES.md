# Five approaches and why grow-only was selected

All profiles use the same hardware virtualization path. They differ in how host RAM is prepared, described to the firmware and made available to the Linux guest. Complexity below is implementation/porting complexity, not an assurance of isolation.

## 1. classic: bounded fixed RAM

**Needs:** the reserve module, compatible legacy guest Image, one pre-start ioctl guard and the generic rootfs/initramfs. Default assignment is 1024 MiB.

**Mechanism:** reserve suitable 2 MiB backing before VM creation and validate the registered physical layout before the initial RM memory operation.

**Advantages:** the smallest amount of compatibility code; a useful starting point for a device port; fixed resource use is easy to reason about.

**Limits:** a large parcel can exceed RM's initial scatter/gather descriptor capacity even if total free memory looks sufficient. The packaged policy stays at the known small working size. Root does not make physically fragmented pages contiguous.

## 2. split: two main parcels

**Needs:** classic prerequisites plus the PID-scoped `gunyah_ram_labels` module and split-memory/FDT shim. Default assignment is 1664 MiB; the reference implementation bounds this strategy at 1792 MiB.

**Mechanism:** split the main backing into two pieces, retain the restricted DMA region, and adapt legacy parcel labels only for the current VMM. Firmware/device-tree descriptions must match the new layout.

**Advantages:** larger useful memory without maintaining a custom guest kernel; fewer descriptors per main parcel.

**Limits:** more dependencies on private RM behavior and labels; still fixed RAM; a fragmentation ceiling remains. The adapter is armed for one VMM PID and disarmed only after normal shutdown. Never use it as a global label-rewriting hook.

## 3. shared: large fixed assignment

**Needs:** the reserve pool, `gunyah_ram_prepare`, shared-parcel shim, legacy guest kernel and sufficient Android headroom for the entire 4096 MiB before boot.

**Mechanism:** register fresh backing as small parcels. A root-only helper chooses and partitions suitable pinned pages and installs descriptor arrays in the legacy VM mapping structures, preserving normal driver teardown ownership. Each parcel is bounded to 512 SG entries. The extra backing preparation includes a transient 192 MiB candidate allowance; do not budget only the nominal guest RAM.

**Advantages:** reaches a much larger fixed capacity while retaining the existing guest kernel.

**Limits:** high porting complexity, exact private structure offsets, larger upfront reservation, transient metadata costs, and sensitivity to physical fragmentation. Reading MemAvailable alone cannot prove a start is possible. All allocations/checks must finish before installing a layout; no arbitrary repartitioning of live used guest pages is permitted.

## 4. custom-fixed: guest kernel control

**Needs:** the shared strategy's host-side preparation and a separately built guest Linux kernel/configuration. Default assignment is 3072 MiB; a 4096 MiB policy limit exists in the implementation family but the default favors headroom.

**Mechanism:** allocate fixed RAM, but choose the Linux features independently of Android. The supplied configuration provides systemd/Docker primitives, virtio, restricted DMA and memory-hotplug infrastructure.

**Advantages:** guest features are reproducible and maintained in source, rather than being an accidental property of a downloaded guest Image. This is the foundation for a general Linux environment.

**Limits:** guest-kernel maintenance is an additional task. It does not repair Android's missing host features, and fixed allocation still holds idle RAM until shutdown. Build Image and guest modules together.

## 5. grow: runtime memory addition

**Needs:** the custom guest kernel, `gunyah_grow_host`, `gunyah_grow_guest`, an exclusive RM transport client, a managed guest agent and the grow shim/controller.

**Mechanism:** boot at 256 MiB. When guest MemAvailable drops below its reserve target, obtain a fresh 128 MiB host-backed region, register it with the existing VMM, SHARE it through RM, have the protected guest ACCEPT the parcel, then add and online it in Linux. The same guest keeps running. Track each phase separately: reserved is not shared, shared is not accepted, and accepted is not yet usable Linux RAM.

**Advantages:** avoids reserving the full possible footprint at boot; the guest can stay small when idle and grow for actual demand. The final operating model is a persistent Linux system or a VM started only when needed.

**Limits:** most complex path; it relies on the reference firmware/RM and audited host layouts. No live shrink or page donation back to Android is implemented. The fixed policy ceiling is 4096 MiB, with 30 additions after the initial 256 MiB. Android pressure or failed hugepage reservation may stop growth earlier.

The high PCI window must leave the hot-add IPA space free. This port moves its advertised start to 8 GiB and uses RAM up to guest address 6 GiB. This is a guest address-space adjustment, not a change to Android PCI resources.

The VMM's initial GuestMemory object is not enlarged. Protected virtio I/O continues to bounce through the initial restricted DMA pool. Consequently, migration, arbitrary direct DMA and new passthrough devices are not supported by simply adding RAM.

## Why this path

A chroot/PRoot/container could not supply an independent kernel. Replacing Android's boot kernel would lose the principal goal of retaining the vendor kernel and its hardware support. CPU emulation is a different performance/compatibility compromise and is not used here. Gunyah already present in the reference platform offered a hardware path; targeted host helpers solved allocation and legacy-interface constraints, while a custom guest kernel supplied Linux features.

Grow-only was selected over memory return because shutdown already gives a clear, auditable ownership boundary. Releasing live shared memory safely needs coordinated device quiescing, guest offlining, RM unmapping and host unpinning; none of those can be replaced by dropping a userspace mapping. It is better to offer bounded growth and normal teardown than to claim a balloon implementation that this interface does not provide.
