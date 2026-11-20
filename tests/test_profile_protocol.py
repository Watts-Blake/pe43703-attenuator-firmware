"""Production attctl over local PTYs into production firmware; no hardware."""
import ctypes
import io
import os
from pathlib import Path
import pty
import select
import shlex
import subprocess
import tempfile
import threading
import unittest
from unittest.mock import patch

from test_attctl import FakeResolver, a


class FirmwareUnit:
    def __init__(self, lib, pause=False, legacy=False):
        self.lib, self.model = lib, lib.model_new()
        self.master, self.slave = pty.openpty()
        self.port = os.ttyname(self.slave)
        self.pause, self.paused = pause, False
        self.legacy = legacy
        self.records, self.errors = [], []
        self.stop = threading.Event()
        self.worker = threading.Thread(target=self.serve, daemon=True)
        self.worker.start()

    def serve(self):
        try:
            pending = b''
            while not self.stop.is_set():
                if not select.select([self.master], [], [], .02)[0]:
                    continue
                pending += os.read(self.master, 1024)
                while b'\n' in pending:
                    command, pending = pending.split(b'\n', 1)
                    out = ctypes.create_string_buffer(16)
                    dispatch = self.lib.model_legacy_command if self.legacy else self.lib.model_command
                    dispatch(self.model, command + b'\n', out)
                    if self.pause and not self.paused and command.startswith(b'profile keep '):
                        # Successful renewal, then host pause before its next SET.
                        self.lib.model_pause_local(self.model)
                        self.paused = True
                    self.records.append((command.decode(), out.value, self.lib.model_applied(self.model)))
                    os.write(self.master, out.value)
        except Exception as exc:
            self.errors.append(exc)

    def close(self):
        self.stop.set()
        self.worker.join(1)
        assert not self.worker.is_alive()
        os.close(self.master)
        os.close(self.slave)
        self.lib.model_free(self.model)


class ProfileProtocolTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        root = Path(__file__).resolve().parents[1]
        shared = Path(cls.temp.name) / 'profile.so'
        sources = ['tests/profile_bridge.c', 'firmware/ui.c', 'firmware/core.c',
                   'firmware/adc.c', 'firmware/oled.c', 'firmware/screen.c']
        subprocess.run(shlex.split(os.environ.get('CC_HOST', 'cc')) + [
            '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-Wconversion',
            '-shared', '-fPIC', '-Ifirmware', *sources, '-o', str(shared)], cwd=root, check=True)
        cls.lib = ctypes.CDLL(str(shared))
        cls.lib.model_new.restype = ctypes.c_void_p
        for name in ('model_free', 'model_pause_local', 'model_applied'):
            getattr(cls.lib, name).argtypes = [ctypes.c_void_p]
        cls.lib.model_free.restype = cls.lib.model_pause_local.restype = None
        cls.lib.model_command.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_char_p]
        cls.lib.model_command.restype = None
        cls.lib.model_legacy_command.argtypes = cls.lib.model_command.argtypes
        cls.lib.model_legacy_command.restype = None
        cls.lib.model_applied.restype = ctypes.c_uint32

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def test_expiry_after_keep_before_set_preserves_local_apply(self):
        units = [FirmwareUnit(self.lib, pause=True), FirmwareUnit(self.lib)]
        try:
            resolver = FakeResolver(dict(zip(('A', 'B'), (u.port for u in units))))
            with tempfile.TemporaryDirectory() as tmp, patch.object(a.Serial, 'check_holders'), patch('sys.stdout', io.StringIO()):
                result = a.run({}, target='profile', steps=[{'A': '20.00', 'B': '1.00', 'duration': .05}],
                               resolver=resolver, lock_path=str(Path(tmp) / 'lock'), timeout=.5)
            self.assertTrue(units[0].paused)
            self.assertEqual(result, 1)
            self.assertFalse(units[0].errors or units[1].errors)
            # The stale schedule must never overwrite the fresh local 0.25 dB.
            self.assertNotIn(80, [qdb for _, _, qdb in units[0].records], units[0].records)
            rejected = [r for r in units[0].records if r[0].startswith('profile set ')]
            self.assertEqual(len(rejected), 1)
            self.assertEqual(rejected[0][1:], (b'err lock\r\n', 1))
            self.assertNotIn(4, [qdb for _, _, qdb in units[1].records], units[1].records)
            for unit in units:
                self.assertIn(('set 0.00', b'ok 0.00\r\n', 0), unit.records)
                self.assertIn(('get', b'0.00\r\n', 0), unit.records)
        finally:
            for unit in units:
                unit.close()

    def test_success_and_cancellation_cleanup_with_mixed_firmware(self):
        for legacy in (False, True):
            for cancel in (False, True):
                with self.subTest(legacy=legacy, cancel=cancel):
                    units = [FirmwareUnit(self.lib), FirmwareUnit(self.lib, legacy=legacy)]
                    try:
                        resolver = FakeResolver(dict(zip(('A', 'B'), (u.port for u in units))))
                        output = io.StringIO()
                        emit = a.emit

                        def on_event(event, **fields):
                            emit(event, **fields)
                            if cancel and event == 'step':
                                raise a.Interrupted('cancelled')

                        with tempfile.TemporaryDirectory() as tmp, patch.object(a.Serial, 'check_holders'), patch('sys.stdout', output), patch.object(a, 'emit', on_event):
                            result = a.run({}, target='profile', steps=[{'A': '31.75', 'B': '1.00', 'duration': .05}],
                                           resolver=resolver, lock_path=str(Path(tmp) / 'lock'), timeout=.5)
                        self.assertEqual(result, int(cancel))
                        self.assertFalse(units[0].errors or units[1].errors)
                        for unit, qdb in zip(units, (127, 4)):
                            applied = [r for r in unit.records if r[1].startswith(b'ok ') and r[2] == qdb]
                            self.assertEqual(len(applied), 1)
                            if unit.legacy:
                                self.assertTrue(applied[0][0].startswith('set '))
                            else:
                                self.assertTrue(applied[0][0].startswith('profile set '))
                                self.assertTrue(unit.records[-1][0].startswith('profile end '))
                                self.assertEqual(unit.records[-1][1], b'ok end\r\n')
                            self.assertIn(('set 0.00', b'ok 0.00\r\n', 0), unit.records)
                            self.assertIn(('get', b'0.00\r\n', 0), unit.records)
                    finally:
                        for unit in units:
                            unit.close()
