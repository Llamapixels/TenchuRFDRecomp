"""Bind the API actually used by this disc's join paths, not a lookalike thunk."""
from pathlib import Path
import struct
import tomllib

root = Path(__file__).resolve().parents[1]
image = (root / 'assets/default.xbe').read_bytes()
base = struct.unpack_from('<I', image, 0x104)[0]
count, headers = struct.unpack_from('<II', image, 0x11c)

def at(address, size):
    for i in range(count):
        offset = headers - base + i * 56
        va, _, raw, length = struct.unpack_from('<4I', image, offset + 4)
        if va <= address and address + size <= va + length:
            return image[raw + address - va:raw + address - va + size]
    raise AssertionError(f'Address outside image: {address:08X}')

config = tomllib.loads((root / 'config/startup.toml').read_text())
entry = config['symbols']['XNetXnAddrToInAddr']
for callsite in (0x0002f35f, 0x0002fad7, 0x00030b05):
    instruction = at(callsite, 5)
    assert instruction[0] == 0xe8, 'Expected original direct join call'
    target = callsite + 5 + struct.unpack_from('<i', instruction, 1)[0]
    assert target == entry, f'Join call {callsite:08X} uses {target:08X}, but configuration binds {entry:08X}'

# The matched thunk passes an XNADDR pointer to code comparing its six-byte MAC.
thunk = at(entry, 23)
assert thunk[-5] == 0xe9
body = entry + 23 + struct.unpack_from('<i', thunk, 19)[0]
assert b'\x8d\x78\x0a' in at(body, 32), 'Expected XNADDR MAC access at pointer+0A'

# The previous erroneous entry is a different service-address operation. Its
# validator explicitly treats argument0 as an IPv4 value, not an XNADDR pointer.
assert entry != 0x003e566d
print(f'PASS: all three original RFD join calls bind XNetXnAddrToInAddr at {entry:08X}; XNADDR pointer ABI confirmed.')
