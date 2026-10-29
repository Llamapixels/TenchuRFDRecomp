"""Extract the verified official portable compiler; requires Python 3.14.

Download first, saved as tools/llvm/archive.tar.zst:
https://github.com/llvm/llvm-project/releases/download/llvmorg-23.1.4/clang%2Bllvm-23.1.4-x86_64-pc-windows-msvc.tar.zst
"""
from pathlib import Path
from compression import zstd
import tarfile, hashlib
root = Path(__file__).resolve().parents[2]
folder = root/'tools/llvm'
archive = folder/'archive.tar.zst'
DIGEST = '1a3f2f998e230b5791cc4e2bfe11a1f7c07fe42c96ff44f227c9db0bedfaf091'  # the release asset's SHA256
with archive.open('rb') as f:
    assert hashlib.file_digest(f,'sha256').hexdigest() == DIGEST, 'not the official LLVM 23.1.4 archive'
n=0
with zstd.open(archive,'rb',options={zstd.DecompressionParameter.window_log_max:31}) as f:
    with tarfile.open(fileobj=f,mode='r|') as tar:
        for m in tar:
            if m.name.endswith(('/bin/clang.exe','/bin/clang++.exe','/bin/lld.exe','/bin/lld-link.exe','/bin/llvm-ar.exe','/bin/llvm-lib.exe','/bin/llvm-ranlib.exe','/bin/llvm-rc.exe','/LICENSE.TXT')) or '/lib/clang/' in m.name or ('/bin/' in m.name and m.name.endswith('.dll')):
                tar.extract(m,folder,filter='data')
                n+=1
print(f'Verified SHA256; extracted {n} compiler files')
