# SPDX-License-Identifier: GPL-2.0-only
import pathlib,os,json,time,uuid,re,fcntl,subprocess,shlex,base64,hashlib,tarfile,io,math
BASE=pathlib.Path(os.environ.get('DROIDVM_STATE','/var/lib/android-linux-hardware-vm'))
PARAMS=pathlib.Path('/sys/module/gh_hugepage_reserve/parameters')
def current():
 try:
  s=json.loads((BASE/'state.json').read_text());p=pathlib.Path('/proc')/str(s['pid'])
  if s['boot_id']!=pathlib.Path('/proc/sys/kernel/random/boot_id').read_text().strip():return None
  if (p/'comm').read_text().strip()!='crosvm':return None
  fields=(p/'stat').read_text().rsplit(')',1)[1].split()
  if s.get('start_ticks') and str(s['start_ticks'])!=fields[19]:return None
  return s,fields[0]
 except (OSError,ValueError,KeyError):return None
class LifecycleLock:
 def __enter__(self):
  self.fd=os.open('/run/android-linux-hardware-vm.lock',os.O_CREAT|os.O_RDWR,0o600)
  try:fcntl.flock(self.fd,fcntl.LOCK_EX|fcntl.LOCK_NB)
  except BlockingIOError:os.close(self.fd);raise RuntimeError('Another VM lifecycle operation is running')
  return self
 def __exit__(self,*args):os.close(self.fd)
def meminfo():
 return {k:int(v.split()[0]) for k,v in (line.split(':',1) for line in pathlib.Path('/proc/meminfo').read_text().splitlines())}
def snapshot():
 d={'time':time.time(),'meminfo_kib':meminfo(),'vm':current()}
 if PARAMS.exists():d['module']=dict(s.split('=',1) for s in (PARAMS/'refill_stat').read_text().splitlines() if '=' in s)
 return d
def send(command):
 if not current():raise RuntimeError('VM is not running')
 fd=os.open(BASE/'console.in',os.O_WRONLY|os.O_NONBLOCK)
 try:os.write(fd,(command+'\n').encode())
 finally:os.close(fd)
def read_since(offset):
 with (BASE/'console.log').open('rb') as f:f.seek(offset);return f.read().replace(b'\r\n',b'\n')
def wait_line(offset,pattern,timeout):
 end=time.monotonic()+timeout;blocked_since=None
 while time.monotonic()<end:
  data=read_since(offset);match=re.search(pattern,data,re.M)
  if match:return match,data
  vm=current()
  if not vm:raise RuntimeError('VM exited; inspect console.log')
  if vm[1]=='D':
   if blocked_since is None:blocked_since=time.monotonic()
   elif time.monotonic()-blocked_since>=30:raise RuntimeError('VMM remained uninterruptible for 30 seconds; inspect before another launch')
  else:blocked_since=None
  time.sleep(.5)
 raise TimeoutError('Guest operation timed out')
def rpc(command,timeout=30):
 token=uuid.uuid4().hex;marker='VM_RPC_'+token
 fd=os.open(BASE/'console.lock',os.O_CREAT|os.O_RDWR,0o600)
 try:
  fcntl.flock(fd,fcntl.LOCK_EX)
  offset=(BASE/'console.log').stat().st_size
  send("printf '\\n"+marker+"_BEGIN\\n'; /bin/bash -c "+shlex.quote(command)+"; printf '\\n"+marker+'=%s\\n\' "$?"')
  match,data=wait_line(offset,rb'^'+marker.encode()+rb'=(\d+)\n',timeout)
  begin=data.find(('\n'+marker+'_BEGIN\n').encode()); output=data[begin+len(marker)+8:match.start()] if begin>=0 else data[:match.start()]
  return int(match.group(1)),output.decode(errors='replace')
 finally:os.close(fd)
