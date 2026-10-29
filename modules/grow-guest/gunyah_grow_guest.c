// SPDX-License-Identifier: GPL-2.0
/* Grow-only guest RAM: accept a host-SHARE'd parcel, then hot-add it.
 * Uses its own RM transport on the separately built kernel (no native Gunyah driver). */
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/file.h>
#include <linux/miscdevice.h>
#include <linux/capability.h>
#include <linux/of.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/memory_hotplug.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/mutex.h>
#include <linux/unaligned.h>
#include <linux/arm-smccc.h>
#include <linux/delay.h>
/* RM framing and raw-HVC accept derived from Droid-VM/droidvm-guest-additions
 * gunyah_guest/gunyah_guest.c, GPL-2.0-or-later. This kernel has no other RM
 * receive-queue consumer. */
#define GH_HCALL(fn)							\
	ARM_SMCCC_CALL_VAL(ARM_SMCCC_FAST_CALL, ARM_SMCCC_SMC_64,	\
			   ARM_SMCCC_OWNER_VENDOR_HYP, (fn))
#define GH_HCALL_MSGQ_SEND	GH_HCALL(0x801B)
#define GH_HCALL_MSGQ_RECV	GH_HCALL(0x801C)
#define GH_MSGQ_TX_PUSH		BIT(0)
#define GH_ERROR_OK		0

/* RM RPC framing. */
#define GH_RM_RPC_API		0x21	/* version 1 | hdr_words 2 (<<4) */
#define GH_RM_RPC_TYPE_REQUEST	0x01
#define GH_RM_RPC_TYPE_REPLY	0x02
#define GH_RM_RPC_TYPE_MASK	0x03

#define GH_RM_RPC_MEM_ACCEPT	0x51000011
#define GH_RM_RPC_MEM_RELEASE	0x51000014

#define GH_RM_MSGQ_MSG_SIZE	240

/* MEM_ACCEPT constants (production form proven in bring-up). */
#define GH_RM_MEM_TYPE_NORMAL	0
#define GH_RM_TRANS_TYPE_SHARE	2
#define GH_RM_MEM_ACCEPT_MAP_IPA_CONTIGUOUS	BIT(4)
#define GH_RM_MEM_ACCEPT_DONE			BIT(7)

struct gh_rpc_reply {
	u8 api;
	u8 type;
	__le16 seq;
	__le32 msg_id;
	__le32 err_code;
} __packed;

static u64 gg_tx_capid, gg_rx_capid;
static bool gg_ready;
static u16 gg_seq = 1;
static DEFINE_MUTEX(gg_lock);
static u8 gg_txbuf[64] __aligned(8);
static u8 gg_rxbuf[GH_RM_MSGQ_MSG_SIZE] __aligned(8);

static int gg_msgq_send(u64 capid, void *buff, size_t size)
{
	struct arm_smccc_res res;

	arm_smccc_1_1_hvc(GH_HCALL_MSGQ_SEND, capid, size, (uintptr_t)buff,
			  GH_MSGQ_TX_PUSH, 0, &res);
	return (int)res.a0;
}

static int gg_msgq_recv(u64 capid, void *buff, size_t size, size_t *recv_size)
{
	struct arm_smccc_res res;

	arm_smccc_1_1_hvc(GH_HCALL_MSGQ_RECV, capid, (uintptr_t)buff, size,
			  0, &res);
	if (res.a0 == GH_ERROR_OK)
		*recv_size = res.a1;
	return (int)res.a0;
}

/* Send @txlen bytes, poll for the REPLY matching @seq, return RM err_code. */
static int gg_rpc(size_t txlen, u16 seq, u32 *err_code)
{
	struct gh_rpc_reply *r = (void *)gg_rxbuf;
	size_t rxlen = 0;
	int err, i;

	err = gg_msgq_send(gg_tx_capid, gg_txbuf, txlen);
	if (err) {
		pr_err("msgq_send failed: gunyah_error=%d\n", err);
		return -EIO;
	}

	for (i = 0; i < 2000; i++) {
		rxlen = 0;
		err = gg_msgq_recv(gg_rx_capid, gg_rxbuf, sizeof(gg_rxbuf),
				   &rxlen);
		if (err == GH_ERROR_OK && rxlen >= sizeof(*r) &&
		    (r->type & GH_RM_RPC_TYPE_MASK) == GH_RM_RPC_TYPE_REPLY &&
		    le16_to_cpu(r->seq) == seq) {
			*err_code = le32_to_cpu(r->err_code);
			return 0;
		}
		udelay(100);
	}
	pr_err("RPC timeout (last gunyah_error=%d)\n", err);
	return -ETIMEDOUT;
}

bool gunyah_guest_available(void)
{
	return READ_ONCE(gg_ready);
}
EXPORT_SYMBOL_GPL(gunyah_guest_available);

int gunyah_guest_mem_accept(u32 handle, u64 gpa, u64 size)
{
	u8 *p = gg_txbuf;
	size_t off = 0;
	u32 err_code = 0;
	u16 seq;
	int ret;

	if (!READ_ONCE(gg_ready))
		return -ENODEV;

	mutex_lock(&gg_lock);
	seq = ++gg_seq;

	/* RPC header (8) */
	p[off++] = GH_RM_RPC_API;
	p[off++] = GH_RM_RPC_TYPE_REQUEST;
	put_unaligned_le16(seq, p + off); off += 2;
	put_unaligned_le32(GH_RM_RPC_MEM_ACCEPT, p + off); off += 4;
	/* accept hdr (12) */
	put_unaligned_le32(handle, p + off); off += 4;
	p[off++] = GH_RM_MEM_TYPE_NORMAL;
	p[off++] = GH_RM_TRANS_TYPE_SHARE;
	/*
	 * MAP_IPA_CONTIGUOUS + a SINGLE sgl {gpa, size} is the correct form for a
	 * scatter-gather blob. A >2MB blob is backed by several independent, non-
	 * adjacent 2MB folios, so the memparcel carries N mem_entries (one per
	 * physically-contiguous run). The RM's accept has exactly two shapes
	 * (rsc-mgr memparcel_do_accept): num_mappings = contiguous ? 1 : N. With the
	 * flag OFF the guest must supply N sgl entries, each sized to a region -- a
	 * layout the guest cannot know. With the flag ON, num_mappings=1: the guest
	 * gives ONE sgl {gpa, total_size} (total_size must equal mp->total_size, i.e.
	 * the exact page-aligned byte count the host share_blob()'d = obj->size) and
	 * the RM allocates one contiguous IPA range and lays the N scattered
	 * mem_entries into it sequentially -- IPA contiguous, PA scattered. A single
	 * sgl WITHOUT the flag gave err_code=0x6 (ARGUMENT_INVALID: 1 != N mappings).
	 */
	p[off++] = GH_RM_MEM_ACCEPT_MAP_IPA_CONTIGUOUS | GH_RM_MEM_ACCEPT_DONE;
	p[off++] = 0;						/* reserved1 */
	put_unaligned_le32(0, p + off); off += 4;		/* validate_label */
	/* acl_desc: n=0 (SHARE accepted with no ACL; RM uses its stored copy) */
	put_unaligned_le32(0, p + off); off += 4;
	/* sgl_desc_intf: n(u16)=1, reserved(u16=map_vmid 0), one {ipa,size} */
	put_unaligned_le16(1, p + off); off += 2;
	put_unaligned_le16(0, p + off); off += 2;
	put_unaligned_le64(gpa, p + off); off += 8;
	put_unaligned_le64(size, p + off); off += 8;
	/* mem_attr_desc: n=0 */
	put_unaligned_le16(0, p + off); off += 2;
	put_unaligned_le16(0, p + off); off += 2;

	ret = gg_rpc(off, seq, &err_code);
	mutex_unlock(&gg_lock);

	if (ret)
		return ret;
	if (err_code) {
		pr_err("MEM_ACCEPT handle=0x%x gpa=0x%llx rejected, err_code=0x%x\n",
		       handle, gpa, err_code);
		return -EACCES;
	}
	pr_debug("MEM_ACCEPT handle=0x%x mapped at gpa=0x%llx size=0x%llx\n",
		 handle, gpa, size);
	return 0;
}
EXPORT_SYMBOL_GPL(gunyah_guest_mem_accept);

struct request {u32 handle,flags;u64 gpa,size;};
#define ACCEPT _IOWR('L',4,struct request)
static DEFINE_MUTEX(lock);
static u64 next_gpa=0x90000000ULL;
static int init_transport(void){
 struct device_node*np=of_find_compatible_node(NULL,NULL,"gunyah-resource-manager");int ret;
 if(!np)return -ENODEV;
 ret=of_property_read_u64_index(np,"reg",0,&gg_tx_capid);
 if(!ret)ret=of_property_read_u64_index(np,"reg",1,&gg_rx_capid);
 of_node_put(np);if(ret)return ret;
 WRITE_ONCE(gg_ready,true);pr_info("gunyah_grow_guest: RM transport ready tx=%llx rx=%llx\n",gg_tx_capid,gg_rx_capid);return 0;
}
static long control(struct file*f,unsigned cmd,unsigned long arg){
 struct request r;int ret;
 if(!capable(CAP_SYS_ADMIN))return -EPERM;if(cmd!=ACCEPT)return -ENOTTY;
 if(copy_from_user(&r,(void __user*)arg,sizeof(r)))return -EFAULT;
 if(r.flags||!r.handle||r.size!=128ULL*1024*1024||r.gpa<0x90000000ULL||r.gpa>=0x180000000ULL||(r.gpa&((128ULL<<20)-1)))return -EINVAL;
 mutex_lock(&lock);if(r.gpa!=next_gpa){ret=-EINVAL;goto out;}
 ret=gunyah_guest_mem_accept(r.handle,r.gpa,r.size);
 if(ret){pr_err("gunyah_grow_guest: MEM_ACCEPT failed %d\n",ret);goto out;}
 ret=add_memory_driver_managed(0,r.gpa,r.size,"System RAM (gunyah-grow)",0);
 if(ret){pr_err("gunyah_grow_guest: accepted parcel, hot-add failed %d; keep until shutdown\n",ret);goto out;}
 next_gpa+=r.size;pr_info("gunyah_grow_guest: hot-added 128 MiB at %llx\n",r.gpa);
out:mutex_unlock(&lock);return ret;
}
static const struct file_operations ops={.owner=THIS_MODULE,.unlocked_ioctl=control,.llseek=noop_llseek};
static struct miscdevice device={.minor=MISC_DYNAMIC_MINOR,.name="gunyah_grow_guest",.fops=&ops,.mode=0600};
static int __init start(void){
 int ret=init_transport();u32 error=0;u16 seq;if(ret)return ret;
 mutex_lock(&gg_lock);seq=++gg_seq;gg_txbuf[0]=GH_RM_RPC_API;gg_txbuf[1]=GH_RM_RPC_TYPE_REQUEST;
 put_unaligned_le16(seq,gg_txbuf+2);put_unaligned_le32(0x56000024,gg_txbuf+4);
 ret=gg_rpc(8,seq,&error);mutex_unlock(&gg_lock);
 pr_info("gunyah_grow_guest: RM self-ID probe ret=%d error=%x\n",ret,error);
 if(ret||error)return ret?ret:-EIO;
 pr_info("gunyah_grow_guest: ready for grow-only RAM\n");return misc_register(&device);}
static void __exit stop(void){misc_deregister(&device);}module_init(start);module_exit(stop);
MODULE_LICENSE("GPL");MODULE_DESCRIPTION("Grow-only Gunyah guest RAM accept and hot-add prototype");
