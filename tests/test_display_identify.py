"""Scripted portable interaction and production firmware over local PTYs."""
import ctypes
import io
import os
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import display_identify as di
from test_profile_protocol import FirmwareUnit


class ScriptedLink:
    def __init__(self, *, capability=di.CAPABILITY, failure=None):
        self.failed = False
        self.commands = []
        self.capability, self.failure = capability, failure
        self.trial, self.saved, self.saving = 41, 41, False

    def request(self, command, expected=None, seconds=2):
        self.commands.append(command)
        if command == self.failure:
            self.failed = True
            raise TimeoutError('UART disconnected; completion unknown')
        op = command.decode().strip()
        if op == 'display':
            result = self.capability
        elif op == 'display get':
            result = f'config {self.saved:02d} 62\r\n'.encode()
        elif op in ('display start', 'display sweep'):
            self.trial = 0 if op.endswith('sweep') else 41
            result = b'ok display\r\n'
        elif op == 'display status':
            result = (f'saved {self.saved:02d}\r\n' if self.saving else f'trial {self.trial:02d} R\r\n').encode()
        elif op == 'display next':
            self.trial += 48
            result = b'ok next\r\n'
        elif op.startswith('display try '):
            self.trial = int(op.split()[-1])
            result = b'ok try\r\n'
        elif op.startswith('display save '):
            self.saved = int(op.split()[-1])
            self.saving = True
            result = b'ok saving\r\n'
        elif op == 'display cancel':
            result = b'ok cancel\r\n'
        elif op == 'display finish':
            result = b'ok finish\r\n'
        else:
            raise AssertionError(op)
        if expected is not None and result != expected:
            self.failed = True
            raise ValueError('unexpected reply')
        return result


class InteractionTests(unittest.TestCase):
    def session(self, link, answers):
        iterator = iter(answers)
        def ask(prompt, seconds):
            self.assertTrue(0 < seconds <= 90)
            value = next(iterator)
            if isinstance(value, BaseException):
                raise value
            return value
        return di.Identification(link, .2, ask=ask, output=lambda s: None)

    def test_exact_capability_legacy_and_future_refusal(self):
        for capability in (b'err\r\n', b'display 2\r\n', b'display 1\n', b'display 10\r\n'):
            link = ScriptedLink(capability=capability)
            session = self.session(link, [])
            with self.assertRaises(ValueError):
                session.run()
            session.cancel()
            self.assertEqual(link.commands, [b'display\n'])

    def test_explicit_save_only(self):
        for family, index in [('SSD1306', 41), ('SH1106', 89)]:
            link = ScriptedLink()
            session = self.session(link, ['', family, f'SAVE {index}'])
            session.run()
            session.cancel()
            self.assertEqual(link.saved, index)
            self.assertIn(f'display save {index}\n'.encode(), link.commands)
            self.assertNotIn(b'display cancel\n', link.commands)
            self.assertTrue(all(c.startswith(b'display') for c in link.commands))

    def test_cancel_eof_signal_and_prompt_timeout_never_save(self):
        for failure in ('cancel', EOFError(), KeyboardInterrupt(), TimeoutError('prompt expired')):
            for stage in range(3):
                answers = ['', 'SH1106', 'SAVE 89'][:stage] + [failure]
                link = ScriptedLink()
                session = self.session(link, answers)
                with self.assertRaises((OSError, EOFError, KeyboardInterrupt)):
                    session.run()
                session.cancel()
                self.assertEqual(link.saved, 41)
                self.assertEqual(link.commands[-1], b'display cancel\n')
                self.assertFalse(any(c.startswith(b'display save') for c in link.commands))

    def test_unconfirmed_choice_and_neither(self):
        for answers in (['', 'SH1106', 'yes'], ['', 'bogus']):
            link = ScriptedLink()
            session = self.session(link, answers)
            with self.assertRaises((ValueError, InterruptedError)):
                session.run()
            session.cancel()
            self.assertEqual(link.saved, 41)
        link = ScriptedLink()
        self.session(link, ['', 'neither']).run()
        self.assertEqual(link.commands[-1], b'display finish\n')

    def test_serial_failure_does_not_replay_or_reopen(self):
        for command in (b'display start\n', b'display next\n', b'display try 89\n', b'display save 89\n'):
            link = ScriptedLink(failure=command)
            session = self.session(link, ['', 'SH1106', 'SAVE 89'])
            with self.assertRaises(TimeoutError):
                session.run()
            session.cancel()
            self.assertEqual(link.commands[-1], command)
            self.assertEqual(link.commands.count(command), 1)

    def test_sweep_is_explicit_and_save_requires_confirmation(self):
        link = ScriptedLink()
        session = self.session(link, ['yes', 'SAVE 00'])
        session.run(sweep=True)
        self.assertEqual(link.saved, 0)
        self.assertIn(b'display sweep\n', link.commands)
        self.assertIn(b'display save 00\n', link.commands)

    def test_unexpected_front_key_change_cancels_without_save(self):
        link = ScriptedLink()
        session = self.session(link, [])
        original = link.request
        def changed(command, expected=None, seconds=2):
            if command == b'display status\n':
                link.trial = 89
            return original(command, expected, seconds)
        link.request = changed
        with self.assertRaisesRegex(ValueError, 'changed'):
            session.run()
        session.cancel()
        self.assertEqual(link.commands[-1], b'display cancel\n')
        self.assertFalse(any(c.startswith(b'display save') for c in link.commands))

    def test_render_wait_and_requests_share_five_second_deadline(self):
        link = ScriptedLink()
        now = [0.0]
        budgets = []
        original = link.request
        def waiting(command, expected=None, seconds=2):
            result = original(command, expected, seconds)
            if command == b'display status\n':
                budgets.append(seconds)
                return b'trial 41 W\r\n'
            return result
        link.request = waiting
        session = di.Identification(link, 30, ask=lambda p, s: self.fail('unfinished frame prompt'),
                                    output=lambda s: None, clock=lambda: now[0],
                                    sleep=lambda s: now.__setitem__(0, now[0] + s))
        with self.assertRaises(TimeoutError):
            session.run()
        session.cancel()
        self.assertTrue(all(0 < b <= 5 for b in budgets))
        self.assertLessEqual(now[0], 5.1)

    def test_total_session_deadline_during_prompt_does_not_save(self):
        now = [0.0]
        def late(prompt, seconds):
            now[0] = 601
            return ''
        link = ScriptedLink()
        session = di.Identification(link, 2, ask=late, output=lambda s: None, clock=lambda: now[0])
        with self.assertRaises(TimeoutError):
            session.run()
        session.cancel()
        self.assertEqual(link.commands[-1], b'display cancel\n')
        self.assertNotIn(b'display next\n', link.commands)

    def test_lost_save_completion_does_not_retry_or_claim_failure_of_commit(self):
        link = ScriptedLink()
        original = link.request
        def lost(command, expected=None, seconds=2):
            if link.saving and command == b'display status\n':
                link.failure = command
            return original(command, expected, seconds)
        link.request = lost
        session = self.session(link, ['', 'SH1106', 'SAVE 89'])
        messages = []
        session.output = messages.append
        with self.assertRaises(TimeoutError):
            session.run()
        session.cancel()
        self.assertEqual(link.saved, 89)
        self.assertEqual(link.commands.count(b'display save 89\n'), 1)
        self.assertEqual(link.commands[-1], b'display status\n')
        self.assertTrue(any('may already have committed' in m for m in messages))

    def test_busy_start_never_cancels_existing_owner(self):
        link = ScriptedLink()
        original = link.request
        def busy(command, expected=None, seconds=2):
            if command == b'display start\n':
                link.commands.append(command)
                link.failed = True
                raise ValueError('unexpected UART reply: err busy')
            return original(command, expected, seconds)
        link.request = busy
        session = self.session(link, [])
        with self.assertRaisesRegex(ValueError, 'busy'):
            session.run()
        session.cancel()
        self.assertEqual(link.commands[-1], b'display start\n')

    def test_real_input_timeout_and_eof(self):
        import threading
        release = threading.Event()
        def blocked(_):
            release.wait(1)
            return ''
        try:
            with patch('builtins.input', blocked), self.assertRaises(TimeoutError):
                di.timed_input('prompt', .01)
        finally:
            release.set()
        with patch('builtins.input', side_effect=EOFError()), self.assertRaises(EOFError):
            di.timed_input('prompt', .1)

    def test_cli_validation_and_offline_help(self):
        with patch.object(di, 'Serial') as factory, patch('sys.stderr', io.StringIO()):
            self.assertEqual(di.main(['--port', 'COM7', '--timeout', 'nan']), 1)
            factory.assert_not_called()
        code = """
import builtins, runpy, sys
original = builtins.__import__
def guarded(name, *args, **kwargs):
    if name in ('serial', 'fcntl', 'termios', 'attctl'):
        raise AssertionError(name)
    return original(name, *args, **kwargs)
builtins.__import__ = guarded
sys.path.insert(0, 'tools')
sys.argv = ['tools/display_identify.py', '--help']
runpy.run_path(sys.argv[0], run_name='__main__')
"""
        result = subprocess.run([sys.executable, '-c', code], cwd=ROOT, capture_output=True, timeout=5)
        self.assertEqual(result.returncode, 0, result.stderr)


class ProductionPtyTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        shared = Path(cls.temp.name) / 'display.so'
        sources = ['tests/display_bridge.c', 'firmware/ui.c', 'firmware/core.c',
                   'firmware/display_id.c', 'firmware/display_config.c',
                   'firmware/adc.c', 'firmware/oled.c', 'firmware/screen.c']
        subprocess.run(shlex.split(os.environ.get('CC_HOST', 'cc')) + [
            '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-Wconversion',
            '-shared', '-fPIC', '-Ifirmware', *sources, '-o', str(shared)], cwd=ROOT, check=True)
        cls.lib = ctypes.CDLL(str(shared))
        cls.lib.model_new.restype = ctypes.c_void_p
        cls.lib.model_free.argtypes = cls.lib.model_applied.argtypes = [ctypes.c_void_p]
        cls.lib.model_free.restype = None
        cls.lib.model_applied.restype = ctypes.c_uint32
        cls.lib.model_command.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_char_p]
        cls.lib.model_command.restype = None
        cls.lib.model_legacy_command.argtypes = cls.lib.model_command.argtypes
        cls.lib.model_legacy_command.restype = None

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def test_helper_saves_through_production_parser_and_restores_display(self):
        from serial_port import Serial
        for sweep, answers, selected in [(False, ['', 'SH1106', 'SAVE 89'], 89), (True, ['yes', 'SAVE 00'], 0)]:
            unit = FirmwareUnit(self.lib)
            try:
                with tempfile.TemporaryDirectory() as tmp, Serial(unit.port, lock_path=str(Path(tmp) / 'lock')) as link:
                    iterator = iter(answers)
                    session = di.Identification(link, .5, ask=lambda p, s: next(iterator), output=lambda s: None)
                    session.run(sweep)
                    self.assertEqual(link.request(b'display status\n'), f'saved {selected:02d}\r\n'.encode())
                    self.assertTrue(link.request(b'display get\n').startswith(f'config {selected:02d} '.encode()))
                self.assertFalse(unit.errors)
                self.assertTrue(all(qdb == 0 for _, _, qdb in unit.records))
            finally:
                unit.close()

    def test_legacy_firmware_refusal_is_exact_and_inert(self):
        from serial_port import Serial
        unit = FirmwareUnit(self.lib, legacy=True)
        try:
            with tempfile.TemporaryDirectory() as tmp, Serial(unit.port, lock_path=str(Path(tmp) / 'lock')) as link:
                session = di.Identification(link, .5, ask=lambda p, s: self.fail('legacy prompt'), output=lambda s: None)
                with self.assertRaisesRegex(ValueError, 'lacks'):
                    session.run()
                session.cancel()
            self.assertEqual(unit.records, [('display', b'err\r\n', 0)])
        finally:
            unit.close()


if __name__ == '__main__':
    unittest.main()
