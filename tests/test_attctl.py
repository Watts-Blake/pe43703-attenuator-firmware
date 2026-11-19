"""Offline serial behavior uses local PTYs only; never physical devices."""
from contextlib import contextmanager
from decimal import Decimal
import fcntl
import importlib.util
import io
import json
import os
from pathlib import Path
import pty
import signal
import subprocess
import sys
import termios
import tempfile
import threading
import time
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location('attctl', Path(__file__).resolve().parents[1] / 'tools/attctl.py')
a = importlib.util.module_from_spec(spec)
spec.loader.exec_module(a)


class FakeResolver:
    def __init__(self, ports):
        self.ports = ports
        self.changed = set()

    def resolve(self, label):
        port = self.ports[label]
        return (port, 'changed' if label in self.changed else label, os.stat(port).st_rdev, '1', '2')


class Unit:
    def __init__(self, mode='normal'):
        self.master, self.slave = pty.openpty()
        self.port = os.ttyname(self.slave)
        self.mode, self.value, self.commands = mode, '0.00', []
        self.stop = threading.Event()
        self.worker = threading.Thread(target=self.serve, daemon=True)
        self.worker.start()

    def serve(self):
        pending = b''
        while not self.stop.is_set():
            import select
            if not select.select([self.master], [], [], .02)[0]:
                continue
            try:
                chunk = os.read(self.master, 1024)
            except OSError:
                break
            pending += chunk
            while b'\n' in pending:
                command, pending = pending.split(b'\n', 1)
                text = command.decode('ascii', errors='replace')
                self.commands.append(text)
                if text.startswith('set '):
                    self.value = text[4:]
                    reply = ('ok ' + self.value + '\r\n').encode()
                elif text == 'get':
                    reply = (self.value + '\r\n').encode()
                else:
                    reply = b'err\r\n'
                if text.startswith('set ') and text != 'set 0.00':
                    if self.mode == 'timeout':
                        continue
                    if self.mode == 'mismatch':
                        reply = b'ok 9.00\r\n'
                    if self.mode == 'oversize':
                        reply = b'x' * 65 + b'\n'
                    if self.mode == 'disconnect':
                        os.close(self.master)
                        self.master = -1
                        return
                try:
                    if self.mode == 'fragment':
                        for ch in reply:
                            os.write(self.master, bytes([ch]))
                            time.sleep(.001)
                    else:
                        os.write(self.master, reply)
                except OSError:
                    return

    def close(self):
        self.stop.set()
        self.worker.join(1)
        if self.master >= 0:
            os.close(self.master)
        os.close(self.slave)


class Tests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        self.units = [Unit(), Unit()]
        self.resolver = FakeResolver(dict(zip(('A', 'B'), (u.port for u in self.units))))
        self.output = io.StringIO()

    def tearDown(self):
        for unit in self.units:
            unit.close()
        self.temp.cleanup()

    def run_cli(self, **kw):
        with patch.object(a.Serial, 'check_holders'), patch('sys.stdout', self.output):
            return a.run({}, resolver=self.resolver, lock_path=str(self.root / 'lock'), timeout=.12, **kw)

    def file(self, data):
        path = self.root / 'input.json'
        path.write_text(json.dumps(data))
        return path

    def test_values_and_strict_profile(self):
        for n in ('0', '-0', '31.75', '2.25'):
            self.assertEqual(a.db(n), format(Decimal(n).copy_abs(), '.2f'))
        for n in ('nan', 'Infinity', '-.25', '32', '0.25000000000000000000000000001', '1e-10000000', True, 1.2):
            with self.assertRaises(ValueError, msg=str(n)):
                a.db(n)
        valid = {'version': 1, 'steps': [{'A': '12.00', 'B': 0, 'duration': 60}] * 20}
        self.assertEqual(len(a.profile(self.file(valid))), 20)
        for duration in (0, -1, 3601, True, '60', float('inf'), .0001, .05001):
            with self.assertRaises(ValueError):
                a.profile(self.file({'version': 1, 'steps': [{'A': 0, 'B': 0, 'duration': duration}]}))
        for data in ({'version': 1, 'steps': []}, {'version': True, 'steps': []}, {'version': 1, 'steps': [{}] * 1025}, {'version': 1, 'steps': [{'A': 0, 'B': 0, 'duration': 3600}] * 3}):
            with self.assertRaises(ValueError):
                a.profile(self.file(data))
        path = self.root / 'input.json'
        for raw in ('{"version":1,"version":1}', 'x' * 65537, '{"n":NaN}'):
            path.write_text(raw)
            with self.assertRaises(ValueError):
                a.load_json(path)
        self.assertEqual(self.units[0].commands, [])

    def test_direct_set_persists_and_get_fragmentation(self):
        self.units[0].mode = 'fragment'
        self.assertEqual(self.run_cli(target='A', action='set', value='31.75'), 0)
        self.assertEqual(self.units[0].value, '31.75')
        self.assertEqual(self.run_cli(target='A', action='get'), 0)
        self.assertEqual(self.units[0].commands, ['set 31.75', 'get'])

    def test_serial_failure_types_bounded(self):
        for mode in ('mismatch', 'timeout', 'oversize', 'disconnect'):
            with self.subTest(mode=mode):
                unit = Unit(mode)
                resolver = FakeResolver({'A': unit.port})
                with patch.object(a.Serial, 'check_holders'):
                    link = a.Serial(resolver, 'A', .1)
                    start = time.monotonic()
                    try:
                        with self.assertRaises((ValueError, TimeoutError, OSError)):
                            link.set('10.00')
                        self.assertLess(time.monotonic() - start, .5)
                    finally:
                        try:
                            link.close()
                        except OSError:
                            pass
                        unit.close()

    def test_profile_holds_and_cleans_independently(self):
        steps = [{'A': '10.00', 'B': '20.00', 'duration': .05}, {'A': '15.00', 'B': '0.00', 'duration': .05}]
        started = time.monotonic()
        self.assertEqual(self.run_cli(target='profile', steps=steps), 0)
        self.assertGreaterEqual(time.monotonic() - started, .5)  # holds plus recovery drains
        for unit in self.units:
            self.assertEqual(unit.value, '0.00')
            self.assertEqual(unit.commands[-3:], ['!', 'set 0.00', 'get'])
        events = [json.loads(line) for line in self.output.getvalue().splitlines()]
        self.assertEqual(len([e for e in events if e['event'] == 'step']), 2)
        self.assertTrue(all(e['elapsed'] >= .05 for e in events if e['event'] == 'held'))
        self.units[0].mode = 'mismatch'
        self.assertEqual(self.run_cli(target='profile', steps=steps), 1)
        self.assertEqual(self.units[0].value, '0.00')
        self.assertEqual(self.units[1].value, '0.00')

    def test_cleanup_failure_does_not_skip_other_unit(self):
        original = a.Serial.zero
        def fail_first(link):
            if link.label == 'A':
                raise OSError('disconnected')
            original(link)
        with patch.object(a.Serial, 'zero', fail_first):
            self.assertEqual(self.run_cli(target='profile', steps=[{'A': '10.00', 'B': '12.00', 'duration': .05}]), 1)
        self.assertEqual(self.units[1].value, '0.00')
        self.assertIn('"status": "failed"', self.output.getvalue())

    def test_logging_failure_and_interrupt_cleanup(self):
        for failure in (BrokenPipeError('log closed'), a.Interrupted('signal')):
            with patch.object(a, 'emit', side_effect=failure), patch('sys.stderr', io.StringIO()):
                self.assertEqual(self.run_cli(target='profile', steps=[{'A': '10.00', 'B': '12.00', 'duration': .05}]), 1)
            self.assertEqual([u.value for u in self.units], ['0.00', '0.00'])

    def test_lock_refusal_no_writes(self):
        with a.locked(str(self.root / 'lock')):
            with self.assertRaises(RuntimeError):
                self.run_cli(target='both', action='set', value='10.00')
        self.assertEqual([u.commands for u in self.units], [[], []])

    def test_partial_open_failure_cleans_owned_only(self):
        real_serial = a.Serial
        def open_link(resolver, label, timeout):
            if label == 'B':
                raise RuntimeError('foreign owner')
            return real_serial(resolver, label, timeout)
        with patch.object(a.Serial, 'check_holders'), patch.object(a, 'Serial', side_effect=open_link), patch('sys.stdout', self.output):
            result = a.run({}, target='profile', steps=[{'A': '10.00', 'B': '12.00', 'duration': .05}], resolver=self.resolver, lock_path=str(self.root / 'lock'), timeout=.12)
        self.assertEqual(result, 1)
        self.assertEqual(self.units[0].commands, ['!', 'set 0.00', 'get'])
        self.assertEqual(self.units[1].commands, [])
        self.assertIn('port not owned', self.output.getvalue())

    def test_concurrent_process_lock_refusal(self):
        code = "import sys,time;sys.path.insert(0,'tools');import attctl as a\nwith a.locked(sys.argv[1]):\n print('locked',flush=True);time.sleep(2)"
        child = subprocess.Popen([sys.executable, '-c', code, str(self.root / 'lock')], cwd=Path(__file__).resolve().parents[1], stdout=subprocess.PIPE, text=True)
        try:
            self.assertEqual(child.stdout.readline().strip(), 'locked')
            with self.assertRaises(RuntimeError):
                self.run_cli(target='both', action='set', value='10.00')
            self.assertEqual([u.commands for u in self.units], [[], []])
        finally:
            child.terminate(); child.wait(timeout=3); child.stdout.close()

    def test_changed_binding_no_write_and_cleanup_other(self):
        original = a.Serial.set
        def change(link, value):
            result = original(link, value)
            if link.label == 'A':
                self.resolver.changed.add('A')
            return result
        with patch.object(a.Serial, 'set', change):
            self.assertEqual(self.run_cli(target='profile', steps=[{'A': '10.00', 'B': '12.00', 'duration': .05}]), 1)
        self.assertEqual(self.units[0].commands, ['set 10.00'])
        self.assertEqual(self.units[1].value, '0.00')

    def test_real_signals_cleanup_and_no_catchup(self):
        for signum in (signal.SIGINT, signal.SIGTERM, signal.SIGHUP):
            timer = threading.Timer(.1, lambda s=signum: os.kill(os.getpid(), s))
            timer.start()
            try:
                self.assertEqual(self.run_cli(target='profile', steps=[{'A': '10.00', 'B': '12.00', 'duration': 5}]), 1)
            finally:
                timer.join()
            self.assertEqual([u.value for u in self.units], ['0.00', '0.00'])

    def test_oversized_reply_recovers_toward_zero_only(self):
        self.units[0].mode = 'oversize'
        self.assertEqual(self.run_cli(target='profile', steps=[{'A': '10.00', 'B': '12.00', 'duration': .05}]), 1)
        self.assertEqual([u.value for u in self.units], ['0.00', '0.00'])
        self.assertEqual(self.units[0].commands, ['set 10.00', '!', 'set 0.00', 'get'])

    @unittest.skipUnless(Path('/proc/self/fd').is_dir(), 'Linux /proc required')
    def test_raw_foreign_holder_refused_without_termios_change(self):
        saved = termios.tcgetattr(self.units[0].slave)
        # The test process holds a raw slave fd without flock. A child must refuse it.
        code = """import sys; sys.path.insert(0, 'tools'); import attctl as a, os
from unittest.mock import patch
class Resolver:
 def resolve(self,label): return (sys.argv[1], 'A', os.stat(sys.argv[1]).st_rdev,'1','2')
try:
 with patch.object(a.Path, 'glob', return_value=[a.Path('/proc/' + sys.argv[2])]):
  a.Serial(Resolver(),'A',.5)
except RuntimeError as e:
 print(str(e)); sys.exit(0 if 'held by another process' in str(e) else 2)
sys.exit(3)
"""
        result = subprocess.run([sys.executable, '-c', code, self.units[0].port, str(os.getpid())], cwd=Path(__file__).resolve().parents[1], capture_output=True, timeout=3)
        self.assertEqual(result.returncode, 0, result.stderr + result.stdout)
        self.assertEqual(termios.tcgetattr(self.units[0].slave), saved)
        self.assertEqual(self.units[0].commands, [])

    def test_config_rejects_duplicate_path_and_bad_vid(self):
        devices = {label: {'usb_path': path, 'vid': '1a86', 'pid': '7523'} for label, path in [('A', '2-3.4'), ('B', '2-3.5')]}
        self.assertEqual(a.config(self.file({'version': 1, 'devices': devices})), devices)
        devices['B']['usb_path'] = devices['A']['usb_path']
        with self.assertRaises(ValueError): a.config(self.file({'version': 1, 'devices': devices}))
        devices['B']['usb_path'] = '2-3.5'; devices['B']['vid'] = 'ffff'
        with self.assertRaises(ValueError): a.config(self.file({'version': 1, 'devices': devices}))

    def test_resolver_renumber_missing_duplicate_and_identity(self):
        sysfs, dev = self.root / 'tty', self.root / 'dev'
        sysfs.mkdir(); dev.mkdir()
        usb = self.root / 'usb' / '2-3.4'
        target = usb / '2-3.4:1.0' / 'ttyUSB9'
        target.mkdir(parents=True)
        for name, value in {'idVendor': '1a86', 'idProduct': '7523', 'busnum': '2', 'devnum': '8'}.items():
            (usb / name).write_text(value)
        def add(name):
            (sysfs / name).mkdir()
            (sysfs / name / 'device').symlink_to(target)
            (dev / name).symlink_to(self.units[0].port)
        add('ttyUSB9')
        resolver = a.Resolver({'A': {'usb_path': '2-3.4', 'vid': '1a86', 'pid': '7523'}}, sysfs, dev)
        self.assertTrue(resolver.resolve('A')[0].endswith('ttyUSB9'))
        (sysfs / 'ttyUSB9').rename(sysfs / 'ttyUSB42')
        (dev / 'ttyUSB9').rename(dev / 'ttyUSB42')
        self.assertTrue(resolver.resolve('A')[0].endswith('ttyUSB42'))
        add('ttyUSB8')
        with self.assertRaises(ValueError): resolver.resolve('A')
        (sysfs / 'ttyUSB8' / 'device').unlink(); (sysfs / 'ttyUSB8').rmdir()
        (usb / 'idVendor').write_text('ffff')
        with self.assertRaises(ValueError): resolver.resolve('A')
        (sysfs / 'ttyUSB42' / 'device').unlink(); (sysfs / 'ttyUSB42').rmdir()
        with self.assertRaises(ValueError): resolver.resolve('A')


if __name__ == '__main__':
    unittest.main()
