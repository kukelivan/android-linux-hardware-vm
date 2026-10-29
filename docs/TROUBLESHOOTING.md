# Diagnosis

Read `supervisor.log`, `events.jsonl`, `console.log` and `memory.jsonl` in DROIDVM_STATE. Preserve logs after a failure before another launch. An Android `/dev/kmsg` collector and an external serial/log receiver are useful additions when diagnosing host resets; local writes may not survive a reset.

| Symptom | Meaning / next action |
|---|---|
| doctor: wrong release/BTF | Wrong host ABI; rebuild/audit a device port. Never disable the gate. |
| no `/dev/gunyah` | Required firmware/driver interface is absent or inaccessible. Root or this module cannot manufacture it. |
| namespace/permission error | Confirm real root, capabilities and actual chroot. PRoot is insufficient. |
| crosvm rejects `gunyah` | This crosvm build does not implement the required backend. |
| hugepage reserve is short | MemAvailable does not imply suitable backing. Lower the fixed profile or use smaller growth; preserve counters. |
| pre-start guard refuses pages/SG count | No RM lend should be attempted. Do not increase the 512 bound to force a start. |
| MEM_ACCEPT succeeds, hot-add returns EEXIST | Inspect guest `/proc/iomem`: PCI/reserved ranges may overlap. Keep pins until shutdown. |
| guest module times out on RPC | Confirm generated RM DT node, capability IDs and exclusive ownership of the receive queue. |
| growth agent missing | Check `systemctl status grow-agent` in the guest. It must be a managed service after switch_root. Docker startup is gated on telemetry. |
| helper load resets host | Inspect initializer disassembly, CFI type hashes, no-fault reads and the exact BTF. A VM need not exist for a module bug to panic Android. |
| D-state crosvm / RM hang | Refuse new starts and force unloads. Capture state; recovery may require an operator-controlled device reboot. |
| Docker cannot reach the internet | Check guest route/DNS, slirp and Android networking/VPN. SSH forwarding is separate from guest egress. |

For Android reset diagnosis, distinguish the original unclean reset from subsequent user-requested restarts. Bootreason often describes the reset mechanism rather than the initiating software component. A `hard` or PS_HOLD label alone does not prove a hypervisor panic.

Read `/sys/module/ramoops/parameters`: zero record_size/console_size means kernel panic/console records are not retained there. Enabling printk after boot does not retroactively create persistent panic storage. Do not change boot-reserved memory without a device-specific recovery plan.
