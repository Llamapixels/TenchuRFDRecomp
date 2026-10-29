"""Apply narrowly checked integration edits to a build copy of the MIT SDK."""
from pathlib import Path
import sys
root=Path(__file__).resolve().parents[2]
source=root/'tools/XboxXBERecomp/runtime/xdk/dsound.cpp'
text=source.read_text()
edits={
    '    }\n}\n\nint64_t NowMs() {':'    }\n    tenchu::audio::ApplyMaster(out, size_t(frames) * 2);\n}\n\nint64_t NowMs() {',
    'struct Voice {':'struct Voice {\n    tenchu::RolloffState tenchu_rolloff;',
    'float Attenuation(const Voice& v, float dist) {':'float TenchuNormalAttenuation(const Voice& v, float dist) {',
    'Place Locate(const Voice& v) {':'''float Attenuation(const Voice& v, float dist) {
    return v.tenchu_rolloff.Attenuate(TenchuNormalAttenuation(v, dist));
}

Place Locate(const Voice& v) {''',
    'HLE_STDCALL(IDirectSound_CommitDeferredSettings, 1) { return 0; }':'''HLE_STDCALL(IDirectSound_CommitDeferredSettings, 1) {
    std::lock_guard<std::recursive_mutex> lock(g_lock);
    for (auto& [object, voice] : g_voices) voice.tenchu_rolloff.Commit();
    return 0;
}''',
}
for old,new in edits.items():
    assert text.count(old)==1,f'Upstream source changed: {old}'
    text=text.replace(old,new)
text='#include "audio/rolloff_state.h"\n#include "audio/master_volume.h"\n'+text
text+='''
// Project bridge into the SDK's private voice/mixer state.
uint32_t TenchuSetStreamRolloff(uint32_t object, float value, uint32_t apply) {
    std::lock_guard<std::recursive_mutex> lock(g_lock);
    Voice* voice = Find(object);
    if (!voice || !voice->stream) return 0x80070057u;
    return voice->tenchu_rolloff.Set(value, apply);
}
'''
out=Path(sys.argv[1])
out.parent.mkdir(parents=True,exist_ok=True)
if not out.exists() or out.read_text()!=text: out.write_text(text)
