// SPDX-License-Identifier: GPL-2.0-only
#define _GNU_SOURCE
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <poll.h>
#include <errno.h>
struct request {uint32_t handle,flags;uint64_t gpa,size;};
static int emit(int fd,const char*s){size_t n=strlen(s);return write(fd,s,n)==n?0:-1;}
static void stats(int fd){unsigned long total=0,avail=0;char line[256],out[256];FILE*f=fopen("/proc/meminfo","r");if(!f)return;while(fgets(line,sizeof(line),f)){sscanf(line,"MemTotal: %lu kB",&total);sscanf(line,"MemAvailable: %lu kB",&avail);}fclose(f);snprintf(out,sizeof(out),"STAT %lu %lu\n",total,avail);emit(fd,out);}
int main(void){
 int tty=-1;for(int i=0;i<200;i++){tty=open("/dev/hvc1",O_RDWR|O_NOCTTY);if(tty>=0)break;usleep(50000);}if(tty<0)return 3;
 struct termios t;if(!tcgetattr(tty,&t)){cfmakeraw(&t);t.c_cc[VMIN]=0;t.c_cc[VTIME]=0;tcsetattr(tty,TCSANOW,&t);}int ctl=open("/dev/gunyah_grow_guest",O_RDWR);if(ctl<0)return 4;
 emit(tty,"READY\n");char buffer[256];size_t used=0;
 for(;;){stats(tty);struct pollfd p={.fd=tty,.events=POLLIN};if(poll(&p,1,200)<=0)continue;
  char c;while(read(tty,&c,1)==1){if(c=='\r')continue;if(c!='\n'){if(used<sizeof(buffer)-1)buffer[used++]=c;continue;}buffer[used]=0;used=0;
   unsigned h;unsigned long long g,s;
   if(sscanf(buffer,"ACCEPT %u %llu %llu",&h,&g,&s)==3){struct request r={.handle=h,.gpa=g,.size=s};int rc=ioctl(ctl,0xc0184c04UL,&r);char out[128];snprintf(out,sizeof(out),"ACK %u %d %d\n",h,rc,rc<0?errno:0);emit(tty,out);}
  }
 }
}
