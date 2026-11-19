"""Portable fake-serial checks; PTY checks are separately platform qualified."""
from contextlib import contextmanager
import io
import os
from pathlib import Path, PureWindowsPath
import struct
import subprocess
import sys
import tempfile
import threading
import types
import unittest
from unittest.mock import Mock, patch
import zlib

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import attenuator
import serial_port
import usb_update


class FakePort:
    def __init__(self, reply=b'', chunk=1000):
        self.reply, self.chunk = bytearray(reply), chunk
        self.written = bytearray()
        self.open = Mock()
        self.close = Mock()
        self.reset_input_buffer = Mock()

    def write(self, data):
        count = min(len(data), self.chunk)
        self.written.extend(data[:count])
        return count

    def read(self, count):
        value = bytes(self.reply[:count])
        del self.reply[:count]
        return value


class PortableSerial(unittest.TestCase):
    def link(self, peer, port='COM12', platform='win32'):
        factory = Mock(return_value=peer)
        with patch.dict(sys.modules, serial=types.SimpleNamespace(Serial=factory)), patch('serial_port.sys.platform', platform):
            link = serial_port.Serial(port)
        self.addCleanup(link.close)
        return link, factory

    def test_native_names_and_configuration(self):
        for platform, port in [('win32', 'COM12'), ('darwin', '/dev/cu.wchusbserial-example')]:
            peer = FakePort(b'ok 1.25\r\n', chunk=1)
            link, factory = self.link(peer, port, platform)
            self.assertEqual(link.request(b'set 1.25\n', b'ok 1.25\r\n'), b'ok 1.25\r\n')
            self.assertEqual(peer.written, b'set 1.25\n')
            self.assertEqual(peer.port, port)
            self.assertFalse(peer.dtr)
            self.assertFalse(peer.rts)
            options = factory.call_args.kwargs
            self.assertEqual((options['baudrate'], options['bytesize'], options['parity'], options['stopbits']), (115200, 8, 'N', 1))
            self.assertEqual(options.get('exclusive'), None if platform == 'win32' else True)
            for key in ('xonxoff', 'rtscts', 'dsrdtr'):
                self.assertFalse(options[key])
            self.assertTrue(0 < peer.timeout <= 5)
            self.assertTrue(0 < peer.write_timeout <= 5)

    def test_linux_lock_is_acquired_before_serial_and_released_after_close(self):
        events = []
        @contextmanager
        def locked(path):
            events.append(('lock', path))
            yield
            events.append('unlock')
        peer = FakePort()
        peer.close.side_effect = lambda: events.append('close')
        def create(**options):
            events.append('serial')
            return peer
        fake_attctl = types.SimpleNamespace(locked=locked, LOCK_PATH='/run/lock/attctl.lock')
        with patch.dict(sys.modules, attctl=fake_attctl, serial=types.SimpleNamespace(Serial=create)), patch('serial_port.sys.platform', 'linux'):
            link = serial_port.Serial('/dev/ttyUSB0')
            link.close()
        self.assertEqual(events, [('lock', '/run/lock/attctl.lock'), 'serial', 'close', 'unlock'])

    def test_lock_refusal_never_opens_port(self):
        @contextmanager
        def locked(path):
            raise RuntimeError('controller busy')
            yield
        factory = Mock()
        with patch.dict(sys.modules, attctl=types.SimpleNamespace(locked=locked, LOCK_PATH='lock'), serial=types.SimpleNamespace(Serial=factory)), patch('serial_port.sys.platform', 'linux'):
            with self.assertRaisesRegex(RuntimeError, 'busy'):
                serial_port.Serial('/dev/ttyUSB0')
        factory.assert_not_called()

    def test_open_failure_closes_port(self):
        peer = FakePort()
        peer.open.side_effect = OSError('unplugged')
        with patch.dict(sys.modules, serial=types.SimpleNamespace(Serial=Mock(return_value=peer))), patch('serial_port.sys.platform', 'win32'):
            with self.assertRaises(OSError):
                serial_port.Serial('COM1')
        peer.close.assert_called_once()

    def test_invalid_port_never_loads_pyserial(self):
        for port in ('', ' ', 'COM1\nget', 'loop://', 'x' * 1025, None):
            with self.assertRaises(ValueError):
                serial_port.Serial(port)

    def test_reply_errors_poison_session_without_replay(self):
        for reply, error in [(b'', TimeoutError), (b'ok', TimeoutError), (b'err\n', ValueError),
                             (b'ok 0.00\n', ValueError), (b'x' * 65, ValueError)]:
            peer = FakePort(reply)
            link, _ = self.link(peer)
            with self.assertRaises(error):
                link.request(b'set 0\n', b'ok 0.00\r\n', seconds=.02)
            with self.assertRaises(RuntimeError):
                link.request(b'set 0\n')
            self.assertEqual(peer.written, b'set 0\n')

    def test_zero_write_and_disconnect_stop(self):
        for failure in (0, OSError('disconnected')):
            peer = FakePort()
            peer.write = Mock(side_effect=failure) if isinstance(failure, Exception) else Mock(return_value=failure)
            link, _ = self.link(peer)
            with self.assertRaises((OSError, TimeoutError)):
                link.request(b'get\n')
            peer.write.assert_called_once()

    def test_total_deadline_includes_fragmented_reply_and_write(self):
        peer = FakePort(b'ok\n', chunk=1)
        link, _ = self.link(peer)
        with patch('serial_port.time.monotonic', side_effect=[0, .01, .02, .03, .04, .2]):
            with self.assertRaises(TimeoutError):
                link.request(b'x\n', b'ok\n', seconds=.1)

    def test_timeout_validation_before_write(self):
        peer = FakePort()
        link, _ = self.link(peer)
        for value in ('nan', 'inf', '-1', '0', '31', 'bad'):
            with self.assertRaises(ValueError):
                link.request(b'get\n', seconds=value)
        self.assertEqual(peer.written, b'')

    def test_cli_validation_and_help_open_nothing(self):
        for argv in (['set', '--port', 'COM1', '32'], ['set', '--port', 'COM1', '1.001'],
                     ['set', '--port', 'COM1', 'NaN'], ['get', '--port', 'COM1', '--timeout', 'nan']):
            with patch('attenuator.Serial') as factory, patch('sys.stderr', io.StringIO()):
                self.assertEqual(attenuator.main(argv), 1)
                factory.assert_not_called()
        for value in ('1e0', '-0', '.25', '1.250', '1\n', '0.01', '31.76', '9' * 1000):
            with self.assertRaises(ValueError):
                attenuator.db(value)
        for value, result in [('0', '0.00'), ('01.25', '1.25'), ('31.75', '31.75')]:
            self.assertEqual(attenuator.db(value), result)
        # Missing dependency and blocked POSIX imports prove help/import stay offline.
        code = """
import builtins, pathlib, runpy, sys
original = builtins.__import__
def guarded(name, *args, **kwargs):
    if name in ('serial', 'termios', 'fcntl', 'attctl'):
        raise AssertionError('unexpected import: ' + name)
    return original(name, *args, **kwargs)
builtins.__import__ = guarded
sys.path.insert(0, 'tools')
import attenuator, usb_update, serial_port
sys.argv = [sys.argv[1], '--help']
runpy.run_path(sys.argv[0], run_name='__main__')
"""
        for script in ('tools/attenuator.py', 'tools/usb_update.py'):
            result = subprocess.run([sys.executable, '-c', code, script], cwd=ROOT, capture_output=True, timeout=5)
            self.assertEqual(result.returncode, 0, result.stderr)

    def test_set_requires_exact_ack_and_matching_get(self):
        for replies, status in [(b'ok 1.25\r\n1.25\r\n', 0), (b'ok 1.00\r\n', 1),
                                (b'ok 1.25\r\n1.00\r\n', 1), (b'ok 1.25\r\nerr\r\n', 1)]:
            peer = FakePort(replies)
            link, _ = self.link(peer)
            with patch('attenuator.Serial', return_value=link), patch('sys.stdout', io.StringIO()) as output, patch('sys.stderr', io.StringIO()):
                self.assertEqual(attenuator.main(['set', '--port', 'COM12', '1.25']), status)
                self.assertEqual(output.getvalue(), '1.25\n' if status == 0 else '')
            self.assertEqual(peer.written, b'set 1.25\n' if replies == b'ok 1.00\r\n' else b'set 1.25\nget\n')

    def test_get_rejects_noncanonical_replies(self):
        for reply in (b'32.00\r\n', b'01.25\r\n', b'1.2\r\n', b'1.00\n', b'ok 1.00\r\n', b'\xff\r\n'):
            link, _ = self.link(FakePort(reply))
            with self.assertRaises(ValueError):
                attenuator.get(link, 1)

    def test_full_update_with_fake_serial_and_synthetic_app(self):
        app = struct.pack('<II', 0x20004000, 0x08002009) + b'\0' * 248
        replies = b'ok bootloader\r\n' + usb_update.HELLO + b'ok\n' * 7 + b'0.00\r\n'
        peer = FakePort(replies, chunk=7)
        link, _ = self.link(peer)
        with patch('usb_update.time.sleep'):
            usb_update.update(link, app, 'app')
        commands = peer.written.splitlines()
        self.assertEqual(commands[:3], [b'bootloader', b'hello', f'begin {len(app):08x} {zlib.crc32(app):08x}'.encode()])
        self.assertEqual(commands[-3:], [b'finish', b'run', b'get'])
        self.assertEqual(b''.join(bytes.fromhex(c.split()[2].decode()) for c in commands if c.startswith(b'write ')), app)

    @unittest.skipIf(sys.platform == 'win32', 'POSIX PTY only')
    def test_real_pyserial_set_get_over_pty(self):
        import pty
        master, slave = pty.openpty()
        self.addCleanup(os.close, master)
        self.addCleanup(os.close, slave)
        errors = []
        def peer():
            try:
                for expected, reply in [(b'set 1.25\n', b'ok 1.25\r\n'), (b'get\n', b'1.25\r\n')]:
                    command = bytearray()
                    while not command.endswith(b'\n'):
                        command.extend(os.read(master, 1))
                    self.assertEqual(bytes(command), expected)
                    for byte in reply:
                        os.write(master, bytes([byte]))
            except BaseException as exc:
                errors.append(exc)
        with tempfile.TemporaryDirectory() as directory:
            with serial_port.Serial(os.ttyname(slave), lock_path=str(Path(directory) / 'lock')) as link:
                worker = threading.Thread(target=peer, daemon=True)
                worker.start()
                link.request(b'set 1.25\n', b'ok 1.25\r\n', seconds=1)
                self.assertEqual(attenuator.get(link, 1), '1.25')
                worker.join(timeout=2)
                self.assertFalse(worker.is_alive())
                self.assertEqual(errors, [])

    @unittest.skipIf(sys.platform == 'win32', 'POSIX PTY only')
    def test_real_pyserial_update_over_pty_with_synthetic_app(self):
        import pty
        master, slave = pty.openpty()
        self.addCleanup(os.close, master)
        self.addCleanup(os.close, slave)
        app = struct.pack('<II', 0x20004000, 0x08002009) + b'\0' * 248
        errors, observed = [], []
        sequence = [b'bootloader', b'hello', f'begin {len(app):08x} {zlib.crc32(app):08x}'.encode()]
        sequence += [f'write {offset:08x} {app[offset:offset+64].hex()}'.encode() for offset in range(0, len(app), 64)]
        sequence += [b'finish', b'run', b'get']
        def peer():
            try:
                for expected in sequence:
                    command = bytearray()
                    while not command.endswith(b'\n'):
                        command.extend(os.read(master, 1))
                    line = bytes(command[:-1])
                    self.assertEqual(line, expected)
                    observed.append(line)
                    reply = (b'ok bootloader\r\n' if line == b'bootloader' else usb_update.HELLO if line == b'hello'
                             else b'0.00\r\n' if line == b'get' else b'ok\n')
                    for byte in reply:
                        os.write(master, bytes([byte]))
            except BaseException as exc:
                errors.append(exc)
        with tempfile.TemporaryDirectory() as directory:
            with serial_port.Serial(os.ttyname(slave), lock_path=str(Path(directory) / 'lock')) as link:
                worker = threading.Thread(target=peer, daemon=True)
                worker.start()
                usb_update.update(link, app, 'app')
                worker.join(timeout=2)
                self.assertFalse(worker.is_alive())
                self.assertEqual(errors, [])
                self.assertEqual(observed, sequence)

    def test_updater_confirmation_and_invalid_image_never_open_port(self):
        app = struct.pack('<II', 0x20004000, 0x08002009) + b'\0' * 248
        with tempfile.TemporaryDirectory() as directory:
            image = Path(directory) / 'app.bin'
            image.write_bytes(app)
            with patch.object(sys, 'argv', ['usb_update.py', 'inspect', '--app', str(image)]), patch('usb_update.Serial') as factory, patch('sys.stdout', io.StringIO()) as output:
                usb_update.main()
                self.assertIn('app bytes=256', output.getvalue())
                factory.assert_not_called()
            for content, confirmation, error in [(app, 'wrong', SystemExit), (b'bad', 'wrong', ValueError)]:
                image.write_bytes(content)
                with patch.object(sys, 'argv', ['usb_update.py', 'update', '--app', str(image), '--port', 'COM12', '--entry', 'app', '--confirm', confirmation]), patch('usb_update.Serial') as factory, patch('sys.stdout', io.StringIO()), patch('sys.stderr', io.StringIO()):
                    with self.assertRaises(error):
                        usb_update.main()
                    factory.assert_not_called()

    def test_windows_staged_image_path_uses_tcl_forward_slashes(self):
        from install import tcl_plan
        import install
        identity = dict(kind='uid-bound', device='STM32F103C8T6', unit='example',
                        serial='example', profile='native-dapdirect', dpidr='0x1ba01477',
                        idcode='0x20036410', uid='123456781234567812345678',
                        obr='0x03fffffc', wrpr='0xffffffff')
        with patch.object(install, 'Path') as path:
            path.return_value.resolve.return_value = PureWindowsPath('C:/attenuator/plan/candidate.bin')
            text = tcl_plan(identity, 'flash', 'unused', 'a' * 64)
        self.assertIn('{C:/attenuator/plan/candidate.bin}', text)
        self.assertNotIn('C:\\', text)

    @unittest.skipUnless(sys.platform.startswith('linux'), 'Linux controller lock only')
    def test_real_attctl_lock_refuses_port_open(self):
        from attctl import locked
        with tempfile.TemporaryDirectory() as directory:
            lock_path = str(Path(directory) / 'lock')
            with locked(lock_path), patch.dict(sys.modules, serial=types.SimpleNamespace(Serial=Mock())) as modules:
                with self.assertRaisesRegex(RuntimeError, 'another attctl controller'):
                    serial_port.Serial('/dev/ttyUSB0', lock_path=lock_path)
                modules['serial'].Serial.assert_not_called()
            import pty
            master, slave = pty.openpty()
            try:
                with serial_port.Serial(os.ttyname(slave), lock_path=lock_path):
                    with self.assertRaisesRegex(RuntimeError, 'another attctl controller'):
                        with locked(lock_path):
                            self.fail('lock must refuse attctl while portable link is open')
                with locked(lock_path):
                    pass  # Close releases the persistent inode for the next controller.
            finally:
                os.close(master)
                os.close(slave)


if __name__ == '__main__':
    unittest.main()
