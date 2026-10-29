static uint32_t beget(const unsigned char*p){return (uint32_t)p[0]<<24|(uint32_t)p[1]<<16|(uint32_t)p[2]<<8|p[3];}
static void beset(unsigned char*p,uint32_t v){p[0]=v>>24;p[1]=v>>16;p[2]=v>>8;p[3]=v;}
static int patch_fdt(int fd,uint64_t gpa,uint64_t capacity){
 unsigned char*b=0;for(unsigned j=0;j<used;j++){struct region*r=&entries[j].r;if(entries[j].fd==fd&&gpa>=r->guest&&gpa+capacity<=r->guest+r->size)b=(void*)(r->user+gpa-r->guest);}
 if(!b||capacity<40||beget(b)!=0xd00dfeed)return -1;
 uint32_t total=beget(b+4),start=beget(b+8),strings=beget(b+12),pos=start,clone=0,finish=0,label=0,base=0;int depth=0,clonedepth=-1;
 if(total>capacity||strings>=total)return -1;
 while(pos+4<=strings){uint32_t token=beget(b+pos),at=pos;pos+=4;
  if(token==1){if(!clone&&!strncmp((char*)b+pos,"shm-",4)){clone=at;clonedepth=depth;}depth++;while(pos<strings&&b[pos])pos++;pos=(pos+4)&~3U;}
  else if(token==2){depth--;if(clone&&depth==clonedepth){finish=pos;break;}}
  else if(token==3){if(pos+8>strings)return -1;uint32_t len=beget(b+pos),name=beget(b+pos+4);pos+=8;if(name>=total-strings||len>strings-pos)return -1;if(clone){if(!strcmp((char*)b+strings+name,"label")&&len==4)label=pos-clone;if(!strcmp((char*)b+strings+name,"base")&&len==8)base=pos-clone;}pos=(pos+len+3)&~3U;}
  else if(token==9)break;else if(token!=4)return -1;
 }
 if(!finish||!label||!base)return -1;
 unsigned count=0;for(unsigned j=0;j<used;j++)if(entries[j].fd==fd&&entries[j].r.label>0&&entries[j].r.guest<gpa)count++;
 /* Main extra regions precede the DTB only with end placement; use labels instead. */
 uint32_t template_label=beget(b+clone+label);count=0;for(unsigned j=0;j<used;j++)if(entries[j].fd==fd&&entries[j].r.label>0&&entries[j].r.label<template_label)count++;
 if(!count)return 0;
 uint32_t length=finish-clone,extra=length*count;if(total+extra>capacity)return -1;
 unsigned char*copy=malloc(length);if(!copy)return -1;memcpy(copy,b+clone,length);
 memmove(b+finish+extra,b+finish,total-finish);unsigned k=0;
 for(unsigned j=0;j<used;j++){struct region*r=&entries[j].r;if(entries[j].fd!=fd||!r->label||r->label>=template_label)continue;if(r->label>15){free(copy);return -1;}unsigned char*out=b+finish+k++*length;memcpy(out,copy,length);out[8]="0123456789abcdef"[r->label];beset(out+label,0);beset(out+base,r->guest>>32);beset(out+base+4,r->guest);}
 free(copy);beset(b+4,total+extra);beset(b+12,strings+extra);beset(b+36,beget(b+36)+extra);message("DROIDVM_FDT_COMPAT: added missing RAM parcel descriptions\n");return 0;
}
