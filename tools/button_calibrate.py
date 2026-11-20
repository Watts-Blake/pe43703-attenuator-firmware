"""Explicit five-button calibration after OLED setup, using an exact serial port."""
import argparse
import os
import queue
import re
import sys
import threading
import time
from serial_port import Serial, timeout_seconds

CAPABILITY = b'buttons 1\r\n'
KEYS = 'LPMRO'
NAMES = ('LEFT', '+', '-', 'RIGHT', 'OK')
SESSION_SECONDS = 600
PROMPT_SECONDS = 90


class Console:
    """One bounded writer; a blocked OS write never owns the serial thread.

    Raw writes avoid holding Python's buffered-stream lock at interpreter exit.
    The daemon can finish a pending message but cannot send device commands.
    """
    def __init__(self, stream):
        self.fd = stream.fileno()
        self.jobs = queue.Queue(maxsize=1)
        self.failed = False
        threading.Thread(target=self.write, daemon=True).start()

    def write(self):
        while True:
            data, done = self.jobs.get()
            try:
                while data:
                    n = os.write(self.fd, data)
                    if n <= 0:
                        raise OSError('Console write made no progress.')
                    data = data[n:]
                done.put(None)
            except OSError as exc:
                done.put(exc)

    def emit(self, text, seconds=.25):
        if self.failed or seconds <= 0:
            raise TimeoutError('Console unavailable; session closed. A confirmed save may have committed.')
        done = queue.Queue(maxsize=1)
        self.jobs.put_nowait(((text + '\n').encode('utf-8'), done))
        try:
            error = done.get(timeout=min(.25, seconds))
        except queue.Empty as exc:
            self.failed = True
            raise TimeoutError('Console blocked; session closed. A confirmed save may have committed.') from exc
        if error is not None:
            self.failed = True
            raise error


def quiet_input(_prompt, _seconds):
    # Calibration owns the prompt deadline and polls device status while this
    # daemon reads. No input(prompt) flush/write can block the serial owner.
    line = sys.stdin.readline()
    if not line:
        raise EOFError('Console input closed.')
    return line.strip()


class Calibration:
    def __init__(self, link, seconds, *, ask=quiet_input, output=None,
                 clock=time.monotonic, sleep=time.sleep):
        self.link, self.seconds = link, seconds
        self.ask, self.clock, self.sleep = ask, clock, sleep
        self.console = Console(sys.stdout) if output is None else None
        self.output = output if output is not None else self.emit
        self.owned = False
        self.deadline = None
        self.save_pending = False

    def request(self, command, expected=None, *, deadline=None):
        end = self.deadline if deadline is None else deadline
        seconds = self.seconds if end is None else min(self.seconds, end - self.clock())
        if seconds <= 0:
            raise TimeoutError('Calibration deadline reached; cancelling.')
        return self.link.request((command + '\n').encode('ascii'), expected, seconds=seconds)

    def emit(self, text):
        left = .25 if self.deadline is None else self.deadline - self.clock()
        self.console.emit(text, left)

    def prompt(self, text, *, watch=False):
        left = PROMPT_SECONDS if self.deadline is None else min(PROMPT_SECONDS, self.deadline - self.clock())
        if left <= 1:
            raise TimeoutError('Calibration deadline reached; cancelling.')
        self.output(text)
        answers = queue.Queue(maxsize=1)

        def read():
            try:
                answers.put((self.ask('', left - 1), None))
            except BaseException as exc:
                answers.put((None, exc))

        threading.Thread(target=read, daemon=True).start()
        end = self.clock() + left - 1
        while True:
            if watch:
                status = self.request('buttons status', deadline=end)
                if self.completion(status):
                    return None
                if status not in (b'confirm\r\n', b'release O\r\n'):
                    raise ValueError('Calibration stopped: ' + repr(status))
            remaining = end - self.clock()
            if remaining <= 0:
                raise TimeoutError('No answer within the prompt deadline; cancelling.')
            try:
                answer, error = answers.get(timeout=min(.05, remaining))
            except queue.Empty:
                continue
            if error is not None:
                raise error
            if self.deadline is not None and self.clock() >= self.deadline:
                raise TimeoutError('Calibration deadline reached; cancelling.')
            if answer.lower() in ('cancel', 'q', 'quit'):
                raise InterruptedError('Calibration cancelled; an on-device save may have committed.')
            return answer

    def values(self, source):
        values = []
        for key in KEYS:
            reply = self.request(f'buttons {source} {key}')
            match = re.fullmatch(rb'value ([LPMRO]) ([0-9]{4})\r\n', reply)
            if not match or match[1].decode() != key or int(match[2]) > 4095:
                raise ValueError('Invalid calibration readback: ' + repr(reply))
            values.append(int(match[2]))
        # Same fixed +/-32 bands and >32-count guard gaps as firmware.
        if any(v < 97 for v in values) or any(abs(a - b) <= 96 for i, a in enumerate(values) for b in values[:i]):
            raise ValueError('Unsafe calibration readback; inspect before retrying.')
        return values

    def report(self, values):
        for name, value in zip(NAMES, values):
            self.output(f'{name}: median {value:04d}; band {max(0, value - 32):04d}..{min(4095, value + 32):04d}')

    def saved(self, captured=None):
        # Commit is terminal even if readback or reporting fails. Never cancel it.
        self.owned = False
        self.request('buttons get', b'buttons saved\r\n')
        values = self.values('stored')
        if captured is not None and values != captured:
            raise ValueError('Saved calibration mismatch; inspect before retrying.')
        self.report(values)
        self.output('Saved complete calibration; normal display restored. RF attenuation is unchanged.')

    def completion(self, status, captured=None):
        if status not in (b'saving\r\n', b'saved\r\n'):
            return False
        # Once either confirmation has initiated a save, leave it to the bounded
        # firmware transaction even if the host loses completion or is cancelled.
        self.save_pending = True
        end = min(self.deadline, self.clock() + 5)
        while status == b'saving\r\n' and self.clock() < end:
            self.sleep(.05)
            if self.clock() >= end:
                break
            status = self.request('buttons status', deadline=end)
        if status == b'saving\r\n':
            raise TimeoutError('Save completion unknown; read stored configuration before retrying.')
        if status != b'saved\r\n':
            raise ValueError('Save failed or cancelled: ' + repr(status))
        self.saved(captured)
        return True

    def run(self):
        if self.request('buttons') != CAPABILITY:
            raise ValueError('Firmware lacks button calibration protocol 1; no wizard started.')
        config = self.request('buttons get')
        if config not in (b'buttons none\r\n', b'buttons saved\r\n'):
            raise ValueError('Invalid button configuration reply: ' + repr(config))
        if config == b'buttons saved\r\n':
            self.output('Previous saved calibration:')
            self.report(self.values('stored'))
        self.output('Complete screen identification first. Release all keys, then follow the OLED prompts: '
                    'LEFT, +, -, RIGHT, OK. Hold each steadily until its ADC value appears, then release. '
                    'Ctrl-C cancels; incomplete/noisy/overlapping captures save nothing.')
        if self.prompt('Type START to begin the bounded on-device wizard: ') != 'START':
            raise InterruptedError('Start not confirmed; no wizard started.')
        self.deadline = self.clock() + SESSION_SECONDS
        self.request('buttons start', b'ok buttons\r\n')
        self.owned = True
        previous = None
        while self.clock() < self.deadline:
            status = self.request('buttons status')
            if self.completion(status):
                return
            if status == b'confirm\r\n':
                try:
                    captured = self.values('value')
                except ValueError:
                    # Local confirmation can finish during the five reads.
                    if self.completion(self.request('buttons status')):
                        return
                    raise
                if self.completion(self.request('buttons status'), captured):
                    return
                self.output('Captured complete set (not yet saved):')
                self.report(captured)
                answer = self.prompt('Type SAVE BUTTONS to persist these five values, or cancel: ', watch=True)
                if answer is None:
                    return
                # Reconcile even a completed prompt before issuing any mutation.
                status = self.request('buttons status')
                if self.completion(status, captured):
                    return
                if answer != 'SAVE BUTTONS':
                    raise InterruptedError('Save not confirmed; no save command sent.')
                if status != b'confirm\r\n':
                    raise ValueError('Confirmation changed; no save command sent: ' + repr(status))
                self.save_pending = True
                self.request('buttons save', b'ok saving\r\n')
                self.completion(b'saving\r\n', captured)
                return
            if not re.fullmatch(rb'(?:release [LPMRO]|hold [LPMRO]|value [LPMRO] [0-9]{4})\r\n', status):
                raise ValueError('Calibration stopped: ' + repr(status))
            if status != previous:
                self.output(status.decode('ascii').strip())
                previous = status
            self.sleep(.05)
        raise TimeoutError('The ten-minute calibration session ended.')

    def cancel(self):
        if not self.owned:
            return
        try:
            if not self.link.failed and not self.save_pending:
                # A console cancellation may race a local explicit confirmation.
                # Observe it before cancel; firmware also retains committed data
                # if commit happens between this query and the cancel command.
                end = self.clock() + self.seconds
                status = self.request('buttons status', deadline=end)
                if status not in (b'saving\r\n', b'saved\r\n'):
                    self.request('buttons cancel', b'ok cancel\r\n', deadline=self.clock() + self.seconds)
        finally:
            self.owned = False
        # No console I/O in cleanup: closing the transport must be independent of
        # stdout/stderr pressure. Unknown saves require a fresh owned readback.


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', required=True, help='exact identified serial port; no scanning')
    parser.add_argument('--timeout', default='2', help='per-request seconds, >0 and <=30 (default 2)')
    args = parser.parse_args(argv)
    try:
        seconds = timeout_seconds(args.timeout)
        with Serial(args.port) as link:
            session = Calibration(link, seconds)
            try:
                session.run()
            finally:
                session.cancel()
        return 0
    except (ValueError, OSError, RuntimeError, EOFError, KeyboardInterrupt) as exc:
        try:
            Console(sys.stderr).emit((str(exc) or 'Calibration interrupted.') +
                                     ' A confirmed save may have committed; read stored configuration '
                                     'in a new owned session before retrying.')
        except OSError:
            pass
        return 1


if __name__ == '__main__':
    sys.exit(main())
