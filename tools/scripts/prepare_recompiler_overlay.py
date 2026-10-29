"""Experimental fixed-point callback discovery; original SDK stays unchanged."""
from pathlib import Path
import sys
root=Path(__file__).resolve().parents[2]
text=(root/'tools/XboxXBERecomp/tools/recomp/analysis.cpp').read_text()
anchor='                    if (!bound && byte_table) {'
assert text.count(anchor)==1
text=text.replace(anchor,'''                    // MSVC often stores byte case classifications immediately
                    // after its dword target table. ReadTable can mistake the
                    // first four classification bytes (e.g. 00 00 02 00) for
                    // an Xbox code pointer 00020000. That byte table is data,
                    // even when no register CMP bound was recovered.
                    if (byte_table > jt.table && (byte_table - jt.table) % 4 == 0) {
                        size_t slots = (byte_table - jt.table) / 4;
                        if (slots && slots < jt.targets.size()) {
                            jt.targets.resize(slots);
                            jt.slots.resize(slots);
                        }
                    }
'''+anchor)
if '--diagnostic' in sys.argv:
    old='            if (insns_.count(va + i)) overlaps++;'
    assert text.count(old)==1
    text=text.replace(old,'''            if (insns_.count(va + i)) {
                overlaps++;
                std::fprintf(stderr, "decode overlap: %08X + %u lands on %08X\\n", va, i, va + i);
            }''')
    out=Path(sys.argv[1]);out.parent.mkdir(parents=True,exist_ok=True)
    if not out.exists() or out.read_text()!=text:out.write_text(text)
    raise SystemExit(0)
start=text.index('    size_t imm = 0;\n')
end=text.index('    EhContinuations();',start)
body=text[start:end].replace('    size_t imm = 0;\n','',1)
old='                if (IsCode(v) && !functions.count(v) && PlausibleEntry(v)) extra.push_back(v);'
assert body.count(old)==1
body=body.replace(old,'''                if (IsCode(v) && !functions.count(v) && PlausibleEntry(v)) {
                    uint32_t available = 0;
                    const uint8_t* p = Bytes(v, available);
                    bool framed = p && available >= 3 && p[0] == 0x55 &&
                                  ((p[1] == 0x8B && p[2] == 0xEC) || (p[1] == 0x89 && p[2] == 0xE5));
                    // Require a real frame prologue: integer sizes
                    // and flags can otherwise look like low Xbox code addresses.
                    if (framed) extra.push_back(v);
                }''')
text=text[:start]+'''    size_t imm = 0, callback_rounds = 0;
    for (;;) {
        size_t before = functions.size();
'''+body+'''
        if (functions.size() == before) break;
        if (++callback_rounds >= 32) throw std::runtime_error("callback discovery did not converge in 32 rounds");
    }
    std::fprintf(stderr, "callback fixed-point rounds: %zu\\n", callback_rounds);
'''+text[end:]
text='#include <stdexcept>\n'+text
old='            if (insns_.count(va + i)) overlaps++;'
assert text.count(old)==1
text=text.replace(old,'''            if (insns_.count(va + i)) {
                overlaps++;
                std::fprintf(stderr, "decode overlap: %08X + %u lands on %08X\\n", va, i, va + i);
            }''')
out=Path(sys.argv[1]);out.parent.mkdir(parents=True,exist_ok=True)
if not out.exists() or out.read_text()!=text:out.write_text(text)
