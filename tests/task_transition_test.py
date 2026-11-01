"""Regression: Story Mode changes a task handler with a store, not a call."""
from pathlib import Path
import json,re,sys
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools/scripts'))
from seed_menu_callbacks import transition_inventory
data=bytearray(64);data[:3]=b'\x55\x8b\xec'
sections=[{'name':'.text','virtual_addr':'0x1000','raw_addr':'0x0','raw_size':64}]
body='''// 00003000 mov dword ptr [ecx], 0x1000
// 00003008 mov dword ptr [eax+0x04], 0x1000
// 00003010 mov dword ptr [ebp-0x04], 0x1000
// 00003018 mov dword ptr [ecx], 0x1020
'''
records=transition_inventory({0x3000:body},data,sections)
assert [r['source'] for r in records]==['00003000','00003008']
records=json.loads((ROOT/'analysis/task_transition_inventory.json').read_text())['records']
assert any(r['source']=='002A6F85' and r['target']==0x2A8750 for r in records)
decoded={int(a,16) for p in (ROOT/'generated/startup').glob('x86_recomp.*.cpp') for a in re.findall(r'X86_FUNC\(__imp__sub_([0-9A-F]+)\)',p.read_text())}
assert 0x2A8750 in decoded, 'Observed Story Mode transition is missing'
coverage=json.loads((ROOT/'analysis/task_transition_coverage.json').read_text())
excluded={int(a,16) for a in coverage['quarantined']}
assert all(r['target'] in decoded or r['target'] in excluded for r in records)
print('PASS: task handler stores, Story Mode callback, and inventory accounting')
