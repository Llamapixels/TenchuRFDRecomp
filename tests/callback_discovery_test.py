"""Regression: callback B found from A must expose callback C in a later pass."""
from pathlib import Path
import struct,subprocess,json
root=Path(__file__).resolve().parents[1]
folder=root/'build/callback_fixture'; folder.mkdir(parents=True,exist_ok=True)
data=bytearray(0x260);data[:4]=b'XBEH'
def w(offset,value):struct.pack_into('<I',data,offset,value)
for offset,value in {0x104:0x10000,0x108:0x200,0x10C:0x2000,0x118:0x101E0,0x11C:1,0x120:0x10180,0x128:0x11000^0xA8FC57AB,0x130:0x10000,0x158:0x101D0^0x5B6D40B6}.items():w(offset,value)
for offset,value in {0:6,4:0x11000,8:0x60,12:0x200,16:0x60,20:0x101C0}.items():w(0x180+offset,value)
data[0x1C0:0x1C6]=b'.text\0';data[0x200:]=b'\xCC'*0x60
for offset,target in [(0x200,0x11020),(0x220,0x11040)]:data[offset:offset+10]=b'\x55\x8B\xEC\xB8'+struct.pack('<I',target)+b'\x5D\xC3'
data[0x240:0x24A]=b'\x55\x8B\xEC\xB8'+struct.pack('<I',0x11050)+b'\x5D\xC3'
data[0x250]=0xC3 # deliberately unframed: not adopted by conservative later passes
xbe=folder/'fixture.xbe';xbe.write_bytes(data)
config=folder/'fixture.toml'
config.write_text('[xbe]\npath="fixture.xbe"\nlibrary="Fixture.dll"\npartitions=2\ncode_sections=[".text"]\ngap_fill=false\npointer_seeds=false\n')
results={}
for name,tool in [('baseline',root/'build/Release/xbe_recomp.exe'),('fixedpoint',root/'build/native/xbe_recomp_fixedpoint.exe')]:
    generated=folder/name
    with (folder/(name+'.log')).open('w') as f:
        subprocess.run([str(tool),str(config),str(xbe),str(generated)],cwd=root,stdout=f,stderr=subprocess.STDOUT,check=True)
    mapping=(generated/'x86_func_mapping.cpp').read_text()
    results[name]={'callback_b_found':'sub_00011020' in mapping,'nested_callback_c_found':'sub_00011040' in mapping,'unframed_d_found':'sub_00011050' in mapping}
assert results['baseline']=={'callback_b_found':True,'nested_callback_c_found':False,'unframed_d_found':False},results
assert results['fixedpoint']=={'callback_b_found':True,'nested_callback_c_found':True,'unframed_d_found':False},results
(root/'analysis/callback_discovery_regression.json').write_text(json.dumps(results,indent=2))
print('PASS: nested framed callback found; unframed candidate stays explicit')
