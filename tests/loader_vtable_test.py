"""Regression for Story Mode loader's virtual call at 0005478C."""
from pathlib import Path
import json,re,struct
ROOT=Path(__file__).resolve().parents[1]
data=(ROOT/'assets/default.xbe').read_bytes()
section=next(s for s in json.loads((ROOT/'analysis/xbe.json').read_text())['sections'] if s['name']=='.rdata')
records=json.loads((ROOT/'analysis/loader_vtable_inventory.json').read_text())['records']
decoded={int(a,16) for p in (ROOT/'generated/startup').glob('x86_recomp.*.cpp') for a in re.findall(r'X86_FUNC\(__imp__sub_([0-9A-F]+)\)',p.read_text())}
assert any(int(r['source'],16)==0x403FB4 and r['target']==0x54C60 for r in records)
assert len(records)==15 and len({r['target'] for r in records})==12
for record in records:
    slot=int(record['source'],16)
    actual=struct.unpack_from('<I',data,int(section['raw_addr'],16)+slot-int(section['virtual_addr'],16))[0]
    assert actual==record['target'] and actual in decoded,record
print('PASS: all 15 loader vtable slots (12 unique methods) translated, including observed Story Mode method')
