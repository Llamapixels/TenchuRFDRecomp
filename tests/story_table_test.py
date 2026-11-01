"""Regression for the indexed Story Mode loading/setup callback tables."""
from pathlib import Path
import json,re,struct
ROOT=Path(__file__).resolve().parents[1]
data=(ROOT/'assets/default.xbe').read_bytes()
sections=json.loads((ROOT/'analysis/xbe.json').read_text())['sections']
s=next(s for s in sections if s['name']=='.data')
def word(va):
    return struct.unpack_from('<I',data,int(s['raw_addr'],16)+va-int(s['virtual_addr'],16))[0]
decoded={int(a,16) for p in (ROOT/'generated/startup').glob('x86_recomp.*.cpp') for a in re.findall(r'X86_FUNC\(__imp__sub_([0-9A-F]+)\)',p.read_text())}
targets=[word(base+i*4) for base in (0x4F0578,0x4F05D8) for i in range(23)]
assert word(0x4F05C0)==0x2BD860, 'Observed mode 18 entry changed'
assert word(0x4F05D4)==0 and word(0x4F0634)==2, 'Table bounds changed'
assert all(t in decoded for t in targets), [hex(t) for t in targets if t not in decoded]
print('PASS: all 46 Story Mode dispatch/setup callbacks translated, including observed loading failure')
