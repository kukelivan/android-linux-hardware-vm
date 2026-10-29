# SPDX-License-Identifier: GPL-2.0-only
import argparse,hashlib,json,os,shutil,subprocess,sys,time
from pathlib import Path
from . import config,host

def main():
 ap=argparse.ArgumentParser(prog='droidvm');sub=ap.add_subparsers(dest='action',required=True)
 p=sub.add_parser('configure');p.add_argument('--android-rootfs',required=True);p.add_argument('--device',default=str(config.REPO/'devices/reference.json'));p.add_argument('--artifacts',default=str(config.REPO/'build'));p.add_argument('--ssh-port',type=int,default=22222)
 p=sub.add_parser('doctor');p.add_argument('--profile',default='grow');p.add_argument('--memory-mib',type=int)
 p=sub.add_parser('up');p.add_argument('profile',choices=['classic','split','shared','custom-fixed','grow'],default='grow',nargs='?');p.add_argument('--memory-mib',type=int)
 p=sub.add_parser('_supervise');p.add_argument('profile');p.add_argument('--memory-mib',type=int)
 sub.add_parser('status');sub.add_parser('down');p=sub.add_parser('exec');p.add_argument('command',nargs=argparse.REMAINDER);sub.add_parser('ssh')
 a=ap.parse_args();state=config.home()
 if a.action=='configure':
  if not 1024<=a.ssh_port<=65535:raise ValueError('SSH port must be 1024..65535')
  if not Path(a.android_rootfs).is_absolute():raise ValueError('Android rootfs path must be absolute')
  state.mkdir(parents=True,exist_ok=True,mode=0o700);state.chmod(0o700)
  c={'android_rootfs':a.android_rootfs.rstrip('/'),'device':str(Path(a.device).resolve()),'artifacts':str(Path(a.artifacts).resolve()),'crosvm':'/apex/com.android.virt/bin/crosvm','ssh_port':a.ssh_port}
  (state/'config.json').write_text(json.dumps(c,indent=2));print(state/'config.json');return
 c=config.load()
 if a.action=='doctor':
  issues=[]
  if os.geteuid()!=0:issues.append('Real root is required')
  try:host.abi(c)
  except (RuntimeError,OSError) as e:issues.append(str(e))
  for n in ['ip','unshare','nsenter','slirp4netns','e2fsck','ssh']:
   if not shutil.which(n):issues.append('Missing executable: '+n)
  p=config.profile(a.profile)
  needed=['host/gh_hugepage_reserve.ko','host/kapi_check','shims/'+a.profile+'.so','guest/legacyImage' if p['kernel']=='legacy' else 'guest/Image','initrd/'+a.profile+'.cpio']+['host/'+n+'.ko' for n in p['host_modules']]
  for n in needed:
   if not config.artifact(c,n).is_file():issues.append('Missing artifact: '+str(config.artifact(c,n)))
  for path in ['/dev/gunyah','/sys/kernel/btf/vmlinux','/proc/1/root'+c['crosvm'],str(state/'rootfs.img')]:
   if not Path(path).exists():issues.append('Missing '+path)
  # Ensure the host can address exactly the same files after chroot into Android.
  for image in ['rootfs.img']:
   path=state/image;other=Path('/proc/1/root'+host.android_path(c,path))
   if path.exists() and (not other.exists() or path.stat().st_ino!=other.stat().st_ino):issues.append('Android rootfs path does not map the same image inode')
  if os.geteuid()==0 and Path('/proc/1/root'+c['crosvm']).exists():
   try:
    probe=subprocess.run([c['crosvm'],'run','--help'],preexec_fn=host.android_preexec,capture_output=True,text=True,timeout=10)
    if 'gunyah' not in (probe.stdout+probe.stderr).lower() and b'/dev/gunyah' not in Path('/proc/1/root'+c['crosvm']).read_bytes():issues.append('No Gunyah backend evidence in this crosvm build')
   except (OSError,subprocess.SubprocessError) as e:issues.append('Cannot enter Android namespace/run crosvm: '+str(e))
  try:host.headroom(config.memory(p,a.memory_mib))
  except RuntimeError as e:issues.append(str(e))
  report={'profile':a.profile,'ready':not issues,'issues':issues,'running_vms':host.running_vms(),'state':str(state)}
  print(json.dumps(report,indent=2));return 1 if issues else 0
 if a.action=='_supervise':
  from .runtime import supervise
  supervise(a.profile,a.memory_mib);return
 if a.action=='status':
  from .console import current
  print(json.dumps({'vm':current(),'ready':json.loads((state/'ready.json').read_text()) if (state/'ready.json').exists() else None},indent=2));return
 if a.action=='up':
  from .console import current
  if host.running_vms():raise RuntimeError('Another VM is active')
  precmd=[str(config.REPO/'droidvm'),'doctor','--profile',a.profile]
  if a.memory_mib is not None:precmd+=['--memory-mib',str(a.memory_mib)]
  preflight=subprocess.run(precmd)
  if preflight.returncode:raise RuntimeError('Preflight failed; VM was not started')
  for name in ['ready.json','stop.request']:(state/name).unlink(missing_ok=True)
  env=os.environ.copy();env['DROIDVM_STATE']=str(state)
  command=[str(config.REPO/'droidvm'),'_supervise',a.profile]
  if a.memory_mib is not None:command+=['--memory-mib',str(a.memory_mib)]
  p=subprocess.Popen(command,env=env,stdin=subprocess.DEVNULL,stdout=open(state/'supervisor.log','a'),stderr=subprocess.STDOUT,start_new_session=True);(state/'supervisor.pid').write_text(str(p.pid))
  until=time.monotonic()+240
  while time.monotonic()<until:
   if (state/'ready.json').exists():print((state/'ready.json').read_text());return
   if p.poll() is not None:raise RuntimeError('VM did not become ready; read '+str(state/'supervisor.log'))
   time.sleep(.5)
  raise RuntimeError('Startup is still pending; inspect status and supervisor.log. No second launch attempted.')
 if a.action=='down':
  from .console import current
  if not current():print('No owned VM is running');return
  (state/'stop.request').touch();until=time.monotonic()+180
  while time.monotonic()<until:
   if not current() and not (state/'ready.json').exists():print('Guest stopped');return
   time.sleep(.5)
  raise RuntimeError('Shutdown incomplete; no force termination or reboot was attempted')
 if a.action=='exec':
  from .console import rpc
  if not a.command:raise ValueError('Supply a shell command after exec')
  code,out=rpc(' '.join(a.command),300);print(out,end='');return code
 if a.action=='ssh':
  cmd=['ssh','-p',str(c['ssh_port']),'-i',str(state/'id_ed25519'),'-o','UserKnownHostsFile='+str(state/'known_hosts'),'-o','StrictHostKeyChecking=yes','root@127.0.0.1'];return subprocess.call(cmd)

def entry():
 try:raise SystemExit(main() or 0)
 except (RuntimeError,ValueError,OSError,subprocess.SubprocessError) as e:print(str(e),file=sys.stderr);raise SystemExit(1)
