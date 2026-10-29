#include <libfdt.h>
#define FP(call) do{int fdt_rc_=(call);if(fdt_rc_<0){char m[256];snprintf(m,sizeof(m),"FDT_ERROR: %s %s\n",#call,fdt_strerror(fdt_rc_));message(m);return fdt_rc_;}}while(0)
static int patch_fdt(int fd,uint64_t gpa,uint64_t capacity){
 void*b=NULL;unsigned extras=0;
 for(unsigned i=0;i<used;i++){struct region*r=&entries[i].r;if(entries[i].fd==fd&&gpa>=r->guest&&gpa+capacity<=r->guest+r->size)b=(void*)(r->user+gpa-r->guest);}
 if(!b||capacity>2097152)return -1;FP(fdt_check_header(b));FP(fdt_open_into(b,b,capacity));
 for(unsigned i=0;i<used;i++){
  struct region*r=&entries[i].r;if(entries[i].fd!=fd||!r->label||!entries[i].lend)continue;
  int devices=fdt_path_offset(b,"/gunyah-vm-config/vdevices");if(devices<0)return devices;
  char name[64];snprintf(name,sizeof(name),"ram-extra-%u",r->label);int node=fdt_add_subnode(b,devices,name);if(node<0)return node;
  FP(fdt_setprop_u32(b,node,"#address-cells",2));FP(fdt_setprop_u32(b,node,"#size-cells",2));FP(fdt_setprop_string(b,node,"vdevice-type","shm"));FP(fdt_setprop(b,node,"peer-default",NULL,0));FP(fdt_setprop_u64(b,node,"dma_base",0));
  FP(fdt_setprop_string(b,node,"push-compatible","gunyah,extra-ram"));
  int mem=fdt_add_subnode(b,node,"memory");if(mem<0)return mem;
  FP(fdt_setprop(b,mem,"optional",NULL,0));FP(fdt_setprop_u32(b,mem,"label",r->label));FP(fdt_setprop_u32(b,mem,"#address-cells",2));FP(fdt_setprop_u64(b,mem,"base",r->guest));
  int reserved=fdt_path_offset(b,"/reserved-memory");if(reserved<0)return reserved;
  snprintf(name,sizeof(name),"ram-extra@%lx",(unsigned long)r->guest);node=fdt_add_subnode(b,reserved,name);if(node<0)return node;
  fdt64_t reg[2]={cpu_to_fdt64(r->guest),cpu_to_fdt64(r->size)};
  FP(fdt_setprop(b,node,"reg",reg,sizeof(reg)));FP(fdt_setprop_u32(b,node,"qcom,label",r->label));FP(fdt_setprop_string(b,node,"status","disabled"));extras++;
 }
 int devices=fdt_path_offset(b,"/gunyah-vm-config/vdevices"),item;
 fdt_for_each_subnode(item,b,devices){int len;const char*type=fdt_getprop(b,item,"vdevice-type",&len);if(type&&len==4&&!strcmp(type,"shm")){FP(fdt_setprop_u32(b,item,"#address-cells",2));FP(fdt_setprop_u32(b,item,"#size-cells",2));}}
 {
 int dev=fdt_path_offset(b,"/gunyah-vm-config/vdevices");if(dev<0)return dev;
 int rpc=fdt_add_subnode(b,dev,"grow-rm-rpc");if(rpc<0)return rpc;
 FP(fdt_setprop_string(b,rpc,"vdevice-type","rm-rpc"));
 FP(fdt_setprop_string(b,rpc,"generate","/hypervisor/resource-manager"));
 }
 FP(fdt_pack(b));char msg[128];snprintf(msg,sizeof(msg),"DROIDVM_FDT_MANY: %u explicit RAM parcels, DTB %u bytes\n",extras,fdt_totalsize(b));message(msg);return 0;
}
