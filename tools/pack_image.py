"""Deterministic offline bundle: loader, app, and CRC commit page."""
from pathlib import Path
import struct
import zlib
from image import read_elf

APP_BASE, META_BASE, END = 0x08002000, 0x0800fc00, 0x08010000


def validate_app(app):
    if len(app) < 236 or len(app) > META_BASE-APP_BASE or len(app) % 4:
        raise ValueError('app size must be 236..56320 bytes, multiple of four')
    sp, pc = struct.unpack_from('<II', app)
    if not 0x20000100 <= sp <= 0x20004ff8 or sp % 8 or not pc & 1 or not APP_BASE <= pc & ~1 < APP_BASE+len(app):
        raise ValueError('invalid relocated app vectors')
    return zlib.crc32(app)


def bundle(loader, app):
    crc = validate_app(app)
    if not 236 <= len(loader) <= 8192:
        raise ValueError('loader must fit first 8 KiB')
    image = bytearray(b'\xff'*65536)
    image[:len(loader)] = loader
    image[8192:8192+len(app)] = app
    image[64512:64532] = struct.pack('<5I', 0x41505031, len(app), crc, len(app)^0xffffffff, crc^0xffffffff)
    return bytes(image)


if __name__ == '__main__':
    app = read_elf('build/attenuator.elf')['binary']
    loader = read_elf('build/bootloader.elf', loader=True)['binary']
    if Path('build/attenuator.bin').read_bytes() != app or Path('build/bootloader.bin').read_bytes() != loader:
        raise SystemExit('BIN differs from ELF')
    Path('build/combined.bin').write_bytes(bundle(loader, app))
