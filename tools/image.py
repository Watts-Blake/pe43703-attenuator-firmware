"""Small, dependency-free ELF32/ARM inspector; never opens a device."""
import hashlib
import struct
from pathlib import Path

FLASH, FLASH_END = 0x08000000, 0x08010000
RAM, RAM_END = 0x20000000, 0x20005000


def read_elf(path):
    data = Path(path).read_bytes()
    if len(data) < 52 or data[:7] != b'\x7fELF\x01\x01\x01':
        raise ValueError('expected little-endian ELF32')
    h = struct.unpack_from('<16sHHIIIIIHHHHHH', data)
    _, kind, machine, _, entry, phoff, shoff, _, _, phsize, phnum, shsize, shnum, names = h
    if machine != 40 or kind != 2 or phsize != 32 or shsize != 40:
        raise ValueError('expected executable ARM ELF')
    def take(offset, size):
        if offset < 0 or size < 0 or offset + size > len(data):
            raise ValueError('ELF range outside file')
        return data[offset:offset + size]
    sections = [struct.unpack('<10I', take(shoff + i*40, 40)) for i in range(shnum)]
    strings = sections[names]
    section_names = take(strings[4], strings[5])
    def name(table, offset):
        return table[offset:].split(b'\0', 1)[0].decode('ascii')
    by_name = {name(section_names, s[0]): s for s in sections}
    symbols = {}
    for s in sections:
        if s[1] != 2:
            continue
        if s[9] != 16 or s[5] % 16:
            raise ValueError('bad symbol table')
        st = sections[s[6]]
        table = take(st[4], st[5])
        for offset in range(s[4], s[4] + s[5], 16):
            n, value, size, _, _, index = struct.unpack('<IIIBBH', take(offset, 16))
            symbols[name(table, n)] = (value, size, index)
    loads = []
    for i in range(phnum):
        typ, off, virtual, physical, filesz, memsz, flags, _ = struct.unpack('<8I', take(phoff + i*32, 32))
        if typ != 1:
            continue
        if filesz > memsz:
            raise ValueError('bad load sizes')
        if filesz:
            if not FLASH <= physical < physical + filesz <= FLASH_END:
                raise ValueError('load image outside 64 KiB flash')
            loads.append((physical, take(off, filesz)))
        if memsz and not (FLASH <= virtual < virtual + memsz <= FLASH_END or
                          RAM <= virtual < virtual + memsz <= RAM_END):
            raise ValueError('load segment outside target memory')
    loads.sort()
    if not loads or loads[0][0] != FLASH or len(loads[0][1]) < 236:
        raise ValueError('missing vector table')
    stack, reset = struct.unpack_from('<II', loads[0][1])
    if stack != RAM_END or not reset & 1 or reset != entry:
        raise ValueError('bad reset vector/entry/stack')
    if not FLASH <= reset & ~1 < FLASH_END:
        raise ValueError('reset outside flash')
    if symbols.get('mailbox', ())[:2] != (RAM, 128):
        raise ValueError('mailbox symbol/address/size mismatch')
    mb = by_name.get('.mailbox')
    if not mb or mb[1] != 8 or mb[3] != RAM or mb[5] != 128:
        raise ValueError('mailbox must be NOBITS at RAM base')
    end = max(addr + len(blob) for addr, blob in loads)
    image = bytearray(end - FLASH)
    covered = set()
    for addr, blob in loads:
        indexes = set(range(addr-FLASH, addr-FLASH+len(blob)))
        if covered & indexes:
            raise ValueError('overlapping flash loads')
        covered |= indexes
        image[addr-FLASH:addr-FLASH+len(blob)] = blob
    return {'entry': entry, 'stack': stack, 'loads': loads, 'binary': bytes(image),
            'symbols': symbols, 'sha256': hashlib.sha256(data).hexdigest()}
