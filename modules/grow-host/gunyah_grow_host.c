// SPDX-License-Identifier: GPL-2.0
/* Prototype only: runtime SHARE of an already registered, fresh zero RAM slot.
 * The VMM must keep the slot pinned until normal guest shutdown; no reclaim.
 * Needs the phone's validated Android 6.1 DDK and exact live-BTF validation. */
#include <linux/module.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>
#include <linux/file.h>
#include <linux/mm.h>
#include <linux/capability.h>
#include <linux/uaccess.h>
#include <linux/kprobes.h>
#include <linux/gunyah_rsc_mgr.h>
#include "vm_mgr.h"
static typeof(&gh_rm_mem_share) runtime_share;
static noinline __used int share_signature(struct gh_rm*rm,struct gh_rm_mem_parcel*p){return 0;}
struct request {u32 vmfd,label,handle,flags;};
#define SHARE _IOWR('L',3,struct request)
static long control(struct file*file,unsigned cmd,unsigned long arg){
 struct request r;struct file*f;struct gh_vm*vm;struct gh_vm_mem*m;int ret=-ENOENT;char symbol[64];
 if(!capable(CAP_SYS_ADMIN))return -EPERM;if(cmd!=SHARE)return -ENOTTY;
 if(copy_from_user(&r,(void __user*)arg,sizeof(r)))return -EFAULT;
 if(r.flags||r.label<0x47560001||r.label>0x4756001e)return -EINVAL;
 f=fget(r.vmfd);if(!f)return -EBADF;
 scnprintf(symbol,sizeof(symbol),"%ps",f->f_op->unlocked_ioctl);
 if(strcmp(symbol,"gh_vm_ioctl")){ret=-ENOTTY;goto put;}
 vm=f->private_data;if(!vm||vm->mm!=current->mm){ret=-EPERM;goto put;}
 down_read(&vm->status_lock);mutex_lock(&vm->mm_lock);
 if(vm->vm_status!=GH_RM_VM_STATUS_RUNNING){ret=-EBUSY;goto unlock;}
 list_for_each_entry(m,&vm->memory_mappings,list){
  if(m->parcel.label!=r.label)continue;
  if(m->share_type!=VM_MEM_SHARE||m->npages!=32768||m->guest_phys_addr<0x90000000ULL||m->guest_phys_addr>=0x180000000ULL||(m->guest_phys_addr&((128ULL<<20)-1))||m->parcel.n_acl_entries!=2||!m->parcel.n_mem_entries||m->parcel.n_mem_entries>512){ret=-EINVAL;goto unlock;}
  if(m->parcel.mem_handle!=GH_MEM_HANDLE_INVAL){ret=-EALREADY;goto unlock;}
  m->parcel.acl_entries[0].vmid=cpu_to_le16(vm->vmid);
  ret=runtime_share(vm->rm,&m->parcel);
  if(!ret){r.handle=m->parcel.mem_handle;pr_info("gunyah_grow_host: runtime shared label %x handle %x GPA %llx\n",r.label,r.handle,m->guest_phys_addr);}
  break;
 }
unlock:mutex_unlock(&vm->mm_lock);up_read(&vm->status_lock);
put:fput(f);if(ret)return ret;return copy_to_user((void __user*)arg,&r,sizeof(r))?-EFAULT:0;
}
static const struct file_operations ops={.owner=THIS_MODULE,.unlocked_ioctl=control,.llseek=no_llseek};
static struct miscdevice dev={.minor=MISC_DYNAMIC_MINOR,.name="gunyah_grow_host",.fops=&ops,.mode=0600};
static int __init start(void){
 struct kprobe kp={.symbol_name="gh_rm_mem_share"};int ret;u32 expected,actual;
 BUILD_BUG_ON(sizeof(struct gh_vm)!=544);BUILD_BUG_ON(offsetof(struct gh_vm,rm)!=8);
 BUILD_BUG_ON(offsetof(struct gh_vm,vm_status)!=88);BUILD_BUG_ON(offsetof(struct gh_vm,mm)!=256);
 BUILD_BUG_ON(offsetof(struct gh_vm,memory_mappings)!=312);BUILD_BUG_ON(sizeof(struct gh_rm_mem_parcel)!=112);
 ret=register_kprobe(&kp);if(ret)return ret;runtime_share=(void*)kp.addr;unregister_kprobe(&kp);
 ret=copy_from_kernel_nofault(&expected,(const char*)share_signature-4,4);if(ret)return ret;
 ret=copy_from_kernel_nofault(&actual,(const char*)runtime_share-4,4);if(ret)return ret;
 if(expected!=actual){pr_err("gunyah_grow_host: CFI signature mismatch %x %x\n",expected,actual);return -EINVAL;}
 pr_info("gunyah_grow_host: CFI signature verified %x\n",actual);
 return misc_register(&dev);
}
static void __exit stop(void){misc_deregister(&dev);}module_init(start);module_exit(stop);
MODULE_LICENSE("GPL");MODULE_DESCRIPTION("Grow-only runtime RAM SHARE prototype; not production validated");
