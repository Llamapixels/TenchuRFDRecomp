"""Do not decode adjacent byte classifications as extra jump targets."""
from pathlib import Path
import struct,subprocess,json
ROOT=Path(__file__).resolve().parents[1]
folder=ROOT/'build/switch_boundary_fixture';folder.mkdir(parents=True,exist_ok=True)
size=0x11000
data=bytearray(0x200+size);data[:4]=b'XBEH'
def w(o,v):struct.pack_into('<I',data,o,v)
for o,v in {0x104:0x10000,0x108:0x200,0x10C:0x22000,0x118:0x101E0,0x11C:1,0x120:0x10180,0x128:0x11000^0xA8FC57AB,0x130:0x10000,0x158:0x101D0^0x5B6D40B6}.items():w(o,v)
for o,v in {0:6,4:0x11000,8:size,12:0x200,16:size,20:0x101C0}.items():w(0x180+o,v)
data[0x1C0:0x1C6]=b'.text\0';data[0x200:]=b'\xCC'*size
# No register-CMP bound: the table boundary must come from MOVZX's byte table.
code=b'\xB8\x00\x00\x00\x00\x0F\xB6\x88'+struct.pack('<I',0x1104C)+b'\xFF\x24\x8D'+struct.pack('<I',0x11040)
data[0x200:0x200+len(code)]=code
for va in (0x11030,0x11031,0x11032,0x20000):data[0x200+va-0x11000]=0xC3
for i,va in enumerate((0x11030,0x11031,0x11032)):w(0x240+i*4,va)
data[0x24C:0x250]=b'\x00\x00\x02\x00' # looks like code address 00020000
xbe=folder/'fixture.xbe';xbe.write_bytes(data)
cfg=folder/'fixture.toml';cfg.write_text('[xbe]\nlibrary="Fixture.dll"\npartitions=1\ncode_sections=[".text"]\ngap_fill=false\npointer_seeds=false\n')
results={}
for name,tool in [('baseline',ROOT/'build/Release/xbe_recomp.exe'),('patched',ROOT/'build/native/xbe_recomp_diagnostics.exe')]:
    out=folder/name
    result=subprocess.run([str(tool),str(cfg),str(xbe),str(out)],cwd=ROOT,capture_output=True,text=True,check=True)
    source='\n'.join(p.read_text() for p in out.glob('x86_recomp.*.cpp'))
    results[name]={'bogus_case':'case 0x20000u:' in source,'valid_cases':all(f'case 0x{t:X}u:' in source for t in (0x11030,0x11031,0x11032))}
assert results['baseline']=={'bogus_case':True,'valid_cases':True},results
assert results['patched']=={'bogus_case':False,'valid_cases':True},results
(ROOT/'analysis/switch_boundary_regression.json').write_text(json.dumps(results,indent=2))
print('PASS: baseline overreads byte data as code; patched compiler keeps all real targets and excludes fake target')
