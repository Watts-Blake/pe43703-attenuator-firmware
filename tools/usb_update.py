"""Offline image inspection or explicit CH340 UART app update (Windows, macOS and Linux).

Never opens a port on import or during inspect. Updates cannot replace loader.
"""
import argparse
import hashlib
from pathlib import Path
import time
from pack_image import validate_app
from serial_port import Serial

HELLO = b'boot 1 08002000 0000dc00 00000040\n'


def update(link, app, entry):
    crc = validate_app(app)  # No serial actions before local validation.
    if entry not in ('app', 'loader'):
        raise ValueError('entry must be app or loader')
    if entry == 'app':
        link.request(b'bootloader\n', b'ok bootloader\r\n')
        # MCU reset/loader startup only; no USB reenumeration expected.
        time.sleep(0.1)
    link.request(b'hello\n', HELLO)
    # Only this exact geometry/version handshake permits erase.
    link.request(f'begin {len(app):08x} {crc:08x}\n'.encode(), b'ok\n', seconds=10)
    for offset in range(0, len(app), 64):
        link.request(f'write {offset:08x} {app[offset:offset+64].hex()}\n'.encode(), b'ok\n')
    link.request(b'finish\n', b'ok\n', seconds=10)
    link.request(b'run\n', b'ok\n')
    time.sleep(0.1)
    link.request(b'get\n', b'0.00\r\n')


def main():
    p = argparse.ArgumentParser(description=__doc__)
    sub = p.add_subparsers(dest='action', required=True)
    inspect = sub.add_parser('inspect', help='offline only')
    inspect.add_argument('--app', required=True, type=Path)
    run = sub.add_parser('update', help='DEVICE ACTION: writes app flash and resets MCU')
    run.add_argument('--app', required=True, type=Path)
    run.add_argument('--port', required=True, help='exact identified serial device; no scanning')
    run.add_argument('--entry', required=True, choices=('app','loader'))
    run.add_argument('--confirm', required=True, help='UPDATE <exact-port> <app-SHA256>')
    a = p.parse_args()
    app = a.app.read_bytes()
    crc = validate_app(app)
    sha = hashlib.sha256(app).hexdigest()
    print(f'app bytes={len(app)} crc32={crc:08x} sha256={sha}', flush=True)
    if a.action == 'inspect':
        return
    if a.confirm != f'UPDATE {a.port} {sha}':
        p.error('confirmation must be: '+f'UPDATE {a.port} {sha}')
    link = Serial(a.port)
    try:
        update(link, app, a.entry)
        print('App CRC committed, started, and GET confirmed requested 0.00 dB.')
    finally:
        link.close()


if __name__ == '__main__':
    main()
