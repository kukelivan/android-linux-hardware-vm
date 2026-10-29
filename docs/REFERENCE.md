# Reference bring-up record

The public supervisor was exercised on the reference phone with a **new Debian 13 ARM64 root filesystem made exclusively from public packages**. No existing personal/application image was imported.

| Profile | Bring-up assignment | Result |
|---|---:|---|
| classic | 1024 MiB | Linux boot, Docker overlay2, ARM64 hello-world, clean shutdown |
| split | 1664 MiB | Same, with PID-scoped split-parcel adapter |
| shared | 3072 MiB explicitly selected | Same, with bounded shared-parcel layout |
| custom-fixed | 3072 MiB | Same, on the reference custom guest kernel |
| grow | 256 MiB initial, automatically increased to 512 MiB for startup | Same, with managed telemetry and runtime hot-add |

A file created on `/workspace` persisted across profile changes. SSH used new per-installation keys and a pinned generated guest host key. The Docker daemon reported `26.1.5+dfsg1`, storage driver `overlay2`. Each successful stop checked both ext4 disks offline and returned the owned reserve/module resources.

The default shared 4096 MiB request was correctly refused when Android MemAvailable was below its 6868 MiB requirement. A smaller **explicit** reservation was used; no guard was weakened and no application was killed to force the request. The underlying reference grow-only path has been exercised to a 4096 MiB assignment; that is the implementation ceiling, not a hardware maximum or a promise that every phone state can spare it.

The local reference custom Image identifies as `6.18.16-docker-grow1`. It and its matching guest module were used as audited local build artifacts for bring-up. The supplied rebuild configuration deliberately uses the public local version `-android-linux-vm`; build its Image and module together. Host `.ko` files used the audited Android ABI. No such binaries are committed or offered as universally installable artifacts.

This record is specific to the reference device and trusted guest configuration. It is not a cross-device compatibility certification, a sandbox security claim or a performance ranking.
