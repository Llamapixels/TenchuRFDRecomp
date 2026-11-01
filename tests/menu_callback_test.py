"""Regression for table-driven menu callbacks missed by literal PUSH scanning."""
from pathlib import Path
import json,re,struct,sys
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools/scripts'))
from seed_menu_callbacks import inventory

data=bytearray(128)
# Synthetic table holds a valid framed target, an interior/unaligned pointer,
# and an aligned target with no function prologue.
struct.pack_into('<III',data,64,0x1000,0x1001,0x1020)
data[:3]=b'\x55\x8b\xec'
sections=[{'name':'.text','virtual_addr':'0x1000','raw_addr':'0x0','raw_size':64},
          {'name':'.data','virtual_addr':'0x2000','raw_addr':'0x40','raw_size':64}]
records=inventory(data,sections,['// 00003000 mov dword ptr [eax], 0x1000\n// 00003008 add eax, 0x1000'])
assert records==[{'kind':'data_pointer','source':'0x2000','target':0x1000},
                 {'kind':'code_pointer','source':'00003000','target':0x1000}]

# The game's two menu variants use 12 descriptors, stride 0x20. Their
# first word is installed into task->callback by 00249CA0 at 0024A2EC.
image=(ROOT/'assets/default.xbe').read_bytes()
meta=json.loads((ROOT/'analysis/xbe.json').read_text())['sections']
section=next(s for s in meta if s['name']=='.data')
def read32(va):
    return struct.unpack_from('<I',image,int(section['raw_addr'],16)+va-int(section['virtual_addr'],16))[0]
decoded={int(a,16) for p in (ROOT/'generated/startup').glob('x86_recomp.*.cpp') for a in re.findall(r'X86_FUNC\(__imp__sub_([0-9A-F]+)\)',p.read_text())}
entries=[{'slot':hex(base+i*0x20),'target':hex(read32(base+i*0x20))} for base in (0x4CAA80,0x4CAC00) for i in range(12)]
missing=[e for e in entries if int(e['target'],16) not in decoded]
(ROOT/'analysis/menu_table_coverage.json').write_text(json.dumps({'entries':entries,'missing':missing},indent=2))
assert not missing,missing
print(f'PASS: pointer provenance filters and all {len(entries)} menu descriptor callbacks translated')
