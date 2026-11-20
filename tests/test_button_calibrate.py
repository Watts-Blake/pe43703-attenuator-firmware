"""Portable wizard prompts and real production ADC/UI/config paths over PTYs."""
import ctypes
import io
import os
import select
import threading
import time
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import button_calibrate as bc
import test_display_identify as display_tests
from test_profile_protocol import FirmwareUnit

VALUES = [3900, 2500, 1700, 500, 1100]


class ScriptedLink:
    def __init__(self, capability=bc.CAPABILITY, failure=None):
        self.capability, self.failure = capability, failure
        self.failed, self.saved = False, False
        self.commands = []
        self.states = iter([b'release L\r\n', b'hold L\r\n', b'value L 3900\r\n', b'confirm\r\n'])

    def request(self, command, expected=None, seconds=2):
        assert 0 < seconds <= 30
        self.commands.append(command)
        if command == self.failure:
            self.failed = True
            raise TimeoutError('UART failed; completion unknown')
        op = command.decode().strip()
        if op == 'buttons':
            result = self.capability
        elif op == 'buttons get':
            result = b'buttons saved\r\n' if self.saved else b'buttons none\r\n'
        elif op == 'buttons start':
            result = b'ok buttons\r\n'
        elif op == 'buttons status':
            result = b'saved\r\n' if self.saved else next(self.states, b'confirm\r\n')
        elif op.startswith(('buttons value ', 'buttons stored ')):
            key = op[-1]
            result = f'value {key} {VALUES[bc.KEYS.index(key)]:04d}\r\n'.encode()
        elif op == 'buttons save':
            self.saved = True
            result = b'ok saving\r\n'
        elif op == 'buttons cancel':
            result = b'ok cancel\r\n'
        else:
            raise AssertionError(op)
        if expected is not None and expected != result:
            self.failed = True
            raise ValueError('unexpected reply: ' + repr(result))
        return result


class InteractionTests(unittest.TestCase):
    def session(self, link, answers=('START', 'SAVE BUTTONS')):
        answers = iter(answers)
        def ask(prompt, seconds):
            self.assertTrue(0 < seconds <= 90)
            answer = next(answers)
            if isinstance(answer, BaseException):
                raise answer
            return answer
        return bc.Calibration(link, .2, ask=ask, sleep=lambda s: None, output=lambda s: None)

    def test_exact_legacy_future_capabilities_refuse_before_start(self):
        for capability in (b'err\r\n', b'buttons 0\r\n', b'buttons 2\r\n', b'buttons 1\n', b'buttons 10\r\n'):
            link = ScriptedLink(capability)
            session = self.session(link, ())
            with self.assertRaisesRegex(ValueError, 'lacks'):
                session.run()
            session.cancel()
            self.assertEqual(link.commands, [b'buttons\n'])

    def test_explicit_start_save_and_printed_results(self):
        link = ScriptedLink()
        session = self.session(link)
        messages = []
        session.output = messages.append
        session.run()
        session.cancel()
        self.assertEqual(link.commands.count(b'buttons save\n'), 1)
        self.assertNotIn(b'buttons cancel\n', link.commands)
        self.assertTrue(all(c.startswith(b'buttons') for c in link.commands))
        for value in VALUES:
            self.assertTrue(any(f'median {value:04d}' in s for s in messages))

    def test_cancel_eof_signal_timeout_and_wrong_confirmation_never_save(self):
        for failure in ('cancel', 'yes', EOFError(), KeyboardInterrupt(), TimeoutError('prompt deadline')):
            for stage in range(2):
                link = ScriptedLink()
                session = self.session(link, ['START'][:stage] + [failure])
                with self.assertRaises((OSError, EOFError, KeyboardInterrupt)):
                    session.run()
                session.cancel()
                self.assertNotIn(b'buttons save\n', link.commands)
                self.assertEqual(b'buttons cancel\n' in link.commands, bool(stage))

    def test_every_uart_failure_is_terminal_without_replay(self):
        for failed in (b'buttons\n', b'buttons get\n', b'buttons start\n', b'buttons status\n',
                       b'buttons value L\n', b'buttons save\n', b'buttons stored L\n'):
            link = ScriptedLink(failure=failed)
            session = self.session(link)
            with self.assertRaises(TimeoutError):
                session.run()
            session.cancel()
            self.assertEqual(link.commands[-1], failed)
            self.assertEqual(link.commands.count(failed), 1)

    def test_invalid_values_and_config_cannot_be_saved(self):
        for replacement in (b'value L 0032\r\n', b'value L 5000\r\n', b'value O 3900\r\n', b'value L 2500\r\n'):
            link = ScriptedLink()
            original = link.request
            def altered(command, expected=None, seconds=2):
                result = original(command, expected, seconds)
                return replacement if command == b'buttons value L\n' else result
            link.request = altered
            session = self.session(link)
            with self.assertRaises(ValueError):
                session.run()
            session.cancel()
            self.assertNotIn(b'buttons save\n', link.commands)

    def test_busy_start_does_not_cancel_owner(self):
        link = ScriptedLink()
        original = link.request
        def busy(command, expected=None, seconds=2):
            if command == b'buttons start\n':
                link.commands.append(command)
                link.failed = True
                raise ValueError('err busy')
            return original(command, expected, seconds)
        link.request = busy
        session = self.session(link)
        with self.assertRaisesRegex(ValueError, 'busy'):
            session.run()
        session.cancel()
        self.assertEqual(link.commands[-1], b'buttons start\n')

    def test_firmware_error_or_owner_cancellation_is_not_restarted(self):
        for status in (b'err bands\r\n', b'err noise\r\n', b'err hold\r\n', b'err sample\r\n', b'idle\r\n'):
            link = ScriptedLink()
            link.states = iter([status])
            session = self.session(link)
            with self.assertRaisesRegex(ValueError, 'stopped'):
                session.run()
            session.cancel()
            self.assertEqual(link.commands.count(b'buttons start\n'), 1)
            self.assertFalse(link.saved)

    def test_total_deadline_and_confirmation_deadline(self):
        now = [0.0]
        link = ScriptedLink()
        link.states = iter([b'hold L\r\n'] * 700)
        session = self.session(link)
        session.clock = lambda: now[0]
        session.sleep = lambda s: now.__setitem__(0, now[0] + 1)
        with self.assertRaises(TimeoutError):
            session.run()
        session.cancel()
        self.assertEqual(now[0], 600)
        self.assertEqual(link.commands[-1], b'buttons cancel\n')
        link = ScriptedLink()
        calls = [0]
        def late(prompt, seconds):
            calls[0] += 1
            if calls[0] == 2:
                now[0] += 601
                return 'SAVE BUTTONS'
            return 'START'
        session = bc.Calibration(link, .2, ask=late, clock=lambda: now[0], sleep=lambda s: None, output=lambda s: None)
        with self.assertRaises(TimeoutError):
            session.run()
        session.cancel()
        self.assertFalse(link.saved)

    def test_save_wait_is_five_seconds_and_no_automatic_retry(self):
        now = [0.0]
        link = ScriptedLink()
        original = link.request
        budgets = []
        def waiting(command, expected=None, seconds=2):
            result = original(command, expected, seconds)
            if link.saved and command == b'buttons status\n':
                budgets.append(seconds)
                return b'saving\r\n'
            return result
        link.request = waiting
        session = self.session(link)
        session.seconds = 30
        session.clock = lambda: now[0]
        session.sleep = lambda s: now.__setitem__(0, now[0] + s)
        with self.assertRaisesRegex(TimeoutError, 'unknown'):
            session.run()
        session.cancel()
        self.assertTrue(all(0 < s <= 5 for s in budgets))
        self.assertEqual(link.commands.count(b'buttons save\n'), 1)
        self.assertNotIn(b'buttons cancel\n', link.commands)

    def test_lost_completion_and_readback_error_never_cancel_or_replay_save(self):
        for failed in (b'buttons status\n', b'buttons stored L\n'):
            link = ScriptedLink()
            original = link.request
            def lost(command, expected=None, seconds=2):
                if link.saved and command == failed:
                    link.failed = True
                    link.commands.append(command)
                    raise TimeoutError('Save completion/readback unknown')
                return original(command, expected, seconds)
            link.request = lost
            session = self.session(link)
            with self.assertRaisesRegex(TimeoutError, 'unknown'):
                session.run()
            session.cancel()
            self.assertEqual(link.commands.count(b'buttons save\n'), 1)
            self.assertNotIn(b'buttons cancel\n', link.commands)

    def test_on_device_explicit_confirmation_reads_and_prints_saved_values(self):
        link = ScriptedLink()
        original = link.request
        def saved(command, expected=None, seconds=2):
            if command == b'buttons status\n':
                link.saved = True
            return original(command, expected, seconds)
        link.request = saved
        session = self.session(link, ['START'])
        session.run()
        self.assertNotIn(b'buttons save\n', link.commands)
        self.assertIn(b'buttons stored O\n', link.commands)

    def test_cli_validation_and_offline_help(self):
        with patch.object(bc, 'Serial') as factory, patch('sys.stderr', io.StringIO()):
            self.assertEqual(bc.main(['--port', 'COM7', '--timeout', 'nan']), 1)
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
sys.argv = ['tools/button_calibrate.py', '--help']
runpy.run_path(sys.argv[0], run_name='__main__')
"""
        result = subprocess.run([sys.executable, '-c', code], cwd=ROOT, capture_output=True, timeout=5)
        self.assertEqual(result.returncode, 0, result.stderr)


class ProductionButtonTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        display_tests.ProductionPtyTests.setUpClass.__func__(cls)
        for name in ('model_reboot', 'model_automatic', 'model_pending', 'model_future', 'model_captured', 'model_confirm'):
            getattr(cls.lib, name).argtypes = [ctypes.c_void_p]
        cls.lib.model_reboot.restype = cls.lib.model_automatic.restype = None
        cls.lib.model_future.restype = None
        cls.lib.model_pending.restype = ctypes.c_uint32
        cls.lib.model_hold.argtypes = [ctypes.c_void_p, ctypes.c_uint32, ctypes.c_uint32]
        cls.lib.model_hold.restype = None

    @classmethod
    def tearDownClass(cls):
        display_tests.ProductionPtyTests.tearDownClass.__func__(cls)

    def test_full_helper_saved_values_drive_production_adc_pending_and_apply_after_boot(self):
        from serial_port import Serial
        unit = FirmwareUnit(self.lib)
        self.lib.model_automatic(unit.model)
        try:
            with tempfile.TemporaryDirectory() as tmp, Serial(unit.port, lock_path=str(Path(tmp) / 'lock')) as link:
                # A v0.3 display-only journal must survive the new record format.
                link.request(b'display start\n', b'ok display\r\n')
                link.request(b'display try 89\n', b'ok try\r\n')
                link.request(b'display save 89\n', b'ok saving\r\n')
                self.lib.model_reboot(unit.model)
                link.request(b'display get\n', b'config 89 61\r\n')
                link.request(b'buttons get\n', b'buttons none\r\n')
                answers = iter(('START', 'SAVE BUTTONS'))
                messages = []
                session = bc.Calibration(link, .5, ask=lambda p, s: next(answers), output=messages.append, sleep=lambda s: None)
                session.run()
                self.assertEqual(link.request(b'buttons get\n'), b'buttons saved\r\n')
                self.assertTrue(all(qdb == 0 for _, _, qdb in unit.records))
                self.lib.model_reboot(unit.model)
                link.request(b'display get\n', b'config 89 59\r\n')
                self.assertEqual(link.request(b'get\n'), b'0.00\r\n')
                self.lib.model_hold(unit.model, 0, 100)
                self.lib.model_hold(unit.model, 2048, 100)
                self.assertEqual(self.lib.model_pending(unit.model), 0)
                self.lib.model_hold(unit.model, 0, 100)
                self.lib.model_hold(unit.model, 2500, 100)
                self.assertEqual(self.lib.model_pending(unit.model), 1)
                self.assertEqual(link.request(b'get\n'), b'0.00\r\n')
                self.lib.model_hold(unit.model, 0, 100)
                self.lib.model_hold(unit.model, 1100, 100)
                self.assertEqual(link.request(b'get\n'), b'0.25\r\n')
                self.assertTrue(any('median 3900' in m for m in messages))
            self.assertFalse(unit.errors)
        finally:
            unit.close()

    def test_local_save_progress_and_confirmation_races(self):
        from serial_port import Serial
        # Stop at production boundaries: never manufacture firmware state/replies.
        for timing in ('before-status', 'during-values', 'before-prompt', 'during-prompt', 'cancel-race', 'save-race'):
            with self.subTest(timing=timing):
                unit = FirmwareUnit(self.lib)
                asked = threading.Event()
                release = threading.Event()
                try:
                    with tempfile.TemporaryDirectory() as tmp, Serial(unit.port, lock_path=str(Path(tmp) / 'lock')) as link:
                        outer = self
                        class Schedule:
                            confirmed = False
                            @property
                            def failed(self):
                                return link.failed
                            def request(self, command, expected=None, seconds=2):
                                if (timing in ('during-prompt', 'cancel-race') and asked.is_set()
                                        and command in (b'buttons status\n', b'buttons save\n', b'buttons cancel\n') and not self.confirmed):
                                    outer.lib.model_confirm(unit.model)
                                    self.confirmed = True
                                if timing == 'save-race' and command == b'buttons save\n':
                                    outer.lib.model_confirm(unit.model)
                                result = link.request(command, expected, seconds=seconds)
                                if timing == 'during-values' and command == b'buttons value L\n':
                                    outer.lib.model_confirm(unit.model)
                                    # The ordinary production bridge finishes journal polls.
                                    link.request(b'buttons status\n', b'saving\r\n')
                                if command == b'buttons start\n':
                                    outer.lib.model_hold(unit.model, 0, 120)
                                    outer.lib.model_captured(unit.model)
                                    if timing == 'before-status':
                                        outer.lib.model_confirm(unit.model)
                                if timing == 'before-prompt' and command == b'buttons value O\n':
                                    outer.lib.model_confirm(unit.model)
                                return result
                        calls = []
                        def ask(prompt, seconds):
                            calls.append(prompt)
                            if len(calls) == 1:
                                return 'START'
                            asked.set()
                            if timing == 'cancel-race':
                                return 'cancel'
                            if timing != 'save-race':
                                release.wait(3)
                            return 'SAVE BUTTONS'
                        messages = []
                        session = bc.Calibration(Schedule(), .5, ask=ask, output=messages.append, sleep=lambda s: None)
                        try:
                            try:
                                session.run()
                            except InterruptedError:
                                if timing != 'cancel-race':
                                    raise
                        finally:
                            session.cancel()
                        self.assertEqual(link.request(b'buttons get\n'), b'buttons saved\r\n')
                        if timing != 'save-race':
                            self.assertTrue(any(r[1] == b'saving\r\n' for r in unit.records))
                        else:
                            self.assertIn(('buttons save', b'ok saving\r\n', 0), unit.records)
                        self.assertFalse(any(r[0] == 'buttons cancel' for r in unit.records))
                        self.assertEqual(sum(r[0] == 'buttons save' for r in unit.records), int(timing == 'save-race'))
                        link.request(b'display get\n', b'config 41 60\r\n')
                        self.assertTrue(all(qdb == 0 for _, _, qdb in unit.records))
                        self.assertFalse(unit.errors)
                        if timing != 'cancel-race':
                            self.assertTrue(any('median 3900' in m for m in messages))
                finally:
                    release.set()
                    unit.close()

    @unittest.skipUnless(sys.platform.startswith('linux'), 'Linux pipe/lock observation; other hardware platforms UNRUN')
    def test_actual_cli_full_stdout_releases_port_and_lock_without_drain(self):
        from serial_port import Serial
        unit = FirmwareUnit(self.lib)
        # A real blocking pipe: setting O_NONBLOCK on a shared writer then leaving
        # it set would invalidate the pressure observation.
        r, w = os.pipe()
        try:
            with tempfile.TemporaryDirectory() as tmp:
                lock = str(Path(tmp) / 'lock')
                code = r"""
import os, sys
sys.path.insert(0, 'tools')
import button_calibrate as bc
from serial_port import Serial
bc.SESSION_SECONDS = .15
port, lock = sys.argv[1:]
def serial(port):
    link = Serial(port, lock_path=lock)
    original = link.request
    def request(command, expected=None, seconds=2):
        result = original(command, expected, seconds=seconds)
        if command == b'buttons start\n':
            # Fill after START on the same blocking descriptor as the reporter.
            os.set_blocking(1, False)
            try:
                while True: os.write(1, b'x' * 4096)
            except BlockingIOError:
                pass
            finally:
                os.set_blocking(1, True)
        return result
    link.request = request
    return link
bc.Serial = serial
sys.exit(bc.main(['--port', port, '--timeout', '.1']))
"""
                p = subprocess.Popen([sys.executable, '-u', '-c', code, unit.port, lock],
                                     cwd=ROOT, stdin=subprocess.PIPE, stdout=w, stderr=subprocess.PIPE)
                try:
                    p.stdin.write(b'START\n')
                    p.stdin.flush()
                    started = time.monotonic()
                    # Never drain stdout until the child has exited and released.
                    self.assertEqual(p.wait(timeout=3), 1)
                    self.assertLess(time.monotonic() - started, 2)
                    self.assertIn(b'Console blocked', p.stderr.read())
                    self.assertTrue(any(r[0] == 'buttons start' for r in unit.records))
                    self.assertTrue(any(r[0] == 'buttons cancel' for r in unit.records))
                    with Serial(unit.port, lock_path=lock) as link:
                        link.request(b'buttons get\n', b'buttons none\r\n')
                    self.assertTrue(all(qdb == 0 for _, _, qdb in unit.records))
                finally:
                    if p.poll() is None:
                        p.kill()
                        p.wait(timeout=2)
                    p.stdin.close()
                    p.stderr.close()
        finally:
            os.close(r)
            os.close(w)
            unit.close()

    def test_actual_cli_local_save_exits_with_console_reader_still_waiting(self):
        ready = threading.Event()
        real = self.lib
        class ScheduledLibrary:
            confirmed = False
            def __getattr__(self, name):
                return getattr(real, name)
            def model_command(self, model, command, out):
                if ready.is_set() and command == b'buttons status\n' and not self.confirmed:
                    real.model_confirm(model)
                    self.confirmed = True
                real.model_command(model, command, out)
        scheduled = ScheduledLibrary()
        unit = FirmwareUnit(scheduled)
        self.lib.model_automatic(unit.model)
        try:
            with tempfile.TemporaryDirectory() as tmp:
                code = """
import sys
sys.path.insert(0, 'tools')
import button_calibrate as bc
from serial_port import Serial
bc.Serial = lambda port: Serial(port, lock_path=sys.argv[2])
sys.exit(bc.main(['--port', sys.argv[1], '--timeout', '.5']))
"""
                p = subprocess.Popen([sys.executable, '-u', '-c', code, unit.port, str(Path(tmp) / 'lock')],
                                     cwd=ROOT, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
                try:
                    p.stdin.write(b'START\n')
                    p.stdin.flush()
                    output = b''
                    end = time.monotonic() + 5
                    while b'Type SAVE BUTTONS' not in output:
                        remaining = end - time.monotonic()
                        self.assertGreater(remaining, 0)
                        self.assertTrue(select.select([p.stdout], [], [], remaining)[0])
                        chunk = os.read(p.stdout.fileno(), 4096)
                        self.assertTrue(chunk)
                        output += chunk
                    ready.set()
                    # Keep stdin open and send no answer: the daemon reader is
                    # still blocked when the actual CLI must close and exit.
                    self.assertEqual(p.wait(timeout=3), 0, p.stderr.read())
                    output += p.stdout.read()
                    self.assertTrue(scheduled.confirmed)
                    self.assertIn(b'Saved complete calibration', output)
                    self.assertFalse(any(r[0] in ('buttons save', 'buttons cancel') for r in unit.records))
                    self.assertTrue(all(qdb == 0 for _, _, qdb in unit.records))
                finally:
                    if p.poll() is None:
                        p.kill()
                        p.wait(timeout=2)
                    p.stdin.close()
                    p.stdout.close()
                    p.stderr.close()
        finally:
            unit.close()

    def test_actual_cli_normal_console_output_and_save(self):
        unit = FirmwareUnit(self.lib)
        self.lib.model_automatic(unit.model)
        try:
            with tempfile.TemporaryDirectory() as tmp:
                code = """
import sys
sys.path.insert(0, 'tools')
import button_calibrate as bc
from serial_port import Serial
bc.Serial = lambda port: Serial(port, lock_path=sys.argv[2])
sys.exit(bc.main(['--port', sys.argv[1], '--timeout', '.5']))
"""
                p = subprocess.run([sys.executable, '-u', '-c', code, unit.port, str(Path(tmp) / 'lock')],
                                   cwd=ROOT, input=b'START\nSAVE BUTTONS\n', capture_output=True, timeout=8)
                self.assertEqual(p.returncode, 0, p.stderr)
                self.assertIn(b'Type START', p.stdout)
                self.assertIn(b'Type SAVE BUTTONS', p.stdout)
                for value in VALUES:
                    self.assertIn(f'median {value:04d}'.encode(), p.stdout)
                self.assertTrue(any(r[0] == 'buttons save' for r in unit.records))
                self.assertFalse(any(r[0] == 'buttons cancel' for r in unit.records))
        finally:
            unit.close()

    def test_partial_wizard_cancel_reboot_and_profile_conflict_preserve_defaults(self):
        from serial_port import Serial
        unit = FirmwareUnit(self.lib)
        try:
            with tempfile.TemporaryDirectory() as tmp, Serial(unit.port, lock_path=str(Path(tmp) / 'lock')) as link:
                link.request(b'buttons start\n', b'ok buttons\r\n')
                self.lib.model_hold(unit.model, 0, 120)
                self.lib.model_hold(unit.model, 3900, 350)
                link.request(b'buttons status\n', b'value L 3900\r\n')
                link.request(b'buttons cancel\n', b'ok cancel\r\n')
                self.lib.model_reboot(unit.model)
                link.request(b'buttons get\n', b'buttons none\r\n')
                link.request(b'buttons stored L\n', b'err config\r\n')
                link.request(b'profile 1234abcd\n', b'ok profile\r\n')
                link.request(b'buttons start\n', b'err busy\r\n')
                link.request(b'profile set 1234abcd 2\n', b'ok 2.00\r\n')
                self.lib.model_hold(unit.model, 2500, 100)
                self.lib.model_hold(unit.model, 1100, 100)
                link.request(b'get\n', b'2.00\r\n')
            self.assertFalse(unit.errors)
        finally:
            unit.close()

    def test_legacy_production_parser_is_inert(self):
        from serial_port import Serial
        unit = FirmwareUnit(self.lib, legacy=True)
        try:
            with tempfile.TemporaryDirectory() as tmp, Serial(unit.port, lock_path=str(Path(tmp) / 'lock')) as link:
                session = bc.Calibration(link, .5, ask=lambda p, s: self.fail('legacy prompt'), output=lambda s: None)
                with self.assertRaisesRegex(ValueError, 'lacks'):
                    session.run()
                session.cancel()
            self.assertEqual(unit.records, [('buttons', b'err\r\n', 0)])
        finally:
            unit.close()

    def test_future_revision_over_pty_never_starts_or_cancels_a_session(self):
        from serial_port import Serial
        unit = FirmwareUnit(self.lib)
        self.lib.model_future(unit.model)
        try:
            with tempfile.TemporaryDirectory() as tmp, Serial(unit.port, lock_path=str(Path(tmp) / 'lock')) as link:
                session = bc.Calibration(link, .5, ask=lambda p, s: self.fail('future prompt'), output=lambda s: None)
                with self.assertRaisesRegex(ValueError, 'lacks'):
                    session.run()
                session.cancel()
            self.assertEqual(unit.records, [('buttons', b'buttons 2\r\n', 0)])
        finally:
            unit.close()


if __name__ == '__main__':
    unittest.main()
