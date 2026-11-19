#!/usr/bin/env python3
"""Linux USB attenuator control. No device access occurs on import."""
import argparse
from contextlib import contextmanager
from datetime import datetime, timezone
from decimal import Decimal, InvalidOperation
import fcntl
import json
import io
import os
from pathlib import Path
import re
import select
import signal
import stat
import sys
import termios
import time

LOCK_PATH = '/run/lock/attctl.lock'
MAX_FILE = 65536


def db(value):
    if isinstance(value, bool) or not isinstance(value, (str, int, Decimal)):
        raise ValueError('dB must be a decimal number')
    if len(str(value)) > 64:
        raise ValueError('dB text exceeds 64 characters')
    try:
        n = Decimal(value)
        if not n.is_finite() or abs(n.as_tuple().exponent) > 64:
            raise ValueError('dB exponent exceeds 64')
        if not n.is_finite() or not 0 <= n <= Decimal('31.75') or all(n != Decimal(q) / 4 for q in range(128)):
            raise ValueError('dB must be 0..31.75 in exact 0.25 steps')
        return '0.00' if n == 0 else format(n, '.2f')
    except InvalidOperation as exc:
        raise ValueError('invalid dB') from exc


def load_json(path):
    def pairs(items):
        result = {}
        for key, value in items:
            if key in result:
                raise ValueError('duplicate JSON key: ' + key)
            result[key] = value
        return result
    with open(path, 'rb') as source:
        raw = source.read(MAX_FILE + 1)
    if len(raw) > MAX_FILE:
        raise ValueError('JSON file exceeds 65536 bytes')
    return json.loads(raw, parse_float=Decimal, parse_constant=lambda _: (_ for _ in ()).throw(ValueError('nonfinite JSON')), object_pairs_hook=pairs)


def config(path):
    c = load_json(path)
    if not isinstance(c, dict) or set(c) != {'version', 'devices'} or type(c['version']) is not int or c['version'] != 1:
        raise ValueError('config requires version 1 and devices')
    devices = c['devices']
    if not isinstance(devices, dict) or set(devices) != {'A', 'B'}:
        raise ValueError('config requires exactly A and B')
    for item in devices.values():
        if not isinstance(item, dict) or set(item) != {'usb_path', 'vid', 'pid'}:
            raise ValueError('device requires usb_path, vid, pid')
        if not isinstance(item['usb_path'], str) or not re.fullmatch(r'[0-9]+-[0-9]+(?:\.[0-9]+)*', item['usb_path']):
            raise ValueError('invalid USB topology path')
        if (item['vid'], item['pid']) != ('1a86', '7523'):
            raise ValueError('expected CH340 1a86:7523')
    if devices['A']['usb_path'] == devices['B']['usb_path']:
        raise ValueError('A and B must have distinct USB paths')
    return devices


def profile(path):
    p = load_json(path)
    if not isinstance(p, dict) or set(p) != {'version', 'steps'} or type(p['version']) is not int or p['version'] != 1:
        raise ValueError('profile requires version 1 and steps')
    if not isinstance(p['steps'], list) or not 1 <= len(p['steps']) <= 1024:
        raise ValueError('profile requires 1..1024 steps')
    result, total = [], 0
    for step in p['steps']:
        if not isinstance(step, dict) or set(step) != {'A', 'B', 'duration'}:
            raise ValueError('step requires A, B, duration')
        duration = step['duration']
        if isinstance(duration, bool) or not isinstance(duration, (int, Decimal)):
            raise ValueError('duration must be a finite number')
        duration = Decimal(duration)
        if not duration.is_finite() or not Decimal('0.05') <= duration <= 3600:
            raise ValueError('duration must be 0.05..3600 seconds')
        if duration != Decimal(format(duration, '.3f')):
            raise ValueError('duration resolution is 0.001 seconds')
        total += int(duration * 1000)
        result.append({'A': db(step['A']), 'B': db(step['B']), 'duration': float(duration)})
    if total > 7200000:
        raise ValueError('profile exceeds 7200 seconds')
    return result


class Resolver:
    def __init__(self, devices, sysfs=Path('/sys/class/tty'), dev=Path('/dev')):
        self.devices, self.sysfs, self.dev = devices, sysfs, dev

    def resolve(self, label):
        spec = self.devices[label]
        matches = []
        for tty in self.sysfs.glob('ttyUSB*'):
            target = (tty / 'device').resolve(strict=True)
            for parent in (target, *target.parents):
                if parent.name != spec['usb_path'] or not (parent / 'idVendor').exists():
                    continue
                if ((parent / 'idVendor').read_text().strip(), (parent / 'idProduct').read_text().strip()) != (spec['vid'], spec['pid']):
                    raise ValueError(label + ': USB identity changed')
                port = str(self.dev / tty.name)
                info = os.stat(port)
                if not stat.S_ISCHR(info.st_mode):
                    raise ValueError('serial path is not a character device')
                # bus/dev number catches replacement or re-enumeration at the same socket.
                matches.append((port, str(target), info.st_rdev,
                                (parent / 'busnum').read_text().strip(),
                                (parent / 'devnum').read_text().strip()))
        if len(matches) != 1:
            raise ValueError(label + ': missing or ambiguous USB binding')
        return matches[0]


@contextmanager
def locked(path=LOCK_PATH):
    fd = os.open(path, os.O_RDWR | os.O_CREAT | os.O_NOFOLLOW | os.O_NONBLOCK, 0o600)
    try:
        s = os.fstat(fd)
        if not stat.S_ISREG(s.st_mode) or s.st_nlink != 1 or s.st_uid not in (0, os.geteuid()):
            raise ValueError('unsafe lock file ownership/type')
        try:
            fcntl.flock(fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError as exc:
            raise RuntimeError('another attctl controller holds the lock') from exc
        yield
    finally:
        os.close(fd)  # Never unlink or replace a live lock inode.


class Serial:
    def __init__(self, resolver, label, timeout=2):
        self.resolver, self.label, self.timeout = resolver, label, timeout
        self.binding = resolver.resolve(label)
        self.fd = os.open(self.binding[0], os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK | os.O_NOFOLLOW)
        self.saved = None
        self.exclusive = False
        try:
            self.check()
            fcntl.flock(self.fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
            self.check_holders()
            fcntl.ioctl(self.fd, termios.TIOCEXCL)
            self.exclusive = True
            self.check()
            self.check_holders()
            self.saved = termios.tcgetattr(self.fd)
            attrs = termios.tcgetattr(self.fd)
            attrs[0] = attrs[1] = attrs[3] = 0
            attrs[2] = termios.CLOCAL | termios.CREAD | termios.CS8
            attrs[4] = attrs[5] = termios.B115200
            attrs[6][termios.VMIN] = attrs[6][termios.VTIME] = 0
            termios.tcsetattr(self.fd, termios.TCSANOW, attrs)
            termios.tcflush(self.fd, termios.TCIOFLUSH)
        except BaseException:
            self.close()
            raise

    def check_holders(self):
        # TIOCEXCL prevents future opens; /proc detects pre-existing holders.
        # Fail closed if Linux process visibility cannot establish ownership.
        deadline, inspected = time.monotonic() + self.timeout, 0
        if not Path('/proc/self/fd').is_dir():
            raise RuntimeError('Linux /proc process visibility required')
        for process in Path('/proc').glob('[0-9]*'):
            if time.monotonic() > deadline:
                self.saved = None
                raise TimeoutError('serial holder inspection bound exceeded')
            if int(process.name) == os.getpid():
                continue
            try:
                for entry in (process / 'fd').iterdir():
                    inspected += 1
                    if inspected > 100000 or time.monotonic() > deadline:
                        self.saved = None
                        raise TimeoutError('serial holder inspection bound exceeded')
                    try:
                        info = entry.stat()
                    except FileNotFoundError:
                        continue
                    if stat.S_ISCHR(info.st_mode) and info.st_rdev == self.binding[2]:
                        self.saved = None
                        raise RuntimeError(self.label + ': serial device held by another process')
            except FileNotFoundError:
                continue
            except PermissionError as exc:
                self.saved = None
                raise RuntimeError('cannot establish serial ownership; use an authorized account with /proc visibility') from exc

    def check(self):
        if self.resolver.resolve(self.label) != self.binding or os.fstat(self.fd).st_rdev != self.binding[2]:
            self.saved = None
            raise RuntimeError(self.label + ': USB binding changed; no further writes')

    def write(self, data, deadline):
        while data:
            self.check()
            self.check_holders()
            left = deadline - time.monotonic()
            if left <= 0 or not select.select([], [self.fd], [], left)[1]:
                raise TimeoutError('serial write timeout')
            try:
                n = os.write(self.fd, data)
            except BlockingIOError:
                continue
            if n <= 0:
                raise OSError('serial disconnected')
            data = data[n:]

    def request(self, command, expected=None):
        deadline = time.monotonic() + self.timeout
        self.write(command.encode('ascii') + b'\n', deadline)
        response = bytearray()
        while not response.endswith(b'\n'):
            left = deadline - time.monotonic()
            if left <= 0 or not select.select([self.fd], [], [], left)[0]:
                raise TimeoutError('serial reply timeout; application unknown')
            try:
                ch = os.read(self.fd, 1)
            except BlockingIOError:
                continue
            if not ch:
                raise OSError('serial disconnected')
            response.extend(ch)
            if len(response) > 64:
                raise ValueError('oversized serial reply')
        self.check()
        reply = bytes(response)
        if expected is not None:
            if reply != expected.encode('ascii') + b'\r\n':
                raise ValueError('reply mismatch: ' + repr(reply))
            return expected.removeprefix('ok ')
        if not re.fullmatch(rb'(?:[0-9]|[12][0-9]|3[01])\.(?:00|25|50|75)\r\n', reply):
            raise ValueError('invalid GET reply: ' + repr(reply))
        return db(reply[:-2].decode('ascii'))

    def set(self, value):
        return self.request('set ' + value, 'ok ' + value)

    def zero(self):
        self.check()
        self.check_holders()
        # Invalidate any partial command; never complete/replay a nonzero command.
        termios.tcflush(self.fd, termios.TCIOFLUSH)
        self.write(b'!\n', time.monotonic() + self.timeout)
        deadline = time.monotonic() + 0.2
        count = 0
        while time.monotonic() < deadline:
            left = deadline - time.monotonic()
            if not select.select([self.fd], [], [], max(0, left))[0]:
                break
            chunk = os.read(self.fd, 64)
            if not chunk:
                raise OSError('serial disconnected during recovery')
            count += len(chunk)
            if count > 256:
                raise ValueError('excess serial data during recovery')
        termios.tcflush(self.fd, termios.TCIFLUSH)
        self.set('0.00')
        if self.request('get') != '0.00':
            raise ValueError('zero GET mismatch')

    def close(self):
        try:
            if self.saved is not None:
                termios.tcsetattr(self.fd, termios.TCSANOW, self.saved)
        finally:
            try:
                if self.exclusive:
                    fcntl.ioctl(self.fd, termios.TIOCNXCL)
            finally:
                os.close(self.fd)


def bounded_output(stream, record):
    raw = (json.dumps(record, allow_nan=False) + '\n').encode('utf-8')
    if len(raw) > 4096:
        raise ValueError('log record exceeds 4096 bytes')
    if isinstance(stream, io.StringIO):  # In-memory test capture; no external I/O.
        stream.write(raw.decode('utf-8'))
        return
    fd = stream.fileno()
    was_blocking = os.get_blocking(fd)
    deadline = time.monotonic() + 0.1
    try:
        os.set_blocking(fd, False)
        while raw:
            left = deadline - time.monotonic()
            if left <= 0 or not select.select([], [fd], [], left)[1]:
                raise TimeoutError('log output backpressure')
            try:
                n = os.write(fd, raw)
            except BlockingIOError:
                continue
            if n <= 0:
                raise OSError('log output closed')
            raw = raw[n:]
    finally:
        os.set_blocking(fd, was_blocking)


def emit(event, **fields):
    bounded_output(sys.stdout, {'utc': datetime.now(timezone.utc).isoformat(), 'event': event, **fields})


def report(event, **fields):
    """Output backpressure must never hold cleanup hostage."""
    try:
        emit(event, **fields)
        return True
    except Exception as exc:
        try:
            bounded_output(sys.stderr, {'utc': datetime.now(timezone.utc).isoformat(), 'event': event, **fields, 'logging_error': str(exc)})
        except Exception:
            pass
        return False


class Interrupted(Exception):
    pass


def interrupt(signum, _frame):
    raise Interrupted('signal ' + str(signum))


def run(devices, target, action=None, value=None, steps=None, resolver=None, lock_path=LOCK_PATH, timeout=2):
    resolver = resolver or Resolver(devices)
    labels = ('A', 'B') if steps is not None or target == 'both' else (target,)
    links, failed = {}, False
    error, reports = None, []
    with locked(lock_path):
        handlers = {s: signal.signal(s, interrupt) for s in (signal.SIGINT, signal.SIGTERM, signal.SIGHUP)}
        try:
            # Preflight all bindings before opening either port.
            for label in labels:
                resolver.resolve(label)
            for label in labels:
                links[label] = Serial(resolver, label, timeout)
            if steps is None:
                for label in labels:
                    actual = links[label].set(value) if action == 'set' else links[label].request('get')
                    emit('applied' if action == 'set' else 'read', unit=label, value=actual, status='acknowledged')
            else:
                for index, step in enumerate(steps):
                    actual = {}
                    for label in labels:
                        actual[label] = links[label].set(step[label])
                        emit('applied', step=index, unit=label, value=actual[label], status='acknowledged')
                    started = time.monotonic()
                    emit('step', step=index, values=actual, duration=step['duration'], status='acknowledged')
                    # Start each duration after both acks; late steps never catch up.
                    deadline = started + step['duration']
                    while time.monotonic() < deadline:
                        time.sleep(min(0.1, max(0, deadline - time.monotonic())))
                    emit('held', step=index, elapsed=time.monotonic() - started)
        except Exception as exc:
            failed = True
            error = str(exc)
        finally:
            # Further signals cannot interrupt the bounded independent cleanup attempts.
            for s in handlers:
                signal.signal(s, signal.SIG_IGN)
            if steps is not None and links:
                for label in ('A', 'B'):
                    try:
                        if label not in links:
                            raise RuntimeError('port not owned; no cleanup write attempted')
                        links[label].zero()
                    except Exception as exc:
                        failed = True
                        reports.append(('cleanup', {'unit': label, 'status': 'failed', 'error': str(exc)}))
                    else:
                        reports.append(('cleanup', {'unit': label, 'value': '0.00', 'status': 'zero_confirmed'}))
            for label, link in links.items():
                try:
                    link.close()
                except Exception as exc:
                    failed = True
                    reports.append(('close', {'unit': label, 'status': 'failed', 'error': str(exc)}))
            # No output operation precedes or separates the independent zero attempts.
            if error is not None:
                reports.append(('error', {'error': error, 'status': 'failed'}))
            for event, fields in reports:
                if not report(event, **fields):
                    failed = True
            for s, handler in handlers.items():
                signal.signal(s, handler)
    return 1 if failed else 0


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--config', default='/etc/attctl.json')
    sub = parser.add_subparsers(dest='target', required=True)
    for label in ('A', 'B', 'both'):
        operations = sub.add_parser(label).add_subparsers(dest='action', required=True)
        operations.add_parser('set').add_argument('value')
        if label != 'both':
            operations.add_parser('get')
    sub.add_parser('profile').add_argument('file')
    args = parser.parse_args(argv)
    try:
        devices = config(args.config)
        steps = profile(args.file) if args.target == 'profile' else None
        value = db(args.value) if getattr(args, 'action', None) == 'set' else None
        return run(devices, args.target, getattr(args, 'action', None), value, steps)
    except Exception as exc:
        report('error', status='failed', error=str(exc))
        return 1


if __name__ == '__main__':
    sys.exit(main())
