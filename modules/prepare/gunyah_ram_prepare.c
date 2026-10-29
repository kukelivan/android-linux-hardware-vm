// SPDX-License-Identifier: GPL-2.0
/* Root-only, before-start layout of fresh zero anonymous private RAM.
 * No allocator/free hooks; all installed arrays use the driver's normal kfree.
 * Exact Gunyah structure offsets are pinned to this kernel's live BTF. */
#include <linux/module.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>
#include <linux/file.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/sort.h>
#include <linux/sched.h>
#include <linux/capability.h>
#include <linux/uaccess.h>
#include <linux/mutex.h>
#include <linux/rwsem.h>
#define LIMIT 512
static unsigned long last_runs,last_pages;
module_param(last_runs,ulong,0400);module_param(last_pages,ulong,0400);
#define MAX_BINS 62
struct request {u64 address,bytes;u32 vmfd,flags;u64 descriptors;u32 slots,max_segments;};
#define PREPARE _IOWR('L',1,struct request)
#define FINALIZE _IOWR('L',2,struct request)
struct context {struct page**pages;unsigned long n;int tgid;bool finalized;};
struct segment {__le64 phys,size;};
struct bin {u8*m;u64 gpa;unsigned long capacity,filled,offset;unsigned segments;struct segment*table;};
struct run {unsigned long start,n;bool aligned;};
static int page_cmp(const void*a,const void*b){unsigned long x=page_to_pfn(*(struct page*const*)a),y=page_to_pfn(*(struct page*const*)b);return (x>y)-(x<y);}
static int bin_cmp(const void*a,const void*b){u64 x=((const struct bin*)a)->gpa,y=((const struct bin*)b)->gpa;return (x>y)-(x<y);}
static int run_desc(const void*a,const void*b){unsigned long x=((const struct run*)a)->n,y=((const struct run*)b)->n;return (x<y)-(x>y);}
static int run_cmp(const void*a,const void*b){const struct run*x=a,*y=b;if(x->aligned!=y->aligned)return x->aligned?-1:1;return (x->n<y->n)-(x->n>y->n);}
static int device_open(struct inode*inode,struct file*file){struct context*c;if(!capable(CAP_SYS_ADMIN))return -EPERM;c=kzalloc(sizeof(*c),GFP_KERNEL);if(!c)return -ENOMEM;c->tgid=current->tgid;file->private_data=c;return 0;}
static int device_release(struct inode*inode,struct file*file){struct context*c=file->private_data;if(c->pages){unpin_user_pages(c->pages,c->n);kvfree(c->pages);}kfree(c);return 0;}
static int do_prepare(struct context*c,struct request*r){long n;unsigned long count=r->bytes/PAGE_SIZE;if(c->pages||r->flags||!r->bytes||r->bytes>4ULL*1024*1024*1024||(r->bytes&(PAGE_SIZE-1))||(r->address&(PAGE_SIZE-1)))return -EINVAL;c->pages=kvcalloc(count,sizeof(*c->pages),GFP_KERNEL);if(!c->pages)return -ENOMEM;n=pin_user_pages_fast(r->address,count,FOLL_WRITE|FOLL_LONGTERM,c->pages);if(n!=count){if(n>0)unpin_user_pages(c->pages,n);kvfree(c->pages);c->pages=NULL;return n<0?n:-EFAULT;}c->n=count;return 0;}
static int assign(struct context*c,struct bin*b,unsigned count,struct run*runs,unsigned long nr,struct page**dest,unsigned long slice){
 unsigned i;unsigned long j,k;
 for(i=0;i<count;i++){b[i].filled=0;b[i].segments=0;memset(b[i].table,0,LIMIT*sizeof(*b[i].table));}
 for(j=0;j<nr;j++){
  unsigned long offset=0;
  while(offset<runs[j].n){int best=-1;unsigned long take;u64 phys;
   for(i=0;i<count;i++)if(b[i].filled<b[i].capacity&&(best<0||b[i].segments<b[best].segments||(b[i].segments==b[best].segments&&b[i].capacity-b[i].filled>b[best].capacity-b[best].filled)))best=i;
   if(best<0)return -EOVERFLOW;i=best;take=min(slice,min(runs[j].n-offset,b[i].capacity-b[i].filled));phys=page_to_phys(c->pages[runs[j].start+offset]);
   if(b[i].segments&&le64_to_cpu(b[i].table[b[i].segments-1].phys)+le64_to_cpu(b[i].table[b[i].segments-1].size)==phys)b[i].table[b[i].segments-1].size=cpu_to_le64(le64_to_cpu(b[i].table[b[i].segments-1].size)+take*PAGE_SIZE);
   else{if(b[i].segments>=LIMIT)return -E2BIG;b[i].table[b[i].segments].phys=cpu_to_le64(phys);b[i].table[b[i].segments++].size=cpu_to_le64(take*PAGE_SIZE);}
   for(k=0;k<take;k++)dest[b[i].offset+b[i].filled+k]=c->pages[runs[j].start+offset+k];
   b[i].filled+=take;offset+=take;cond_resched();
  }
 }
 for(i=0;i<count;i++)if(b[i].filled!=b[i].capacity)return -EINVAL;return 0;
}
static int do_finalize(struct context*c,struct request*r){
 struct file*f;void*vm;struct rw_semaphore*status;struct mutex*mm;struct list_head*head,*node;struct bin*b=NULL;struct page**check=NULL,**dest=NULL,**selected=NULL,**chosen_sorted=NULL;struct run*runs=NULL;unsigned count=0,i,attempt;unsigned long n=0,j,nr=0,ci=0,selected_n=0,candidate_n=0;struct context view;int ret=-EINVAL;char symbol[64];static const unsigned long slices[]={1048576,4096,2048,1024,512,256,128};
 if(!c->pages||c->finalized||(r->flags&~1U))return -EINVAL;
 f=fget(r->vmfd);if(!f)return -EBADF;
 scnprintf(symbol,sizeof(symbol),"%ps",f->f_op->unlocked_ioctl);if(strcmp(symbol,"gh_vm_ioctl")){ret=-ENOTTY;goto put;}
 vm=f->private_data;if(!vm||*(struct mm_struct**)((u8*)vm+256)!=current->mm){ret=-EPERM;goto put;}
 status=(void*)((u8*)vm+120);mm=(void*)((u8*)vm+264);head=(void*)((u8*)vm+312);
 down_read(status);mutex_lock(mm);if(*(int*)((u8*)vm+88)!=0){ret=-EBUSY;goto unlock;}
 b=kcalloc(MAX_BINS,sizeof(*b),GFP_KERNEL);if(!b){ret=-ENOMEM;goto unlock;}
 list_for_each(node,head){u8*m=(void*)node,*p=m+24;u32 label=*(u32*)(p+4);unsigned long pages=*(unsigned long*)(m+152);
  if(!label||label>MAX_BINS||*(u64*)(m+136)<0x88000000ULL||*(u64*)(m+136)>=0x88000000ULL+r->bytes||*(int*)(m+16)!=(r->flags?0:1))continue;
  if(count>=MAX_BINS||(pages!=16384&&pages!=32768&&pages!=65536)){ret=-EINVAL;goto cleanup;}
  b[count].m=m;b[count].gpa=*(u64*)(m+136);b[count].capacity=pages;count++;n+=pages;
 }
 if(!count||n>c->n||c->n-n>49152){ret=-EINVAL;goto cleanup;}sort(b,count,sizeof(*b),bin_cmp,NULL);
 j=0;for(i=0;i<count;i++){if(b[i].gpa!=0x88000000ULL+j*PAGE_SIZE){ret=-EINVAL;goto cleanup;}b[i].offset=j;j+=b[i].capacity;}
 check=kvmalloc_array(n,sizeof(*check),GFP_KERNEL);dest=kvmalloc_array(n,sizeof(*dest),GFP_KERNEL);selected=kvmalloc_array(n,sizeof(*selected),GFP_KERNEL);chosen_sorted=kvmalloc_array(n,sizeof(*chosen_sorted),GFP_KERNEL);if(!check||!dest||!selected||!chosen_sorted){ret=-ENOMEM;goto cleanup;}
 j=0;for(i=0;i<count;i++){memcpy(check+j,*(struct page***)(b[i].m+144),b[i].capacity*sizeof(*check));j+=b[i].capacity;}
 candidate_n=c->n;sort(c->pages,candidate_n,sizeof(*c->pages),page_cmp,NULL);sort(check,n,sizeof(*check),page_cmp,NULL);
 for(j=0;j<n;j++){while(ci<candidate_n&&page_to_pfn(c->pages[ci])<page_to_pfn(check[j]))ci++;if(ci>=candidate_n||check[j]!=c->pages[ci]||(j&&check[j]==check[j-1])){ret=-EINVAL;goto cleanup;}if(!(j&4095))cond_resched();}
 for(j=0;j<candidate_n;j++)if(!j||page_to_pfn(c->pages[j])!=page_to_pfn(c->pages[j-1])+1)nr++;
 runs=kvcalloc(nr,sizeof(*runs),GFP_KERNEL);if(!runs){ret=-ENOMEM;goto cleanup;}
 nr=0;for(j=0;j<candidate_n;j++){if(!j||page_to_pfn(c->pages[j])!=page_to_pfn(c->pages[j-1])+1){runs[nr].start=j;runs[nr++].n=1;}else runs[nr-1].n++;}
 sort(runs,nr,sizeof(*runs),run_desc,NULL);
 for(j=0;j<nr&&selected_n<n;j++){unsigned long start=runs[j].start,take=min(runs[j].n,n-selected_n);memcpy(selected+selected_n,c->pages+start,take*sizeof(*selected));runs[j].start=selected_n;runs[j].n=take;selected_n+=take;}
 nr=j;if(selected_n!=n){ret=-EINVAL;goto cleanup;}
 last_runs=nr;last_pages=n;pr_info("gunyah_ram_prepare: selected %lu pages from %lu candidates, %lu physical runs, %u slots\n",n,candidate_n,nr,count);
 {
 struct run*aligned=kvcalloc(nr*3,sizeof(*aligned),GFP_KERNEL);unsigned long ar=0,k,huge=0;
 if(!aligned){ret=-ENOMEM;goto cleanup;}
 for(k=0;k<nr;k++){
  unsigned long start=runs[k].start,len=runs[k].n,pfn=page_to_pfn(selected[start]);
  unsigned long head=min(len,(-pfn)&511UL),middle=(len-head)&~511UL,tail=len-head-middle;
  if(head)aligned[ar++]=(struct run){.start=start,.n=head};
  if(middle){aligned[ar++]=(struct run){.start=start+head,.n=middle,.aligned=true};huge+=middle;}
  if(tail)aligned[ar++]=(struct run){.start=start+head+middle,.n=tail};
 }
 kvfree(runs);runs=aligned;nr=ar;
 pr_info("gunyah_ram_prepare: aligned 2MiB backing %lu MiB\n",huge>>8);
 }
 sort(runs,nr,sizeof(*runs),run_cmp,NULL);view=*c;view.pages=selected;view.n=n;
 memcpy(chosen_sorted,selected,n*sizeof(*selected));sort(chosen_sorted,n,sizeof(*chosen_sorted),page_cmp,NULL);
 for(i=0;i<count;i++){b[i].table=kcalloc(LIMIT,sizeof(*b[i].table),GFP_KERNEL);if(!b[i].table){ret=-ENOMEM;goto cleanup;}}
 for(attempt=0;attempt<ARRAY_SIZE(slices);attempt++){ret=assign(&view,b,count,runs,nr,dest,slices[attempt]);if(!ret)break;}
 if(ret)goto cleanup;
 /* Every check/allocation completed. Install the same page set, repartitioned.
  * Replacing pages as well as descriptors preserves per-parcel reclaim safety. */
 r->descriptors=0;r->max_segments=0;r->slots=count;r->bytes=n*PAGE_SIZE;
 for(i=0;i<count;i++){u8*p=b[i].m+24;void*old=*(void**)(p+32);
  memcpy(*(struct page***)(b[i].m+144),dest+b[i].offset,b[i].capacity*sizeof(*dest));
  *(struct segment**)(p+32)=b[i].table;*(size_t*)(p+24)=b[i].segments;b[i].table=NULL;kfree(old);
  r->descriptors+=b[i].segments;r->max_segments=max(r->max_segments,b[i].segments);
 }
 /* Transfer selected helper pin references to the installed VM arrays.
  * Drop every original VM pin; retain only discarded helper pins for release. */
 unpin_user_pages(check,n);ci=0;selected_n=0;
 for(j=0;j<candidate_n;j++){struct page*page=c->pages[j];if(selected_n<n&&chosen_sorted[selected_n]==page)selected_n++;else c->pages[ci++]=page;}
 c->n=ci;c->finalized=true;ret=0;pr_info("gunyah_ram_prepare: %u parcels, %llu bytes, %llu descriptors, maximum %u\n",r->slots,r->bytes,r->descriptors,r->max_segments);
cleanup:
 if(b){for(i=0;i<count;i++)kfree(b[i].table);kfree(b);}kvfree(check);kvfree(dest);kvfree(runs);kvfree(selected);kvfree(chosen_sorted);
unlock:mutex_unlock(mm);up_read(status);
put:fput(f);return ret;
}
static long device_ioctl(struct file*f,unsigned cmd,unsigned long arg){struct context*c=f->private_data;struct request r;int ret;if(!capable(CAP_SYS_ADMIN)||current->tgid!=c->tgid)return -EPERM;if(copy_from_user(&r,(void __user*)arg,sizeof(r)))return -EFAULT;if(cmd==PREPARE)ret=do_prepare(c,&r);else if(cmd==FINALIZE)ret=do_finalize(c,&r);else return -ENOTTY;if(ret)return ret;if(copy_to_user((void __user*)arg,&r,sizeof(r)))return -EFAULT;return 0;}
static const struct file_operations ops={.owner=THIS_MODULE,.open=device_open,.release=device_release,.unlocked_ioctl=device_ioctl,.llseek=no_llseek};
static struct miscdevice dev={.minor=MISC_DYNAMIC_MINOR,.name="gunyah_ram_prepare",.fops=&ops,.mode=0600};
static int __init start(void){return misc_register(&dev);}static void __exit stop(void){misc_deregister(&dev);}module_init(start);module_exit(stop);
MODULE_LICENSE("GPL");MODULE_VERSION("0.1");MODULE_DESCRIPTION("Before-start layout of fresh zero private RAM across bounded Gunyah parcels");
