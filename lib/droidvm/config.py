# SPDX-License-Identifier: GPL-2.0-only
import json, os
from pathlib import Path
REPO=Path(__file__).resolve().parents[2]
def home():return Path(os.environ.get('DROIDVM_STATE','/var/lib/android-linux-hardware-vm')).resolve()
def load():
 p=home()/'config.json'
 if not p.exists():raise RuntimeError('Run droidvm configure first; see docs/QUICKSTART.md')
 c=json.loads(p.read_text());c['state']=str(home());return c
def profile(name):
 if name not in ['classic','split','shared','custom-fixed','grow']:raise ValueError('Unknown profile')
 return json.loads((REPO/'profiles'/f'{name}.json').read_text())
def artifact(c,name):return Path(c['artifacts'])/name

def memory(profile, requested=None):
 value=profile['initial_mib'] if requested is None else requested
 name=profile['name']
 valid=(value==1024 if name=='classic' else 1024<=value<=1792 and value%64==0 if name=='split' else value==256 if name=='grow' else value in (3072,4096))
 if not valid:raise ValueError('Unsupported memory size for '+name)
 return value
