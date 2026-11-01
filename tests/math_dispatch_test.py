"""Regression for the CRT's classified binary floating-point dispatch."""
from pathlib import Path
import json,re
ROOT=Path(__file__).resolve().parents[1]
records=json.loads((ROOT/'analysis/math_dispatch_inventory.json').read_text())['records']
decoded={int(a,16) for p in (ROOT/'generated/startup').glob('x86_recomp.*.cpp') for a in re.findall(r'X86_FUNC\(__imp__sub_([0-9A-F]+)\)',p.read_text())}
groups={r['descriptor'] for r in records}
assert '0x4fe108' in groups
for group in groups:
    slots={int(r['source'],16) for r in records if r['descriptor']==group}
    assert slots=={int(group,16)+0x10+i*4 for i in range(16)}
assert any(r['target']==0x312A2C for r in records)
assert all(r['target'] in decoded for r in records)
print(f'PASS: all 16 floating-point classifications covered for {len(groups)} binary math descriptor(s)')
