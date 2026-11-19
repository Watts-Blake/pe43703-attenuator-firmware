import hashlib
import os
from pathlib import Path
import pty
import struct
import sys
import threading
import unittest
from unittest.mock import patch
import zlib
sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'tools'))
from pack_image import bundle, validate_app
from usb_update import Serial, update, HELLO

APP = Path(__file__).resolve().parents[1]/'build/attenuator.bin'


class Updates(unittest.TestCase):
    def test_bundle_exact_layout_and_crc(self):
        app = APP.read_bytes()
        loader = APP.with_name('bootloader.bin').read_bytes()
        combined = bundle(loader, app)
        self.assertEqual(combined, APP.with_name('combined.bin').read_bytes())
        self.assertEqual(len(combined), 65536)
        self.assertEqual(combined[:len(loader)], loader)
        self.assertEqual(combined[len(loader):8192], b'\xff'*(8192-len(loader)))
        self.assertEqual(combined[8192:8192+len(app)], app)
        magic, size, crc, inverse_size, inverse_crc = struct.unpack_from('<5I', combined, 64512)
        self.assertEqual((magic,size,crc), (0x41505031,len(app),zlib.crc32(app)))
        self.assertEqual(inverse_size, size^0xffffffff)
        self.assertEqual(inverse_crc, crc^0xffffffff)

    def test_bad_app_never_touches_transport(self):
        from unittest.mock import Mock
        app = APP.read_bytes()
        for bad in (b'', app+b'x', b'\x00'*len(app), app[:4]+struct.pack('<I',0x08000001)+app[8:],
                    app+b'\x00'*56320):
            link = Mock()
            with self.assertRaises(ValueError): update(link,bad,'app')
            link.request.assert_not_called()

    def test_real_posix_serial_over_pty(self):
        app = APP.read_bytes()
        master, slave = pty.openpty()
        path = os.ttyname(slave)
        serial = Serial(path)
        errors = []
        observed = bytearray()
        def peer():
            try:
                buf = b''
                sequence = [b'bootloader',b'hello',f'begin {len(app):08x} {zlib.crc32(app):08x}'.encode()]
                sequence += [f'write {off:08x} {app[off:off+64].hex()}'.encode() for off in range(0,len(app),64)]
                sequence += [b'finish', b'run', b'get']
                for expected in sequence:
                    while b'\n' not in buf:
                        buf += os.read(master, 512)
                    line,buf = buf.split(b'\n',1)
                    self.assertEqual(line,expected)
                    if line.startswith(b'write '): observed.extend(bytes.fromhex(line.split()[2].decode()))
                    reply = (b'ok bootloader\r\n' if line==b'bootloader' else HELLO if line==b'hello'
                             else b'0.00\r\n' if line==b'get' else b'ok\n')
                    # Fragment every response to exercise incremental reads.
                    for byte in reply: os.write(master,bytes([byte]))
            except BaseException as exc: errors.append(exc)
        t = threading.Thread(target=peer,daemon=True); t.start()
        try: update(serial,app,'app')
        finally:
            serial.close(); t.join(timeout=2); os.close(master); os.close(slave)
        self.assertFalse(t.is_alive()); self.assertFalse(errors,errors)
        self.assertEqual(hashlib.sha256(observed).digest(),hashlib.sha256(app).digest())

    def test_failures_stop_without_run_or_retry(self):
        app = APP.read_bytes()
        commands = []
        class Peer:
            def request(self, command, expected, **kwargs):
                commands.append(command)
                if command.startswith(failure): raise ValueError('injected failure')
        for failure in (b'hello',b'begin ',b'write ',b'finish',b'run',b'get'):
            commands.clear()
            with patch('usb_update.time.sleep'), self.assertRaises(ValueError): update(Peer(),app,'loader')
            self.assertTrue(commands[-1].startswith(failure))
            self.assertEqual(sum(c.startswith(failure) for c in commands),1)

    def test_serial_timeout_oversize_wrong_reply(self):
        for mode in ('timeout','long','wrong'):
            master,slave=pty.openpty(); serial=Serial(os.ttyname(slave))
            if mode=='long': os.write(master,b'x'*65)
            elif mode=='wrong': os.write(master,b'err\n')
            try:
                with self.assertRaises((ValueError,TimeoutError)): serial.request(b'hello\n',HELLO,seconds=.02)
            finally: serial.close(); os.close(master); os.close(slave)


if __name__ == '__main__': unittest.main()
