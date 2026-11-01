"""Every discovered static callback is translated or explicitly accounted for."""
from pathlib import Path
import json,re
ROOT=Path(__file__).resolve().parents[1]
records=json.loads((ROOT/'analysis/static_callback_inventory.json').read_text())['records']
decoded={int(a,16) for p in (ROOT/'generated/startup').glob('x86_recomp.*.cpp') for a in re.findall(r'X86_FUNC\(__imp__sub_([0-9A-F]+)\)',p.read_text())}
excluded={int(a,16) for a in json.loads((ROOT/'analysis/static_callback_coverage.json').read_text())['quarantined']}
missing={r['target'] for r in records}-decoded-excluded
assert not missing,[hex(t) for t in sorted(missing)]
assert 0x29FD30 in decoded, 'Latest observed mission-loading handler must be translated'
print(f'PASS: {len(set(r["target"] for r in records))} static callbacks accounted for; observed loading handler included')
