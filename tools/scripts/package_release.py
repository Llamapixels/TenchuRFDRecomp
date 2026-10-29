"""Assemble a clean tester release from build/native, like the SDK's release.bat.

No game code and nothing personal goes in: the first start builds TenchuRFD.dll on
the player's PC from their own disc image with the bundled compiler folder.
Usage: python tools/scripts/package_release.py [output folder]   (default: dist)
"""
from pathlib import Path
import re, shutil, subprocess, sys, zipfile

root = Path(__file__).resolve().parents[2]
native = root / 'build/native'
version = re.search(r'#define TENCHU_VERSION "([^"]+)"', (root / 'src/common/game_info.cpp').read_text()).group(1)
name = f'TenchuRFD-{version}-alpha-windows'
dist = Path(sys.argv[1]) if len(sys.argv) > 1 else root / 'dist'
out = dist / name

def vc_runtime():
    vswhere = Path(r'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe')
    vs = subprocess.run([str(vswhere), '-latest', '-products', '*', '-property', 'installationPath'],
                        capture_output=True, text=True, check=True).stdout.strip()
    found = sorted(Path(vs, 'VC/Redist/MSVC').glob('1*/x64/Microsoft.VC*.CRT'))
    assert found, 'Visual C++ runtime not found: install Visual Studio with the C++ workload'
    return found[-1]

def copy(src, dst):
    dst.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(src, dst)

assert (native / 'TenchuRFD.exe').exists() and (native / 'tools/RFD Switchblade.exe').exists(), 'build first: tools\\scripts\\build_native.cmd'
if out.exists():
    shutil.rmtree(out)
out.mkdir(parents=True)

# Only what players need: no project notes and no debug symbols. The symbols stay beside the
# zips in dist/symbols, matched to the version, to read crash dumps testers send.
copy(native / 'TenchuRFD.exe', out / 'TenchuRFD.exe')
copy(native / 'TenchuRFD.pdb', dist / 'symbols' / f'TenchuRFD-{version}.pdb')
for f in (native / 'renderers/vulkan').iterdir():
    copy(f, out / 'renderers/vulkan' / f.name)

# the compiler folder, without the work folder (game code) or files from the old exe name
compiler = native / 'compiler'
for f in ('clang.exe', 'lld-link.exe', 'xbe_recomp.exe', 'LLVM-LICENSE.txt', 'TenchuRFD.lib', 'config/startup.toml'):
    copy(compiler / f, out / 'compiler' / f)
for folder in ('include', 'xdk'):
    shutil.copytree(compiler / folder, out / 'compiler' / folder)

crt = vc_runtime()
for dll in ('msvcp140.dll', 'vcruntime140.dll', 'vcruntime140_1.dll'):
    copy(crt / dll, out / dll)
for dll in ('msvcp140.dll', 'vcruntime140.dll'):  # Windows only looks beside the exe it starts
    copy(crt / dll, out / 'compiler' / dll)
    copy(crt / dll, out / 'tools' / dll)
copy(native / 'tools/RFD Switchblade.exe', out / 'tools/RFD Switchblade.exe')

readme = (root / 'docs/release/README.txt').read_text(encoding='utf-8').replace('{VERSION}', version)
(out / 'README.txt').write_text(readme, encoding='utf-8', newline='\r\n')
copy(root / 'tools/XboxXBERecomp/THIRD_PARTY.txt', out / 'licenses/THIRD_PARTY.txt')
copy(root / 'tools/XboxXBERecomp/LICENSE', out / 'licenses/XboxXBERecomp-LICENSE.txt')
for lib, src in (('imgui', 'imgui-src/LICENSE.txt'), ('SDL3', 'sdl3-src/LICENSE.txt'),
                 ('tomlplusplus', 'tomlplusplus-src/LICENSE'), ('Zydis', 'zydis-src/LICENSE')):
    copy(native / '_deps' / src, out / f'licenses/{lib}-LICENSE.txt')

# empty folders the player fills, each saying what goes there
for folder, text in (('assets', 'Optional: put your Tenchu: Return from Darkness (USA) .iso here instead of picking it on the first start.\r\n'),
                     ('textures/packs', 'Put texture pack folders here (PS2 Tenchu: Wrath of Heaven packs work), then open F6 > Textures.\r\n'
                                        'tools\\RFD Switchblade.exe checks every match and lets you choose the ones it missed.\r\n'),
                     ('mods', 'Each folder here is a mod that mirrors the game disc. See README.txt and F6 > Mods.\r\n'),
                     ('levels', 'Custom levels: each folder here is one level, made in tools\\RFD Switchblade.exe > Levels.\r\n'
                                'Pick one in F6 > Levels, then start the mission it is built on. See README.txt.\r\n'),
                     ('memory_patches', 'Memory patches: put .txt files here and turn them on in F6 > Patches. A file can hold several\r\n'
                                        'patches, in the format people share for Cheat Engine:\r\n\r\n'
                                        '[Patch name]\r\nAuthor=Your name\r\nNotes=Optional description\r\nAddress,1008E40A0,value,00000063\r\n\r\n'
                                        'Addresses can be Cheat Engine\'s (game memory starts at 100000000) or the game\'s own. The value\'s\r\n'
                                        'digits set its size (2 = byte, 4 = 2 bytes, 8 = 4 bytes); a value with a point is a float. Other\r\n'
                                        'lines are ignored, so a whole post can be pasted in. This file is not read as a patch.\r\n\r\n'
                                        'The examples folder shows a file with one patch and a file with several. Copy one up into\r\n'
                                        'this folder to use it; files in examples are not read by the game.\r\n')):
    (out / folder).mkdir(parents=True, exist_ok=True)
    (out / folder / 'README.txt').write_text(text, encoding='utf-8')
for f in (root / 'docs/release/memory_patches/examples').glob('*.txt'):
    (out / 'memory_patches/examples').mkdir(parents=True, exist_ok=True)
    (out / 'memory_patches/examples' / f.name).write_text(f.read_text(encoding='utf-8'), encoding='utf-8', newline='\r\n')

archive = dist / f'{name}.zip'
archive.unlink(missing_ok=True)
with zipfile.ZipFile(archive, 'w', zipfile.ZIP_DEFLATED, compresslevel=9) as z:
    for f in sorted(out.rglob('*')):
        if f.is_file():
            z.write(f, Path(name) / f.relative_to(out))
size = sum(f.stat().st_size for f in out.rglob('*') if f.is_file())
print(f'{out}  ({size / 2**20:.0f} MiB)')
print(f'{archive}  ({archive.stat().st_size / 2**20:.0f} MiB)')
