"""Explicit five-button calibration after OLED setup, using an exact serial port."""
import argparse
import re
import sys
import time
from display_identify import timed_input
from serial_port import Serial, timeout_seconds

CAPABILITY = b'buttons 1\r\n'
KEYS = 'LPMRO'
NAMES = ('LEFT', '+', '-', 'RIGHT', 'OK')
SESSION_SECONDS = 600
PROMPT_SECONDS = 90


class Calibration:
    def __init__(self, link, seconds, *, ask=timed_input, output=print,
                 clock=time.monotonic, sleep=time.sleep):
        self.link, self.seconds = link, seconds
        self.ask, self.output, self.clock, self.sleep = ask, output, clock, sleep
        self.owned = False
        self.deadline = None

    def request(self, command, expected=None, *, deadline=None):
        end = self.deadline if deadline is None else deadline
        seconds = self.seconds if end is None else min(self.seconds, end - self.clock())
        if seconds <= 0:
            raise TimeoutError('Calibration deadline reached; cancelling.')
        return self.link.request((command + '\n').encode('ascii'), expected, seconds=seconds)

    def prompt(self, text):
        left = PROMPT_SECONDS if self.deadline is None else min(PROMPT_SECONDS, self.deadline - self.clock())
        if left <= 1:
            raise TimeoutError('Calibration deadline reached; cancelling.')
        answer = self.ask(text, left - 1)
        if self.deadline is not None and self.clock() >= self.deadline:
            raise TimeoutError('Calibration deadline reached; cancelling.')
        if answer.lower() in ('cancel', 'q', 'quit'):
            raise InterruptedError('Calibration cancelled before save.')
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
        self.request('buttons get', b'buttons saved\r\n')
        values = self.values('stored')
        if captured is not None and values != captured:
            raise ValueError('Saved calibration mismatch; inspect before retrying.')
        self.report(values)
        self.owned = False
        self.output('Saved complete calibration; normal display restored. RF attenuation is unchanged.')

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
            if status == b'saved\r\n':
                # The OLED also permits an explicit one-second OK hold/release.
                self.output('Save confirmed on the device.')
                self.saved()
                return
            if status == b'confirm\r\n':
                captured = self.values('value')
                self.output('Captured complete set (not yet saved):')
                self.report(captured)
                if self.prompt('Type SAVE BUTTONS to persist these five values, or cancel: ') != 'SAVE BUTTONS':
                    raise InterruptedError('Save not confirmed; no save command sent.')
                self.request('buttons save', b'ok saving\r\n')
                end = min(self.deadline, self.clock() + 5)
                while self.clock() < end:
                    status = self.request('buttons status', deadline=end)
                    if status == b'saved\r\n':
                        self.saved(captured)
                        return
                    if status != b'saving\r\n':
                        raise ValueError('Save failed or cancelled: ' + repr(status))
                    self.sleep(.05)
                raise TimeoutError('Save completion unknown; read stored configuration before retrying.')
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
        if self.link.failed:
            self.output('UART failed; no replay or reopen. Firmware cancels within 120 seconds of the last '
                        'completed capture/release, and ten minutes total. A confirmed save may have committed; '
                        'read stored configuration in a new owned session before retrying.')
        else:
            # Cleanup retains its own bound even when the session deadline expired.
            self.request('buttons cancel', b'ok cancel\r\n', deadline=self.clock() + self.seconds)
            self.output('Wizard closed. Previous calibration or an already committed save is retained.')
        self.owned = False


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
        print(str(exc) or 'Calibration interrupted.', file=sys.stderr)
        return 1


if __name__ == '__main__':
    sys.exit(main())
