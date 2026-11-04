"""Seed every static voice component registered during native session startup."""
from pathlib import Path
import hashlib
import json
import struct
import tomllib

ROOT = Path(__file__).resolve().parents[2]
# Registration pointer, static object, vtable, exclusive table end. The first
# two are registered by2A700 after316C34 registers the third internally.
TABLES = (
    (0x0040DAA8, 0x004FE8F0, 0x0040DAB0, 0x0040DB1C),
    (0x0040DB20, 0x004FE9F8, 0x0040DB28, 0x0040DB94),
    (0x0040DB94, 0x004FEA10, 0x0040DB98, 0x0040DC04),
)

def inventory():
    image = (ROOT / 'assets/default.xbe').read_bytes()
    assert hashlib.sha256(image).hexdigest() == '0d8203f74d9476dd93637bfbf3d75855b29daf576b752e77600d4ff8460a9d34'
    sections = json.loads((ROOT / 'analysis/xbe.json').read_text())['sections']
    def word(at):
        section = next(s for s in sections if int(s['virtual_addr'],16) <= at < int(s['virtual_addr'],16)+s['raw_size'])
        return struct.unpack_from('<I',image,int(section['raw_addr'],16)+at-int(section['virtual_addr'],16))[0]
    records = []
    for registration, obj, table, end in TABLES:
        assert word(registration) == obj and word(obj) == table
        assert end-table == 27*4
        records.extend({'registration':hex(registration),'object':hex(obj),
                        'table':hex(table),'slot':hex(slot),'target':word(slot)}
                       for slot in range(table,end,4))
    assert len(records) == 81
    assert next(r['target'] for r in records if r['slot']=='0x40dbc0') == 0x00316D78
    assert next(r['target'] for r in records if r['slot']=='0x40db28') == 0x00315EDF
    assert all(0x00314000 <= r['target'] < 0x00317000 for r in records)
    # Boundaries are a float constant, a new registration pointer and an
    # unrelated kernel-thunk array, respectively. None is another voice slot.
    assert word(0x0040DB1C) == 0x39909091
    assert word(0x0040DC04) == 0x003FFFBE
    return records

if __name__ == '__main__':
    records = inventory()
    path = ROOT / 'config/startup.toml'
    text = path.read_text(encoding='utf-8-sig')
    known = {int(k,0) for k in tomllib.loads(text)['functions']}
    added = sorted({r['target'] for r in records}-known)
    lines = '\n'.join(f'"0x{target:08X}" = "" # native session voice components; verified static vtables' for target in added)
    if added:
        path.write_text(text.replace('[functions]','[functions]\n'+lines))
    (ROOT / 'analysis/online_voice_vtable_inventory.json').write_text(json.dumps({
        'observed_targets':['00316D78','00315EDF'],
        'tables':[{'registration':hex(r),'object':hex(o),'table':hex(t),'end':hex(e)}
                  for r,o,t,e in TABLES], 'entries':records,
    },indent=2))
    print(f'{len(records)} verified voice vtable slots, {len(set(r["target"] for r in records))} unique methods; {len(added)} added roots.')
