"""Offline image checks and a machine-readable artifact receipt."""
import hashlib
import json
import sys
from pathlib import Path
from image import read_elf

if __name__ == '__main__':
    elf = read_elf(sys.argv[1])
    binary = Path(sys.argv[1]).with_suffix('.bin').read_bytes()
    if binary != elf['binary']:
        raise SystemExit('BIN differs from ELF load image')
    vectors = elf['binary'][:236]
    import struct
    for index, vector in enumerate(struct.unpack('<59I', vectors)[1:], 1):
        if index in (7,8,9,10,13):
            if vector != 0:
                raise SystemExit('nonzero reserved vector')
            continue
        if not vector & 1 or not 0x08000000 <= vector & ~1 < 0x08010000:
            raise SystemExit('invalid exception vector')
    print(json.dumps({'elf_sha256': elf['sha256'], 'bin_sha256': hashlib.sha256(binary).hexdigest(),
                      'bin_bytes': len(binary), 'entry': hex(elf['entry']),
                      'initial_sp': hex(elf['stack']), 'mailbox': '0x20000000/128 bytes',
                      'vectors_checked': 59}, indent=2))
