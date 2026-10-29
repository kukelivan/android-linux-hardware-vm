# SPDX-License-Identifier: GPL-2.0-only
import ctypes,fcntl,json,os,socket,struct,subprocess,time
from pathlib import Path

def start(state):
 holder=subprocess.Popen(['unshare','--net','/bin/sleep','infinity'],stdout=subprocess.DEVNULL,stderr=open(state/'netns.log','w'),start_new_session=True);time.sleep(.2)
 if holder.poll() is not None:raise RuntimeError('Network namespace creation failed')
 process=None;tap=None
 net=['nsenter','--net=/proc/'+str(holder.pid)+'/ns/net']
 try:
  for cmd in [['ip','link','add','vmbr0','type','bridge'],['ip','link','set','vmbr0','up'],['ip','tuntap','add','vmtap0','mode','tap'],['ip','link','set','vmtap0','master','vmbr0'],['ip','link','set','vmtap0','up'],['ip','link','set','lo','up']]:subprocess.run(net+cmd,check=True)
  lib=ctypes.CDLL(None,use_errno=True);orig=os.open('/proc/self/ns/net',os.O_RDONLY);target=os.open('/proc/'+str(holder.pid)+'/ns/net',os.O_RDONLY)
  try:
   if lib.setns(target,0x40000000):raise OSError(ctypes.get_errno(),'netns')
   tap=os.open('/dev/net/tun',os.O_RDWR);fcntl.ioctl(tap,0x400454ca,struct.pack('16sH',b'slirp0',0x0002|0x1000))
   subprocess.run(['ip','link','set','slirp0','master','vmbr0'],check=True);subprocess.run(['ip','link','set','slirp0','up'],check=True)
  finally:
   if lib.setns(orig,0x40000000):raise OSError(ctypes.get_errno(),'restore netns')
   os.close(orig);os.close(target)
  path=state/'slirp.sock'
  if path.exists():path.unlink()
  process=subprocess.Popen(['slirp4netns','--netns-type=tapfd','--mtu=1500','--api-socket='+str(path),str(tap)],pass_fds=[tap],stdout=open(state/'slirp.log','w'),stderr=subprocess.STDOUT,start_new_session=True);os.close(tap);tap=None
  for _ in range(50):
   if path.exists():return holder,process
   if process.poll() is not None:break
   time.sleep(.1)
  raise RuntimeError('slirp4netns API unavailable')
 except BaseException:
  if tap is not None:
   try:os.close(tap)
   except OSError:pass
  if process is not None and process.poll() is None:process.terminate();process.wait(timeout=5)
  if holder.poll() is None:holder.terminate();holder.wait(timeout=5)
  raise

def forward(state,port):
 with socket.socket(socket.AF_UNIX) as s:
  s.settimeout(5);s.connect(str(state/'slirp.sock'));s.sendall((json.dumps({'execute':'add_hostfwd','arguments':{'proto':'tcp','host_addr':'127.0.0.1','host_port':port,'guest_addr':'10.0.2.100','guest_port':22}})+'\n').encode());reply=json.loads(s.recv(8192))
  if 'error' in reply:raise RuntimeError('SSH forwarding failed: '+str(reply))
