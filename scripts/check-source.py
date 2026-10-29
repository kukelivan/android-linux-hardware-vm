#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Check the public source tree before publication or packaging."""
from pathlib import Path
import ast,json,re,sys
R=Path(__file__).resolve().parent.parent
issues=[]
for p in R.rglob('*'):
 if any(x in p.parts for x in ['.git','build','state','__pycache__']):continue
 if not p.is_file():continue
 if p.suffix in ['.img','.ko','.so','.pem','.key','.qcow2']:issues.append('Generated/private binary present: '+str(p))
 try:s=p.read_text()
 except UnicodeDecodeError:issues.append('Unexpected binary: '+str(p));continue
 for pattern in [r'-----BEGIN [A-Z ]*PRIVATE KEY-----',r'gh[pousr]_[A-Za-z0-9]{20,}',r'(?i)gitlab\.[a-z0-9.-]+',r'/root/docker-vm',r'/root/gunyah-module']:
  if re.search(pattern,s) and p.name!='check-source.py':issues.append('Private/local marker in '+str(p))
 if p.suffix=='.py':
  try:ast.parse(s,filename=str(p))
  except SyntaxError as e:issues.append(str(e))
 if p.suffix=='.json':
  try:json.loads(s)
  except ValueError as e:issues.append(str(p)+': '+str(e))
for p in (R/'modules').rglob('*'):
 if not p.is_file() or not any(str(p).endswith(ext) for ext in ['.c','.h','.inc']):continue
 for inc in re.findall(r'^\s*#include\s+"([^"]+)"',p.read_text(),re.M):
  if not (p.parent/inc).exists():issues.append('Missing module include: '+str(p)+': '+inc)
print('\n'.join(issues) if issues else 'Public source checks passed')
raise SystemExit(bool(issues))
