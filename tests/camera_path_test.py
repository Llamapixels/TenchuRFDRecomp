"""Keep camera overrides across rendering and restore before native simulation."""
from pathlib import Path
import re,tomllib
root=Path(__file__).resolve().parents[1]
cfg=tomllib.loads((root/'config/startup.toml').read_text())
hooks={h['call']:h['at'] for h in cfg['hook'] if h['call'].startswith('TenchuCamera')}
assert hooks=={'TenchuCameraBegin':0x2A1929,'TenchuCameraEnd':0x2A1400,
              'TenchuCameraRenderBegin':0x235B80,'TenchuCameraRenderEnd':0x236BAC},hooks
body=None
for path in (root/'generated/startup').glob('x86_recomp.*.cpp'):
    m=re.search(r'X86_FUNC\(__imp__sub_002A1400\) \{(.*?)\n\}',path.read_text(),re.S)
    if m:body=m[1];break
assert body,'player camera task missing'
restore=body.index('TenchuCameraEnd(ctx, base)')
simulate=body.index('sub_0028E980(ctx, base)')
apply=body.index('TenchuCameraBegin(ctx, base)')
assert restore<simulate<apply
assert '// 002A1929 mov esp, ebp' in body[apply:]
print('PASS: restore before native camera simulation; apply at common exit; retain pose across subsequent rendering')
render=None
for path in (root/'generated/startup').glob('x86_recomp.*.cpp'):
    m=re.search(r'X86_FUNC\(__imp__sub_00235B80\) \{(.*?)\n\}',path.read_text(),re.S)
    if m:render=m[1];break
assert render,'native scene renderer missing'
begin=render.index('TenchuCameraRenderBegin(ctx, base)')
end=render.index('TenchuCameraRenderEnd(ctx, base)')
assert begin<render.index('// 00235B80 push ebx')
assert begin<render.index('sub_00237ED0(ctx, base)')
assert end>render.rindex('sub_000139C0(ctx, base)')
assert '// 00236BAC ret' in render[end:]
print('PASS: online guest camera spans the complete renderer, after native playback and before all render consumers')
