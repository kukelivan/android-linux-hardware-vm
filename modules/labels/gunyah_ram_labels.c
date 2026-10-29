// SPDX-License-Identifier: GPL-2.0
#include <linux/module.h>
#include <linux/kprobes.h>
#include <linux/atomic.h>
#include <linux/sched.h>
#include <asm/unaligned.h>
/* Translate only explicitly tagged exclusive RWX normal-RAM lend requests.
 * Linux mapping labels, allocations, reclamation and other VMs stay untouched.
 * The userspace pre-start fragmentation guard is mandatory for our launcher. */
static bool enabled;
module_param(enabled, bool, 0600);
static int target_tgid;
module_param(target_tgid, int, 0600);
static atomic64_t translated = ATOMIC64_INIT(0);
static int relabel(struct kprobe *kp, struct pt_regs *regs)
{
 u8 *req = (void *)regs->regs[2];
 size_t size = regs->regs[3];
 u32 label;
 if (!READ_ONCE(enabled) || !READ_ONCE(target_tgid) ||
     current->tgid != READ_ONCE(target_tgid) || (u32)regs->regs[1] != 0x51000012 ||
     !req || size < 40 || size > 16384)
  return 0;
 label = get_unaligned_le32(req + 4);
 if ((label & 0xffff0000U) != 0x47530000U ||
     !(label & 0xffffU) || (label & 0xffffU) > 31)
  return 0;
 if (req[0] || req[1] || (req[2] & ~2U) || req[3] ||
     get_unaligned_le32(req + 8) != 1 || req[14] != 7 || req[15] ||
     !get_unaligned_le16(req + 16) ||
     get_unaligned_le16(req + 16) > 512 ||
     size != 24 + 16 * get_unaligned_le16(req + 16) ||
     get_unaligned_le32(req + size - 4))
  return 0;
 put_unaligned_le32(0, req + 4);
 atomic64_inc(&translated);
 pr_info_ratelimited("gunyah_ram_labels: translated RAM parcel label %#x\n", label);
 return 0;
}
static struct kprobe probe = { .symbol_name = "gh_rm_call", .pre_handler = relabel };
static int __init ram_labels_init(void)
{
 int ret = register_kprobe(&probe);
 if (!ret) pr_info("gunyah_ram_labels: registered; enabled=%d\n", enabled);
 return ret;
}
static void __exit ram_labels_exit(void)
{
 unregister_kprobe(&probe);
 pr_info("gunyah_ram_labels: removed; translated=%lld\n", atomic64_read(&translated));
}
module_init(ram_labels_init);
module_exit(ram_labels_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Scoped legacy Gunyah split RAM parcel label adapter");
MODULE_VERSION("0.1");
