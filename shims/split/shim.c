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
static void message(const char*s){write(2,s,strlen(s));}
#define MAX_SEGMENTS 512UL
#define GH_VM_START 0x4703UL
#define SET_MEMORY 0x40204701UL
#define LEND_MEMORY 0x40204111UL
#define PFN_MASK ((1ULL<<55)-1)
struct region {uint32_t label,flags;uint64_t guest,size,user;};
struct entry {int fd;struct region r;};
static int fdt_ready;
static struct entry entries[32];static unsigned used;static int overflow;
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
static int guard_start(int fd){
 char path[80],buf[256];snprintf(path,sizeof(path),"/proc/%d/pagemap",getpid());
 int pm=open(path,O_RDONLY);if(pm<0){message("GH_GUARD_DENY: pagemap unavailable\n");error_set(13);return -1;}
 unsigned count=0;unsigned long total=0;uint64_t bytes=0;int bad=overflow;
 for(unsigned i=0;i<used;i++)if(entries[i].fd==fd){
  unsigned long n=0;struct region*r=&entries[i].r;
  if(inspect_range(pm,r->user,r->size,&n))bad=1;
  if(n>MAX_SEGMENTS)bad=1;
  snprintf(buf,sizeof(buf),"GH_GUARD_REGION: label=%u MiB=%lu segments=%lu maximum=%lu\n",r->label,(unsigned long)(r->size>>20),n,MAX_SEGMENTS);message(buf);
  count++;total+=n;bytes+=r->size;
 }
 close(pm);
 snprintf(buf,sizeof(buf),"GH_GUARD_MEMORY: pid=%d parcels=%u MiB=%lu segments=%lu\n",getpid(),count,(unsigned long)(bytes>>20),total);message(buf);
 if(bad||!count){message("GH_GUARD_DENY: refusing GH_VM_START before RM lend; missing pages or over 512 descriptors per parcel\n");error_set(7);return -1;}
 message("GH_GUARD_ALLOW: each registered parcel fits one bounded initial RM message\n");return 0;
}
#include "fdt-compat.h"
struct cma_region {uint32_t label;uint64_t guest;uint32_t flags;uint32_t fd;uint64_t offset,size;};
int ioctl(int fd,unsigned long request,...){
 va_list ap;va_start(ap,request);void*arg=va_arg(ap,void*);va_end(ap);
 if(request==0x40284115UL){
  struct cma_region*r=arg;char log[256];
  if(!r||!r->size||(r->flags&~0x37U)){error_set(22);return -1;}
  void*alias=mmap(0,r->size,PROT_READ|PROT_WRITE,MAP_SHARED,r->fd,r->offset);
  if(alias==MAP_FAILED)return -1;
  if(madvise(alias,r->size,MADV_HUGEPAGE)){munmap(alias,r->size);return -1;}
  struct region translated={.label=r->label,.flags=r->flags&7U,.guest=r->guest,.size=r->size,.user=(uint64_t)alias};
  snprintf(log,sizeof(log),"DROIDVM_CMA_TRANSLATE: label=%u GPA=0x%lx MiB=%lu flags=0x%x\n",r->label,(unsigned long)r->guest,(unsigned long)(r->size>>20),r->flags);message(log);
  int result=ioctl(fd,(r->flags&0x10U)?LEND_MEMORY:SET_MEMORY,&translated);
  if(result<0){int e=__errno?*__errno():(__errno_location?*__errno_location():0);munmap(alias,r->size);error_set(e);}
  return result;
 }
 if(request==0x40104702UL){uint64_t *dt=arg;if(patch_fdt(fd,dt[0],dt[1])){message("DROIDVM_FDT_COMPAT_FAILED\n");error_set(22);return -1;}fdt_ready=1;}
 if(request==GH_VM_START){
  char v[32];int h=open("/sys/module/gunyah_ram_labels/parameters/target_tgid",O_RDONLY);int n=h>=0?read(h,v,sizeof(v)-1):-1;if(h>=0)close(h);if(n>0)v[n]=0;int target=0;if(n>0)for(int j=0;j<n&&v[j]>='0'&&v[j]<='9';j++)target=target*10+v[j]-'0';
  if(used!=3||!fdt_ready||n<=0||target!=getpid()){message("GH_GUARD_DENY: split RAM requires exactly two main regions and its PID-scoped adapter\n");error_set(13);return -1;}
  if(guard_start(fd)<0)return -1;
 }
 struct region tagged;void *kernel_arg=arg;
 if(request==LEND_MEMORY&&arg&&((struct region*)arg)->label){memcpy(&tagged,arg,sizeof(tagged));tagged.label=0x47530000U|tagged.label;kernel_arg=&tagged;}
 int result=(int)syscall(29,fd,request,kernel_arg);
 if(request==GH_VM_START||request==SET_MEMORY||request==LEND_MEMORY){char log[160];int e=__errno?*__errno():(__errno_location?*__errno_location():0);snprintf(log,sizeof(log),"DROIDVM_IOCTL: fd=%d request=0x%lx result=%d errno=%d\n",fd,request,result,result<0?e:0);message(log);}

 if(result==0&&(request==SET_MEMORY||request==LEND_MEMORY)){
  if(used>=32)overflow=1;else{entries[used].fd=fd;memcpy(&entries[used].r,arg,sizeof(struct region));used++;}
 }
 return result;
}
__attribute__((constructor))static void loaded(void){message("GH_GUARD_LOADED: per-parcel prestart memory guard active\n");}
