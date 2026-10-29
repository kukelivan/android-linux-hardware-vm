import pathlib,time
params=pathlib.Path('/sys/module/gh_hugepage_reserve/parameters')
def snapshot():
 return dict(line.split('=',1) for line in (params/'refill_stat').read_text().splitlines() if '=' in line)
def wait_idle(required=0,seconds=60):
 deadline=time.monotonic()+seconds;stable=0
 while time.monotonic()<deadline:
  s=snapshot()
  good=(s.get('state')=='idle' and all(s.get(k)=='0' for k in ['served','active_vms','acquire_active']) and int(s.get('pool_avail','0'))>=required)
  stable=stable+1 if good else 0
  if stable>=3:return
  time.sleep(1)
 raise SystemExit('Gunyah memory has not returned to an idle pool; no new VM was launched.')
def reject_blocked():
 for p in pathlib.Path('/proc').iterdir():
  if not p.name.isdigit():continue
  try:
   if (p/'comm').read_text().strip()!='crosvm':continue
   data=(p/'stat').read_text()
   if data[data.rfind(')')+2:].split()[0]=='D':raise SystemExit('An existing crosvm is blocked in the kernel; refusing another VM start.')
  except (FileNotFoundError,ProcessLookupError,PermissionError):pass

def wait_released(seconds=60):
 deadline=time.monotonic()+seconds;stable=0
 while time.monotonic()<deadline:
  s=snapshot()
  good=s.get('state')=='idle' and all(s.get(k)=='0' for k in ['pool_avail','pool_total','served','active_vms','acquire_active'])
  stable=stable+1 if good else 0
  if stable>=3:return
  time.sleep(1)
 raise SystemExit('Reserved memory has not fully released; inspect module counters before restarting.')
