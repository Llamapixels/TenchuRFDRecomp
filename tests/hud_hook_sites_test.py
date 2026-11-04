"""Check real generated sprite-builder hooks, not callback-only stand-ins."""
from pathlib import Path
import re,tomllib
root=Path(__file__).resolve().parents[1]
cfg=tomllib.loads((root/'config/startup.toml').read_text())
sites={0x11560:(0x119B7,0x119BD),0x119D0:(0x11C94,0x11C9A),
       0x11CA0:(0x121EC,0x121F2),0x12200:(0x12553,0x12559),0x12560:(0x127B6,0x127BC)}
hooks=[h for h in cfg['hook'] if h['call']=='TenchuHudSpriteEnqueue']
assert {h['at'] for h in hooks}=={site for site,end in sites.values()}
assert all(not h.get('after',False) for h in hooks)
source=(root/'generated/startup/x86_recomp.0.cpp').read_text()
for fn,(site,end) in sites.items():
    body=re.search(r'X86_FUNC\(__imp__sub_'+f'{fn:08X}'+r'\) \{(.*?)\n\}',source,re.S)[1]
    call=body.index('TenchuHudSpriteEnqueue(ctx, base)')
    store=body.index(f'// {site:08X} mov ')
    exit=body.index(f'loc_{end:08X}:')
    assert body.count('TenchuHudSpriteEnqueue(ctx, base)')==1
    assert call<store<exit
    assert f'goto loc_{end:08X};' in body[:call], 'full queue must skip alpha hook'
    assert '0x24004u)' in body[store:exit], 'original count store must still execute'
print('PASS: all five actual native sprite builders adjust alpha once before count store; capacity exits skip hook')
