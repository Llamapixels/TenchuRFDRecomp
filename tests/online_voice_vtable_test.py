"""Every voice component registered after joining must have translated methods."""
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT / 'tools/scripts'))
from seed_online_voice_vtable import inventory

records = inventory()
assert len(records)==81 and len({r['table'] for r in records})==3
decoded = {int(a,16) for p in (ROOT / 'generated/startup').glob('x86_recomp.*.cpp')
           for a in re.findall(r'X86_FUNC\(__imp__sub_([0-9A-F]+)\)',p.read_text())}
missing = sorted({r['target'] for r in records}-decoded)
assert not missing, 'Missing native voice methods: '+', '.join(f'{a:08X}' for a in missing)
print(f'PASS: all {len(records)} slots in three session voice components translated, including both observed crash targets.')
