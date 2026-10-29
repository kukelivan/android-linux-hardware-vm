// SPDX-License-Identifier: GPL-2.0-only
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <stdarg.h>
#include <sys/types.h>
#include <sys/mman.h>
extern int *__errno(void) __attribute__((weak));
extern int *__errno_location(void) __attribute__((weak));
static void error_set(int value){if(__errno)*__errno()=value;else if(__errno_location)*__errno_location()=value;}
static void message(const char*s){write(2,s,strlen(s));syscall(82,2);}
#define MAX_SEGMENTS 512UL
#define GH_VM_START 0x4703UL
#define SET_MEMORY 0x40204701UL
#define LEND_MEMORY 0x40204111UL
#define PFN_MASK ((1ULL<<55)-1)
struct region {uint32_t label,flags;uint64_t guest,size,user;};
struct entry {int fd;int lend;struct region r;};
static struct entry entries[4096];static unsigned used;static int overflow;
/* Track the actual successful kernel memory registrations, not VMA guesses.
 * Every parcel must fit in the initial bounded RM message without an append. */
static int inspect_range(int pm,uint64_t first,uint64_t size,unsigned long *segments){
 if(!size||(first&4095)||(size&4095)||size>(2ULL<<30)||first+size<first)return -1;
 uint64_t prev=0;int have=0;*segments=0;
 for(uint64_t off=0;off<size;){
  uint64_t values[512];size_t n=(size-off)/4096;if(n>512)n=512;
  if(pread(pm,values,n*8,(off_t)((first+off)/4096*8))!=(ssize_t)(n*8))return -1;
  for(size_t i=0;i<n;i++){
   uint64_t p=values[i]&PFN_MASK;
   if(!(values[i]&(1ULL<<63))||!p)return -1;
   if(!have||p!=prev+1)(*segments)++;
   prev=p;have=1;
  }
  off+=n*4096;
 }
 return 0;
}
static int pfn_compare(const void*a,const void*b){uint64_t x=*(const uint64_t*)a,y=*(const uint64_t*)b;return (x>y)-(x<y);}
static int inspect_sorted(int pm,uint64_t address,uint64_t bytes,unsigned long*segments){
 size_t n=bytes/4096;if(!n||bytes%4096||n>16384)return -1;uint64_t*pfns=malloc(n*8);if(!pfns)return -1;
 if(pread(pm,pfns,n*8,(off_t)(address/4096*8))!=(ssize_t)(n*8)){free(pfns);return -1;}
 for(size_t i=0;i<n;i++){if(!(pfns[i]&(1ULL<<63))||!(pfns[i]&PFN_MASK)){free(pfns);return -1;}pfns[i]&=PFN_MASK;}
 qsort(pfns,n,8,pfn_compare);*segments=1;for(size_t i=1;i<n;i++){if(pfns[i]==pfns[i-1]){free(pfns);return -1;}if(pfns[i]!=pfns[i-1]+1)(*segments)++;}free(pfns);return 0;
}
static unsigned long read_number(const char*path){char b[64];int f=open(path,O_RDONLY);if(f<0)return ~0UL;int n=read(f,b,63);close(f);if(n<=0)return ~0UL;unsigned long v=0;for(int i=0;i<n&&b[i]>='0'&&b[i]<='9';i++)v=v*10+b[i]-'0';return v;}
struct prepare_request {uint64_t address,bytes;uint32_t vmfd,flags;uint64_t descriptors;uint32_t slots,max_segments;};
static void*anonymous_ram;static uint64_t anonymous_bytes,guest_extra_bytes;static int prepare_fd=-1;static struct prepare_request layout_proof;
static int reserve_swiotlb_first(void){
 int found=-1;
 for(int f=3;f<4096;f++){char path[64],name[256];snprintf(path,sizeof(path),"/proc/self/fd/%d",f);ssize_t n=readlink(path,name,sizeof(name)-1);if(n<0)continue;name[n]=0;if(!strstr(name,"memfd:crosvm_guest (deleted)"))continue;off_t old=lseek(f,0,SEEK_CUR),size=lseek(f,0,SEEK_END);if(old>=0)lseek(f,old,SEEK_SET);if(size==64*1048576){if(found>=0)return -1;found=f;}}
 if(found<0){message("GH_SWI_PREFILL: unique 64 MiB native memfd not found\n");return -1;}
 void*p=mmap(NULL,64*1048576,PROT_READ|PROT_WRITE,MAP_SHARED,found,0);if(p==MAP_FAILED)return -1;if(madvise(p,64*1048576,MADV_HUGEPAGE))return -1;
 for(size_t i=0;i<64*1048576;i+=4096)((volatile unsigned char*)p)[i]=0;
 if(mlock(p,64*1048576))return -1;
 if(!getenv("GH_USE_POOL")&&read_number("/sys/module/gh_hugepage_reserve/parameters/pool_avail")!=0){message("GH_SWI_PREFILL: pool was not fully consumed\n");return -1;}
 message("GH_SWI_PREFILL: DMA memory reserved before anonymous backing\n");return 0;
}
static int prepare_anonymous(void){
 const char*value=getenv("GH_RAM_EXTRA_MIB");uint64_t mib=0;if(!value)return -1;for(const char*p=value;*p>='0'&&*p<='9';p++)mib=mib*10+*p-'0';if(!mib||mib>3968||mib%64)return -1;
 if(reserve_swiotlb_first())return -1;
 guest_extra_bytes=mib*1048576;anonymous_bytes=guest_extra_bytes+192*1048576;int backing=-1;int mapping=MAP_PRIVATE|MAP_ANONYMOUS;if(getenv("GH_USE_POOL")){backing=syscall(279,"gunyah_vm_candidates",0);if(backing<0||ftruncate(backing,anonymous_bytes))return -1;mapping=MAP_SHARED;}anonymous_ram=mmap(NULL,anonymous_bytes,PROT_READ|PROT_WRITE,mapping,backing,0);if(backing>=0)close(backing);if(anonymous_ram==MAP_FAILED)return -1;
 if(madvise(anonymous_ram,anonymous_bytes,MADV_HUGEPAGE))return -1;
 prepare_fd=open("/dev/gunyah_ram_prepare",O_RDWR);if(prepare_fd<0)return -1;
 struct prepare_request req={.address=(uint64_t)anonymous_ram,.bytes=anonymous_bytes};
 if(syscall(29,prepare_fd,0xc0284c01UL,&req))return -1;message("GH_ANONYMOUS_PREPARED: private backing pinned before registration\n");return 0;
}
static int guard_start(int fd){
 char path[80],buf[256];snprintf(path,sizeof(path),"/proc/%d/pagemap",getpid());
 int pm=open(path,O_RDONLY);if(pm<0){message("GH_GUARD_DENY: pagemap unavailable\n");error_set(13);return -1;}
 unsigned count=0;unsigned long total=0;uint64_t bytes=0;int bad=overflow;
 for(unsigned i=0;i<used;i++)if(entries[i].fd==fd){
  unsigned long n=0;struct region*r=&entries[i].r;
  if(entries[i].lend&&r->label)n=layout_proof.max_segments;else if(inspect_range(pm,r->user,r->size,&n))bad=1;
  if(n>MAX_SEGMENTS)bad=1;
  snprintf(buf,sizeof(buf),"GH_GUARD_REGION: label=%u MiB=%lu segments=%lu maximum=%lu\n",r->label,(unsigned long)(r->size>>20),n,MAX_SEGMENTS);message(buf);
  count++;total+=n;bytes+=r->size;
 }
 close(pm);
 snprintf(buf,sizeof(buf),"GH_GUARD_MEMORY: pid=%d parcels=%u MiB=%lu segments=%lu\n",getpid(),count,(unsigned long)(bytes>>20),total);message(buf);
 if(bad||!count){message("GH_GUARD_DENY: refusing GH_VM_START before RM lend; missing pages or over 512 descriptors per parcel\n");error_set(7);return -1;}
 message("GH_GUARD_ALLOW: each registered parcel fits one bounded initial RM message\n");return 0;
}
#include "fdt-many.h"
struct cma_region {uint32_t label;uint64_t guest;uint32_t flags;uint32_t fd;uint64_t offset,size;};
int ioctl(int fd,unsigned long request,...){
 va_list ap;va_start(ap,request);void*arg=va_arg(ap,void*);va_end(ap);
 if(request==0x40284115UL){
  struct cma_region*r=arg;char log[256];
  if(!r||!r->size||(r->flags&~0x37U)){error_set(22);return -1;}
  void*alias;
  if(r->label){if(!anonymous_ram&&prepare_anonymous()){error_set(12);return -1;}uint64_t offset=r->guest-0x88000000ULL;if(r->guest<0x88000000ULL||offset+r->size>guest_extra_bytes){error_set(22);return -1;}alias=(char*)anonymous_ram+offset;}
  else {alias=mmap(0,r->size,PROT_READ|PROT_WRITE,MAP_SHARED,r->fd,r->offset);if(alias==MAP_FAILED)return -1;if(madvise(alias,r->size,MADV_HUGEPAGE)){munmap(alias,r->size);return -1;}}
  struct region translated={.label=r->label,.flags=r->flags&7U,.guest=r->guest,.size=r->size,.user=(uint64_t)alias};
  snprintf(log,sizeof(log),"DROIDVM_CMA_TRANSLATE: label=%u GPA=0x%lx MiB=%lu flags=0x%x\n",r->label,(unsigned long)r->guest,(unsigned long)(r->size>>20),r->flags);message(log);
  int result=ioctl(fd,(r->flags&0x10U)?LEND_MEMORY:SET_MEMORY,&translated);
  if(result<0){int e=__errno?*__errno():(__errno_location?*__errno_location():0);munmap(alias,r->size);error_set(e);}
  return result;
 }
 if(request==0x40104702UL){uint64_t*dt=arg;if(patch_fdt(fd,dt[0],dt[1])){message("DROIDVM_FDT_MANY_FAILED\n");error_set(22);return -1;}}

 if(request==GH_VM_START){unsigned extra=0;for(unsigned i=0;i<used;i++)if(entries[i].fd==fd&&entries[i].r.label&&entries[i].lend)extra++;
  layout_proof=(struct prepare_request){.vmfd=fd,.bytes=guest_extra_bytes,.flags=getenv("GH_SHARE_RAM")?1:0};
  long finalized=prepare_fd<0?-1:syscall(29,prepare_fd,0xc0284c02UL,&layout_proof);char debug[256];snprintf(debug,sizeof(debug),"GH_LAYOUT_RESULT: rc=%ld errno=%d slots=%u expected=%u bytes=%lu expected_bytes=%lu maximum=%u\n",finalized,__errno?*__errno():(__errno_location?*__errno_location():0),layout_proof.slots,extra,(unsigned long)layout_proof.bytes,(unsigned long)guest_extra_bytes,layout_proof.max_segments);message(debug);
  if(prepare_fd<0||finalized||layout_proof.slots!=extra||layout_proof.bytes!=guest_extra_bytes||layout_proof.max_segments>MAX_SEGMENTS){message("GH_GUARD_DENY: before-start private RAM layout failed\n");error_set(7);return -1;}
  char proof[256];snprintf(proof,sizeof(proof),"GH_LAYOUT_PROOF: slots=%u MiB=%lu descriptors=%lu maximum=%u\n",layout_proof.slots,(unsigned long)(layout_proof.bytes>>20),(unsigned long)layout_proof.descriptors,layout_proof.max_segments);message(proof);close(prepare_fd);prepare_fd=-1;munmap(anonymous_ram,anonymous_bytes);anonymous_ram=NULL;
  if(guard_start(fd)<0)return -1;
 }

 struct region tagged;void*kernel_arg=arg;if(request==LEND_MEMORY&&arg&&((struct region*)arg)->label){memcpy(&tagged,arg,sizeof(tagged));tagged.label=tagged.label;kernel_arg=&tagged;}
 unsigned long actual_request=request;if(request==LEND_MEMORY&&arg&&((struct region*)arg)->label&&getenv("GH_SHARE_RAM"))actual_request=SET_MEMORY;
 int result=(int)syscall(29,fd,actual_request,kernel_arg);
 if((request&255)!=5){char log[160];int e=__errno?*__errno():(__errno_location?*__errno_location():0);snprintf(log,sizeof(log),"DROIDVM_IOCTL: fd=%d request=0x%lx result=%d errno=%d\n",fd,request,result,result<0?e:0);message(log);}

 if(result==0&&(request==SET_MEMORY||request==LEND_MEMORY)){
  if(used>=4096)overflow=1;else{entries[used].fd=fd;entries[used].lend=request==LEND_MEMORY;memcpy(&entries[used].r,arg,sizeof(struct region));used++;}
 }
 return result;
}
__attribute__((constructor))static void loaded(void){message("GH_GUARD_LOADED: per-parcel prestart memory guard active\n");}
