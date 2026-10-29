# SPDX-License-Identifier: GPL-2.0-only
"""Root-only lifecycle supervisor. Nothing runs on import."""
import json,os,resource,select,signal,socket,subprocess,threading,time
from pathlib import Path
from . import config,host,network
from .console import LifecycleLock,current,rpc,send,wait_line

def mappings(name,mib):
 if name=='classic':return [],[]
 sizes=[(0,mib//2),(mib//2,mib-64-mib//2)] if name=='split' else [(0,128)]+[(o,min(128,mib-64-o)) for o in range(128,mib-64,128)]
 args=[];fds=[]
 try:
  for offset,size in sizes:
   fd=os.memfd_create('crosvm_guest_ram_'+str(offset),os.MFD_CLOEXEC);os.ftruncate(fd,size<<20);fds.append(fd);args+=['--file-backed-mapping',f'addr={0x80000000+(offset<<20)},size={size<<20},path=/proc/self/fd/{fd},rw,ram']
  return args,fds
 except BaseException:
  for fd in fds:os.close(fd)
  raise

def fsck(c,log):
 if host.running_vms():raise RuntimeError('Refuse disk access while any crosvm exists')
 for name in ['rootfs.img','workspace.img']:
  image=Path(c['state'])/name
  if not image.exists():continue
  p=subprocess.run(['e2fsck','-p',str(image)],capture_output=True,text=True);log('filesystem',image=name,rc=p.returncode,output=p.stdout+p.stderr)
  if p.returncode not in (0,1):raise RuntimeError('Filesystem check needs operator review: '+name)

def supervise(name, memory_mib=None):
 c=config.load();state=Path(c['state']);p=config.profile(name);mib=config.memory(p,memory_mib);proc=None;net=[];fds=[];stop=threading.Event();ready=threading.Event();shutdown=threading.Event();errors=[];telemetry=None;sample_thread=None
 def log(event,**kw):
  with (state/'events.jsonl').open('a') as f:f.write(json.dumps({'time':time.time(),'event':event,**kw})+'\n');f.flush();os.fdatasync(f.fileno())
 def done(*_):shutdown.set()
 signal.signal(signal.SIGTERM,done);signal.signal(signal.SIGINT,done)
 def sample():
  with (state/'memory.jsonl').open('a') as out:
   while not stop.wait(.5):
    row={'time':time.time(),'android':host.meminfo(),'psi':Path('/proc/pressure/memory').read_text()}
    try:row['pool']=host.counters()
    except OSError:pass
    out.write(json.dumps(row)+'\n');out.flush();os.fdatasync(out.fileno())
 def prepare_guest():
  try:
   wait_line(0,rb'root@linux-guest:',180)
   if name=='grow' and not ready.wait(30):raise RuntimeError('Growth agent is missing; refusing to start Docker')
   code,out=rpc('systemctl unmask --runtime containerd.service docker.service docker.socket; systemctl daemon-reload; systemctl start docker && docker info --format "{{.ServerVersion}} {{.Driver}}"',120)
   if code:raise RuntimeError('Docker startup failed: '+out[-500:])
   (state/'ready.json').write_text(json.dumps({'profile':name,'docker':out,'ssh_port':c['ssh_port']}));log('ready',profile=name)
  except Exception as e:errors.append(str(e));shutdown.set();log('startup_error',error=str(e))
 with LifecycleLock():
  if host.running_vms():raise RuntimeError('Another VMM is active')
  host.abi(c);host.headroom(mib);(state/'console.log').write_text('')
  for f in ['ready.json','stop.request']:(state/f).unlink(missing_ok=True)
  log('starting',profile=name,initial_mib=mib)
  wake=Path('/sys/power/wake_lock');wake.write_text('android-linux-hardware-vm\n')
  try:
   fsck(c,log);host.reserve(c,mib);host.headroom(0)
   for mod in p['host_modules']:host.load_module(c,mod,'enabled=1 target_tgid=0' if mod=='gunyah_ram_labels' else '')
   for mod in ['rootfs.img','workspace.img']:
    if (state/mod).is_symlink():raise RuntimeError('Use owned image files, not shared symlinks')
   stage=Path('/proc/1/root/data/local/tmp/android-linux-hardware-vm');stage.mkdir(parents=True,exist_ok=True)
   net=list(network.start(state));Path('/sys/kernel/mm/transparent_hugepage/shmem_enabled').write_text('advise\n')
   kernel=config.artifact(c,'guest/legacyImage' if p['kernel']=='legacy' else 'guest/Image');initrd=config.artifact(c,'initrd/'+name+'.cpio');extra,fds=mappings(name,mib)
   args=[c['crosvm'],'--log-level=info','--no-syslog','run','--disable-sandbox','--hugepages','--hypervisor','gunyah','--protected-vm-without-firmware','--net','tap-name=vmtap0,mac=02:47:44:4f:43:4b','--mem',str(mib),'--cpus','num-cores=2','--no-balloon','--no-rng','--serial','type=stdout,hardware=virtio-console,console,num=1,stdin','--params','console=hvc0 panic=1 rdinit=/init root=/dev/vda rw net.ifnames=0','--initrd',host.android_path(c,initrd),'--block','path='+host.android_path(c,state/'rootfs.img')+',root=true']
   if (state/'workspace.img').exists():args+=['--block','path='+host.android_path(c,state/'workspace.img')]
   if name!='classic':args+=['--fdt-position=after-payload']+extra
   server=None
   if name=='grow':
    path='/proc/1/root/data/local/tmp/android-linux-vm-guest.sock';Path(path).unlink(missing_ok=True);server=socket.socket(socket.AF_UNIX);server.bind(path);server.listen(1);server.settimeout(45);args+=['--serial','type=unix-stream,path=/data/local/tmp/android-linux-vm-guest.sock,input-unix-stream,hardware=virtio-console,num=2']
   args+=[host.android_path(c,kernel)];env=os.environ.copy();env.update(LD_PRELOAD=host.android_path(c,config.artifact(c,'shims/'+name+'.so')),LD_LIBRARY_PATH=host.android_path(c,config.artifact(c,'shims')),GH_RAM_EXTRA_MIB=str(mib-192),GH_USE_POOL='1',GH_SHARE_RAM='1')
   fifo=state/'console.in'
   if not fifo.exists():os.mkfifo(fifo,0o600)
   cin=os.open(fifo,os.O_RDWR)
   def enter():
    if name=='split':Path('/sys/module/gunyah_ram_labels/parameters/target_tgid').write_text(str(os.getpid()))
    host.android_preexec(net[0].pid)
   with (state/'console.log').open('ab',buffering=0) as out:proc=subprocess.Popen(args,env=env,preexec_fn=enter,pass_fds=fds,stdin=cin,stdout=out,stderr=subprocess.STDOUT,start_new_session=True)
   os.close(cin);vmstate={'boot_id':Path('/proc/sys/kernel/random/boot_id').read_text().strip(),'pid':proc.pid,'memory_mib':mib,'profile':name,'start_ticks':Path(f'/proc/{proc.pid}/stat').read_text().rsplit(')',1)[1].split()[19]};(state/'state.json').write_text(json.dumps(vmstate));network.forward(state,c['ssh_port']);sample_thread=threading.Thread(target=sample);sample_thread.start();threading.Thread(target=prepare_guest,daemon=True).start()
   buffer=b'';pending=False;disabled=False;poweroff_sent=False;poweroff_deadline=0
   if server:telemetry,_=server.accept();telemetry.setblocking(False);server.close()
   while proc.poll() is None:
    if (state/'stop.request').exists():shutdown.set()
    if shutdown.is_set() and not poweroff_sent:
     send('systemctl poweroff');poweroff_sent=True;poweroff_deadline=time.monotonic()+120
    if poweroff_sent and time.monotonic()>poweroff_deadline:raise RuntimeError('Guest did not power off; no SIGKILL or reboot attempted')
    if telemetry and select.select([telemetry],[],[],.2)[0]:
     data=telemetry.recv(65536)
     if not data:telemetry.close();telemetry=None;continue
     buffer+=data
     while b'\n' in buffer:
      line,buffer=buffer.split(b'\n',1);s=line.decode(errors='replace').strip();log('guest',line=s)
      if s.startswith('STAT '):
       total,avail=map(int,s.split()[1:3]);ready.set()
       if avail<256*1024 and not pending and mib<p['maximum_mib'] and not disabled and not shutdown.is_set():
        with socket.socket(socket.AF_UNIX) as ctl:
         ctl.settimeout(30);ctl.connect('/proc/1/root/data/local/tmp/android-linux-vm-grow.sock');ctl.sendall(b'GROW\n');reply=ctl.recv(4096).decode().strip()
        log('growth',reply=reply)
        if reply.startswith('SHARED '):
         _,handle,gpa,size,new=reply.split();mib=int(new);pending=True;telemetry.sendall(f'ACCEPT {handle} {gpa} {size}\n'.encode());vmstate['memory_mib']=mib;(state/'state.json').write_text(json.dumps(vmstate))
        elif reply.startswith('LIMIT '):disabled=True
        else:raise RuntimeError(reply)
      elif s.startswith('ACK '):
       if int(s.split()[2]):raise RuntimeError('Guest MEM_ACCEPT/hot-add failed: '+s)
       pending=False
    else:time.sleep(.1)
   log('guest_exited',rc=proc.returncode,assigned_mib=mib)
  finally:
   # Never reclaim pins or touch guest disks while the VMM can still access them.
   if proc and proc.poll() is None:
    try:
     wait_line(0,rb'root@linux-guest:',30);send('systemctl poweroff');proc.wait(timeout=120)
    except (RuntimeError,subprocess.TimeoutExpired):
     if proc.poll() is None:
      log('cleanup_blocked',pid=proc.pid);raise RuntimeError('VM remains alive; inspect logs before another start')
   if telemetry:telemetry.close()
   for fd in fds:os.close(fd)
   for child in reversed(net):
    if child.poll() is None:child.terminate();child.wait(timeout=10)
   if 'gunyah_ram_labels' in host.OWNED:Path('/sys/module/gunyah_ram_labels/parameters/target_tgid').write_text('0\n')
   if 'gh_hugepage_reserve' in host.OWNED:host.release()
   for mod in reversed(host.OWNED):host.unload(mod)
   host.OWNED.clear();Path('/sys/power/wake_unlock').write_text('android-linux-hardware-vm\n');stop.set()
   if sample_thread:sample_thread.join(timeout=5)
   fsck(c,log);(state/'ready.json').unlink(missing_ok=True);log('stopped',resources_released=True)
 if errors:raise RuntimeError('; '.join(errors))
