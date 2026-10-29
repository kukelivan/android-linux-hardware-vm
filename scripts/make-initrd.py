#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
from pathlib import Path
import argparse,stat,time,os
R=Path(__file__).resolve().parent.parent
ap=argparse.ArgumentParser();ap.add_argument('--busybox',default='/bin/busybox');a=ap.parse_args()
base='''#!/bin/busybox sh
BB=/bin/busybox
$BB mount -t proc proc /proc
$BB mount -t sysfs sysfs /sys
$BB mount -t devtmpfs devtmpfs /dev
$BB mkdir -p /newroot
for i in $($BB seq 1 100); do
 [ -b /dev/vda ] && break
 $BB mdev -s
 $BB sleep .1
done
$BB ip link set lo up
$BB ip link set eth0 up
$BB ip addr add 10.0.2.100/24 dev eth0
$BB ip route add default via 10.0.2.2
$BB mount -t ext4 -o rw /dev/vda /newroot || $BB poweroff -f
$BB mkdir -p /newroot/workspace
if [ -b /dev/vdb ]; then $BB mount -t ext4 -o rw /dev/vdb /newroot/workspace || $BB poweroff -f; fi
'''
grow='''$BB insmod /bin/gunyah_grow_guest.ko || $BB poweroff -f
$BB mount -t tmpfs tmpfs /newroot/run
$BB mkdir -p /newroot/run/systemd/system/multi-user.target.wants
$BB cp /bin/agent /newroot/run/grow-agent
$BB chmod 755 /newroot/run/grow-agent
$BB cat > /newroot/run/systemd/system/grow-agent.service <<'UNIT'
[Unit]
Description=Hardware VM memory telemetry and hot-add
After=systemd-udevd.service
[Service]
ExecStart=/run/grow-agent
Restart=on-failure
RestartSec=1
UNIT
$BB ln -s ../grow-agent.service /newroot/run/systemd/system/multi-user.target.wants/grow-agent.service
'''
tail='''$BB mount -t tmpfs tmpfs /newroot/run
'''
mask='''$BB mkdir -p /newroot/run/systemd/system
for u in docker.service docker.socket containerd.service; do $BB ln -s /dev/null /newroot/run/systemd/system/$u; done
$BB mount --move /dev /newroot/dev
$BB mount --move /proc /newroot/proc
$BB mount --move /sys /newroot/sys
exec $BB switch_root /newroot /sbin/init
'''
(R/'build/initrd').mkdir(parents=True,exist_ok=True)
for profile in ['classic','split','shared','custom-fixed','grow']:
 items=[(n,stat.S_IFDIR|0o755,b'',0,0) for n in ['bin','dev','proc','sys']]+[('dev/console',stat.S_IFCHR|0o600,b'',5,1),('bin/busybox',stat.S_IFREG|0o755,Path(a.busybox).read_bytes(),0,0)]
 if profile=='grow':items += [('bin/agent',stat.S_IFREG|0o755,(R/'build/guest/agent').read_bytes(),0,0),('bin/gunyah_grow_guest.ko',stat.S_IFREG|0o600,(R/'build/guest/gunyah_grow_guest.ko').read_bytes(),0,0)]
 init=base+(grow if profile=='grow' else tail)+mask
 items += [('init',stat.S_IFREG|0o755,init.encode(),0,0),('TRAILER!!!',0,b'',0,0)]
 out=bytearray()
 for ino,(name,mode,data,major,minor) in enumerate(items,1):
  nb=name.encode()+b'\0';v=[ino,mode,0,0,1,int(time.time()),len(data),0,0,major,minor,len(nb),0];out+=b'070701'+b''.join(f'{x:08x}'.encode() for x in v)+nb;out+=b'\0'*(-len(out)%4);out+=data;out+=b'\0'*(-len(out)%4)
 (R/'build/initrd'/f'{profile}.cpio').write_bytes(out)
print('Built five initramfs profiles')
