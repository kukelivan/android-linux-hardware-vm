# SPDX-License-Identifier: GPL-2.0-only
import ctypes,hashlib,json,os,resource,subprocess,time,re
from pathlib import Path
from .config import artifact
OWNED=[]
PARAMS=Path('/sys/module/gh_hugepage_reserve/parameters')
def meminfo():return {k:int(v.split()[0]) for k,v in (s.split(':',1) for s in Path('/proc/meminfo').read_text().splitlines())}
def running_vms():
 result=[]
 for p in Path('/proc').iterdir():
  if not p.name.isdigit():continue
  try:
   state=(p/'stat').read_text().rsplit(')',1)[1].split()[0]
   if (p/'comm').read_text().strip()=='crosvm' and state!='Z':result.append({'pid':int(p.name),'state':state})
  except (FileNotFoundError,ProcessLookupError,PermissionError):pass
 return result

def abi(c):
 expected=json.loads(Path(c['device']).read_text())
 if os.uname().release!=expected['kernel_release']:raise RuntimeError('Host kernel release mismatch; a new device port is required')
 if hashlib.sha256(Path('/sys/kernel/btf/vmlinux').read_bytes()).hexdigest()!=expected['btf_sha256']:raise RuntimeError('Host BTF mismatch; do not load these modules')
def load_module(c,name,params=''):
 if Path('/sys/module',name).exists():raise RuntimeError('Module already loaded; ownership is unclear: '+name)
 fd=os.open(artifact(c,'host/'+name+'.ko'),os.O_RDONLY);lib=ctypes.CDLL(None,use_errno=True)
 try:
  if lib.syscall(273,fd,params.encode(),0):raise OSError(ctypes.get_errno(),'finit_module '+name)
 finally:os.close(fd)
 OWNED.append(name)
def unload(name):
 p=Path('/sys/module',name)
 if not p.exists():return
 if (p/'refcnt').read_text().strip()!='0':raise RuntimeError('Module remains in use: '+name)
 lib=ctypes.CDLL(None,use_errno=True)
 if lib.syscall(106,name.encode(),0):raise OSError(ctypes.get_errno(),'delete_module '+name)
def param(name,value):(PARAMS/name).write_text(str(value)+'\n')
def counters():return dict(s.split('=',1) for s in (PARAMS/'refill_stat').read_text().splitlines() if '=' in s)
def wait_idle(required=0,release=False,timeout=60):
 end=time.monotonic()+timeout;stable=0
 while time.monotonic()<end:
  s=counters();keys=['served','active_vms','acquire_active']+(['pool_avail','pool_total'] if release else [])
  ok=s.get('state')=='idle' and all(s.get(k)=='0' for k in keys) and int(s.get('pool_avail',0))>=required
  stable=stable+1 if ok else 0
  if stable>=3:return
  time.sleep(1)
 raise RuntimeError('Memory has not reached idle/released state; no further VM starts allowed')
def reserve(c,mib):
 abi(c)
 if running_vms():raise RuntimeError('Another VM exists; refuse to share a reserve pool')
 r=subprocess.run([str(artifact(c,'host/kapi_check')),'/sys/kernel/btf/vmlinux'],capture_output=True,text=True,check=True)
 if 'disable=\n' not in r.stdout:raise RuntimeError('KAPI checker rejected required symbols: '+r.stdout)
 order=next(s.split(':',1)[1].strip() for s in Path('/proc/pagetypeinfo').read_text().splitlines() if s.startswith('Page block order:'))
 migrate=re.search(r'(?m)^migrate_cma=(\d+)$',r.stdout)
 if not migrate:raise RuntimeError('KAPI checker did not identify MIGRATE_CMA')
 load_module(c,'gh_hugepage_reserve','pool_want=0 pool_want_with_cma=0 refill_enable=0 acquire_drop_slab=0 boot_acquire=0 migrate_cma_val='+migrate.group(1)+' pageblock_order_val='+order)
 wait_idle(release=True);param('hook_enable',1);blocks=mib//2;param('pool_want',blocks+4);param('acquire',1)
 end=time.monotonic()+60
 while time.monotonic()<end and int((PARAMS/'pool_avail').read_text())<blocks+4:time.sleep(.2)
 param('acquire',0);wait_idle(required=blocks+4);param('pool_want',blocks)
 end=time.monotonic()+20
 while time.monotonic()<end and int((PARAMS/'pool_avail').read_text())>blocks:time.sleep(.1)
 wait_idle(required=blocks)
def release():
 if PARAMS.exists():wait_idle();param('acquire',0);param('pool_want',0);wait_idle(release=True)
def android_preexec(netpid=None):
 lib=ctypes.CDLL(None,use_errno=True)
 if netpid:
  fd=os.open('/proc/'+str(netpid)+'/ns/net',os.O_RDONLY)
  if lib.setns(fd,0x40000000):raise OSError(ctypes.get_errno(),'setns net')
  os.close(fd)
 fd=os.open('/proc/1/ns/mnt',os.O_RDONLY)
 if lib.setns(fd,0x20000):raise OSError(ctypes.get_errno(),'setns mount')
 os.close(fd);os.chroot('/proc/1/root');os.chdir('/');os.umask(0o077)
 resource.setrlimit(resource.RLIMIT_MEMLOCK,(-1,-1))
def android_path(c,p):
 p=Path(p).resolve()
 return c['android_rootfs'].rstrip('/')+str(p)
def headroom(mib):
 m=meminfo();need=max(2048,(m['MemTotal']//1024+4)//5)+512+mib
 if m['MemAvailable']//1024<need:raise RuntimeError(f'Need {need} MiB MemAvailable before reservation')
