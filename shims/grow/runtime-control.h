#include <pthread.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <time.h>
static int current_errno(void){return __errno?*__errno():(__errno_location?*__errno_location():0);}
static int vm_control,helper;static unsigned added;static int poisoned;
static unsigned long long parameter(const char*name){char path[256],b[4096];snprintf(path,sizeof(path),"/sys/module/gh_hugepage_reserve/parameters/%s",name);int f=open(path,O_RDONLY);if(f<0)f=open("/sys/module/gh_hugepage_reserve/parameters/refill_stat",O_RDONLY);if(f<0)return ~0ULL;int n=read(f,b,sizeof(b)-1);close(f);if(n<=0)return ~0ULL;b[n]=0;char*p=b;if(strchr(b,'=')){p=strstr(b,name);if(!p)return ~0ULL;p=strchr(p,'=');if(!p)return ~0ULL;p++;}while(*p==' ')p++;unsigned long long v=0;if(*p<'0'||*p>'9')return ~0ULL;while(*p>='0'&&*p<='9')v=v*10+*p++-'0';return v;}
static int set_parameter(const char*n,unsigned long long v){char p[256],s[64];snprintf(p,sizeof(p),"/sys/module/gh_hugepage_reserve/parameters/%s",n);int f=open(p,O_WRONLY);if(f<0)return -1;int z=snprintf(s,sizeof(s),"%llu\n",v);int r=write(f,s,z);close(f);return r==z?0:-1;}
static unsigned long memory_mib(const char*key){char b[4096];int f=open("/proc/meminfo",O_RDONLY);if(f<0)return 0;int n=read(f,b,sizeof(b)-1);close(f);if(n<0)return 0;b[n]=0;char*p=strstr(b,key);if(!p)return 0;p+=strlen(key);while(*p==' ')p++;unsigned long v=0;while(*p>='0'&&*p<='9')v=v*10+*p++-'0';return v/1024;}
static unsigned long available_mib(void){return memory_mib("MemAvailable:");}
static unsigned long android_reserve_mib(void){unsigned long r=memory_mib("MemTotal:")/5;return (r>2048?r:2048)+512;}
static void grow(int client){char out[256];unsigned long avail=available_mib(),reserve=android_reserve_mib(),required=reserve+128;
 if(poisoned||added>=30||avail<required){int n=snprintf(out,sizeof(out),"LIMIT %s assigned_mib=%u available_mib=%lu required_mib=%lu\n",poisoned?"poisoned":added>=30?"configured_max":"android_headroom",256+added*128,avail,required);send(client,out,n,0);return;}
 unsigned long long held=parameter("pool_total");if(held==~0ULL){send(client,"ERROR pool_total\n",17,0);return;}
 message("GROW_BEGIN_RESERVE\n");set_parameter("pool_want",held+64);set_parameter("acquire",1);
 for(int i=0;i<150&&parameter("pool_avail")<64;i++)usleep(100000);
 set_parameter("acquire",0);for(int i=0;i<50&&parameter("acquire_active");i++)usleep(100000);
 if(parameter("pool_avail")<64||parameter("acquire_active")){int n=snprintf(out,sizeof(out),"LIMIT hugepage_reserve assigned_mib=%u available_mib=%lu pool_avail_blocks=%llu acquire_active=%llu\n",256+added*128,available_mib(),parameter("pool_avail"),parameter("acquire_active"));send(client,out,n,0);return;}
 if(available_mib()<reserve){set_parameter("pool_want",held);int n=snprintf(out,sizeof(out),"LIMIT android_headroom_after_reserve assigned_mib=%u available_mib=%lu reserve_mib=%lu\n",256+added*128,available_mib(),reserve);send(client,out,n,0);return;}
 size_t size=128UL<<20,align=2UL<<20;int f=syscall(279,"grow-ram",1);if(f<0||ftruncate(f,size)){send(client,"ERROR memfd\n",12,0);return;}
 void*reservation=mmap(0,size+align,PROT_NONE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);if(reservation==MAP_FAILED){close(f);return;}
 uintptr_t a=((uintptr_t)reservation+align-1)&~(align-1);void*map=mmap((void*)a,size,PROT_READ|PROT_WRITE,MAP_SHARED|MAP_FIXED,f,0);if(map==MAP_FAILED){close(f);return;}
 if(a>(uintptr_t)reservation)munmap(reservation,a-(uintptr_t)reservation);if(a+size<(uintptr_t)reservation+size+align)munmap((void*)(a+size),(uintptr_t)reservation+align-a);
 madvise(map,size,MADV_HUGEPAGE);struct region r={.label=0x47560001+added,.flags=7,.guest=0x90000000ULL+added*(128ULL<<20),.size=size,.user=(uintptr_t)map};
 message("GROW_BEFORE_REGISTER\n");if(syscall(29,vm_control,SET_MEMORY,&r)){int e=current_errno();poisoned=1;int n=snprintf(out,sizeof(out),"ERROR register errno=%d\n",e);send(client,out,n,0);return;}message("GROW_AFTER_REGISTER\n");
 struct{uint32_t vmfd,label,handle,flags;}q={vm_control,r.label,0,0};message("GROW_BEFORE_SHARE\n");if(syscall(29,helper,0xc0104c03UL,&q)){int e=current_errno();poisoned=1;int n=snprintf(out,sizeof(out),"ERROR share errno=%d\n",e);send(client,out,n,0);return;}message("GROW_AFTER_SHARE\n");added++;int n=snprintf(out,sizeof(out),"SHARED %u %llu %lu %u\n",q.handle,(unsigned long long)r.guest,(unsigned long)size,256+added*128);send(client,out,n,0);
}
static void*controller(void*unused){int s=socket(AF_UNIX,SOCK_STREAM,0);struct sockaddr_un a={.sun_family=AF_UNIX};strcpy(a.sun_path,"/data/local/tmp/android-linux-vm-grow.sock");unlink(a.sun_path);if(bind(s,(void*)&a,sizeof(a))||listen(s,1)){message("GROW_SOCKET_ERROR\n");return 0;}message("GROW_CONTROL_READY\n");for(;;){int c=accept(s,0,0);if(c<0)continue;char b[16];int n=read(c,b,15);if(n>=4&&!memcmp(b,"GROW",4))grow(c);close(c);}return 0;}
static void start_runtime_controller(int fd){vm_control=fd;helper=open("/dev/gunyah_grow_host",O_RDWR);if(helper<0){message("GROW_HELPER_OPEN_ERROR\n");return;}pthread_t t;if(!pthread_create(&t,0,controller,0))pthread_detach(t);}
