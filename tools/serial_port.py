"""Bounded pyserial transport; importing this module never opens hardware."""
from contextlib import ExitStack
import math
import sys
import time


def timeout_seconds(value):
    seconds = float(value)
    if not math.isfinite(seconds) or not 0 < seconds <= 30:
        raise ValueError('timeout must be finite and in (0, 30] seconds')
    return seconds


class Serial:
    def __init__(self, port, *, lock_path=None):
        if not isinstance(port, str) or not port.strip() or len(port) > 1024 or any(ord(c) < 32 for c in port):
            raise ValueError('an exact serial port name is required')
        # Native names only: serial_for_url would also permit network transports.
        if '://' in port:
            raise ValueError('serial URLs are not supported')
        try:
            import serial
        except ImportError as exc:
            raise RuntimeError('install pyserial: python -m pip install -r requirements.txt') from exc
        self.resources = ExitStack()
        self.failed = False
        try:
            if sys.platform.startswith('linux'):
                # Share the installed Linux controller's persistent lock inode.
                # Acquire BEFORE opening/configuring/flushing any serial port.
                from attctl import locked, LOCK_PATH
                self.resources.enter_context(locked(LOCK_PATH if lock_path is None else lock_path))
            options = dict(port=None, baudrate=115200, bytesize=8, parity='N', stopbits=1,
                           timeout=5, write_timeout=5, xonxoff=False, rtscts=False, dsrdtr=False)
            if sys.platform != 'win32':
                options['exclusive'] = True
            self.port = serial.Serial(**options)
            self.resources.callback(self.port.close)
            self.port.dtr = self.port.rts = False
            self.port.port = port
            self.port.open()
            self.port.reset_input_buffer()
        except BaseException:
            self.resources.close()
            raise

    def request(self, command, expected=None, seconds=5):
        seconds = timeout_seconds(seconds)
        if self.failed:
            raise RuntimeError('UART session failed; close and inspect before another operation')
        deadline = time.monotonic() + seconds
        try:
            pending = command
            while pending:
                left = deadline - time.monotonic()
                if left <= 0:
                    raise TimeoutError('UART write timeout; completion unknown; stop without replay')
                self.port.write_timeout = left
                count = self.port.write(pending)
                if not count:
                    raise TimeoutError('UART write timeout; completion unknown; stop without replay')
                pending = pending[count:]
            response = bytearray()
            while not response.endswith(b'\n'):
                left = deadline - time.monotonic()
                if left <= 0:
                    raise TimeoutError('UART reply timeout; completion unknown; stop without replay')
                self.port.timeout = left
                chunk = self.port.read(1)
                if not chunk:
                    raise TimeoutError('UART reply timeout; completion unknown; stop without replay')
                response.extend(chunk)
                if len(response) > 64:
                    raise ValueError('oversized UART reply')
            reply = bytes(response)
            if time.monotonic() > deadline:
                raise TimeoutError('UART reply timeout; completion unknown; stop without replay')
            if expected is not None and reply != expected:
                raise ValueError('unexpected UART reply: ' + repr(reply))
            return reply
        except BaseException:
            self.failed = True
            raise

    def close(self):
        self.resources.close()

    def __enter__(self):
        return self

    def __exit__(self, *args):
        self.close()
