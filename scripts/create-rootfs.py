#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Create a new, generic Debian ARM64 image; never import an existing personal disk."""
from pathlib import Path
import os,subprocess,sys,json,argparse
R=Path(__file__).resolve().parent.parent;sys.path.insert(0,str(R/'lib'))
from droidvm import config,host
from droidvm.console import LifecycleLock

def main():
 ap=argparse.ArgumentParser();ap.add_argument('--resume',action='store_true');args=ap.parse_args()
 c=config.load();state=Path(c['state']);image=state/'rootfs.img';tree=state/'rootfs-tree'
 if os.geteuid()!=0:raise RuntimeError('Root is required')
 with LifecycleLock():
  if host.running_vms():raise RuntimeError('Stop all guests before image creation')
  if image.exists():raise RuntimeError('The image already exists; choose a fresh DROIDVM_STATE')
  if tree.exists() and (not args.resume or not (tree/'etc/debian_version').exists()):raise RuntimeError('A partial build tree exists; use --resume only after Debian bootstrap completed')
  env=os.environ.copy();env.update(DEBIAN_FRONTEND='noninteractive',RUNLEVEL='1')
  policy=tree/'usr/sbin/policy-rc.d'
  if not tree.exists():
   tree.mkdir(parents=True);(tree/'usr/sbin').mkdir(parents=True);policy.write_text('#!/bin/sh\nexit 101\n');policy.chmod(0o755)
   subprocess.run(['debootstrap','--arch=arm64','--variant=minbase','--include=systemd-sysv,udev,openssh-server,docker.io,docker-cli,iproute2,ca-certificates,curl','trixie',str(tree),'https://deb.debian.org/debian'],env=env,check=True)
  def write(name,text,mode=0o644):
   p=tree/name;p.parent.mkdir(parents=True,exist_ok=True);p.write_text(text);p.chmod(mode)
  write('etc/hostname','linux-guest\n');write('etc/hosts','127.0.0.1 localhost\n127.0.1.1 linux-guest\n::1 localhost ip6-localhost\n');write('etc/fstab','/dev/vda / ext4 defaults,noatime 0 1\n');write('etc/resolv.conf','nameserver 10.0.2.3\n')
  write('etc/systemd/network/20-virtio.network','[Match]\nName=eth0\n[Network]\nAddress=10.0.2.100/24\nGateway=10.0.2.2\nDNS=10.0.2.3\n[Link]\nMTUBytes=1500\n')
  write('etc/systemd/system/serial-getty@hvc0.service.d/autologin.conf','[Service]\nExecStart=\nExecStart=-/sbin/agetty --autologin root --noclear %I 115200,38400,9600 $TERM\n')
  write('etc/ssh/sshd_config.d/50-hardware-vm.conf','PermitRootLogin prohibit-password\nPasswordAuthentication no\nKbdInteractiveAuthentication no\nPubkeyAuthentication yes\n')
  key=state/'id_ed25519'
  if not key.exists():subprocess.run(['ssh-keygen','-q','-t','ed25519','-N','','-f',str(key)],check=True)
  write('root/.ssh/authorized_keys',key.with_suffix('.pub').read_text(),0o600);(tree/'root/.ssh').chmod(0o700)
  # Make root usable on the private serial console; SSH still requires a key.
  shadow=tree/'etc/shadow';lines=shadow.read_text().splitlines()
  for i,line in enumerate(lines):
   if line.startswith('root:'):
    fields=line.split(':');fields[1]='';lines[i]=':'.join(fields)
  shadow.write_text('\n'.join(lines)+'\n');shadow.chmod(0o640)
  # Configure services without running guest binaries on a nodev Android /data.
  wants=tree/'etc/systemd/system/multi-user.target.wants';wants.mkdir(parents=True,exist_ok=True)
  for unit in ['systemd-networkd.service','ssh.service','docker.service']:
   link=wants/unit;link.unlink(missing_ok=True);link.symlink_to('/usr/lib/systemd/system/'+unit)
  subprocess.run(['ssh-keygen','-A','-f',str(tree)],check=True)
  pub=(tree/'etc/ssh/ssh_host_ed25519_key.pub').read_text().split();(state/'known_hosts').write_text('[127.0.0.1]:'+str(c['ssh_port'])+' '+pub[0]+' '+pub[1]+'\n');(state/'known_hosts').chmod(0o600)
  write('etc/machine-id','');policy.unlink(missing_ok=True);
  with image.open('wb') as f:f.truncate(8<<30)
  image.chmod(0o600);subprocess.run(['mke2fs','-q','-t','ext4','-F','-L','linux-guest','-d',str(tree),str(image)],check=True)
  workspace=state/'workspace.img'
  if not workspace.exists():
   with workspace.open('wb') as f:f.truncate(12<<30)
   workspace.chmod(0o600);subprocess.run(['mke2fs','-q','-t','ext4','-F','-L','workspace',str(workspace)],check=True)
  os.sync();print(image)
if __name__=='__main__':
 try:main()
 except (RuntimeError,OSError,subprocess.SubprocessError) as e:raise SystemExit(str(e))
