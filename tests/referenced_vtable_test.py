"""Regression for the post-character-selection virtual-method failure."""
from pathlib import Path
import json,re,struct
ROOT=Path(__file__).resolve().parents[1]
image=(ROOT/'assets/default.xbe').read_bytes()
section=next(s for s in json.loads((ROOT/'analysis/xbe.json').read_text())['sections'] if s['name']=='.rdata')
decoded={int(a,16) for p in (ROOT/'generated/startup').glob('x86_recomp.*.cpp') for a in re.findall(r'X86_FUNC\(__imp__sub_([0-9A-F]+)\)',p.read_text())}
records=json.loads((ROOT/'analysis/referenced_vtable_inventory.json').read_text())['records']
assert any(r['constructor_site']=='00056B92' and r['table']=='0x40675c' and r['target']==0x56BA0 for r in records)
targets=[struct.unpack_from('<I',image,int(section['raw_addr'],16)+slot-int(section['virtual_addr'],16))[0] for slot in range(0x40675C,0x406770,4)]
assert targets[3]==0x56BA0 and all(t in decoded for t in targets)
excluded={int(t,16) for t in json.loads((ROOT/'analysis/referenced_vtable_coverage.json').read_text())['quarantined']}
assert all(r['target'] in decoded or r['target'] in excluded for r in records)
print('PASS: character-confirmation vtable methods and referenced-table inventory accounting')
