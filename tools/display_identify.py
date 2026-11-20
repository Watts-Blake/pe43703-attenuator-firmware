"""Choose and explicitly save OLED alignment on Windows, macOS or Linux."""
import argparse
import queue
import re
import sys
import threading
import time
from serial_port import Serial, timeout_seconds

CAPABILITY = b'display 1\r\n'
SESSION_SECONDS = 600
PROMPT_SECONDS = 90


def timed_input(prompt, seconds):
    """Console input with the same bound on Windows and POSIX (no select/TTY)."""
    answers = queue.Queue(maxsize=1)

    def read():
        try:
            answers.put((input(prompt), None))
        except BaseException as exc:
            answers.put((None, exc))

    # Only one prompt at a time. An expired prompt ends the program; its daemon
    # reader cannot keep the process alive or send any serial commands.
    threading.Thread(target=read, daemon=True).start()
    try:
        value, error = answers.get(timeout=seconds)
    except queue.Empty as exc:
        raise TimeoutError('No answer within the prompt deadline; cancelling.') from exc
    if error is not None:
        raise error
    return value.strip()


class Identification:
    def __init__(self, link, seconds, *, ask=timed_input, output=print,
                 clock=time.monotonic, sleep=time.sleep):
        self.link, self.seconds = link, seconds
        self.ask, self.output, self.clock, self.sleep = ask, output, clock, sleep
        self.owned = False
        self.deadline = 0

    def request(self, command, expected=None, *, seconds=None):
        return self.link.request((command + '\n').encode('ascii'), expected,
                                 seconds=self.seconds if seconds is None else seconds)

    def prompt(self, text):
        left = self.deadline - self.clock()
        if left <= 1:
            raise TimeoutError('The ten-minute identification session has ended.')
        answer = self.ask(text, min(PROMPT_SECONDS, left - 1))
        if self.clock() >= self.deadline:
            raise TimeoutError('The ten-minute identification session has ended.')
        if answer.lower() in ('cancel', 'q', 'quit'):
            raise InterruptedError('Identification cancelled; no choice saved.')
        return answer

    def ready(self, index):
        deadline = min(self.deadline, self.clock() + 5)
        while self.clock() < deadline:
            result = self.request('display status', seconds=min(self.seconds, deadline - self.clock()))
            if result == f'trial {index:02d} R\r\n'.encode('ascii'):
                return
            if result != f'trial {index:02d} W\r\n'.encode('ascii'):
                raise ValueError('Display trial changed or ended: ' + repr(result))
            self.sleep(0.05)
        raise TimeoutError('Display frame did not complete in time.')

    def run(self, sweep=False):
        # Legacy `err` is a supported refusal, never evidence of capability.
        if self.request('display') != CAPABILITY:
            raise ValueError('Firmware lacks display identification protocol 1; update with a trusted image first.')
        config = self.request('display get')
        match = re.fullmatch(rb'config ([0-9]{2}) ([0-9]{2})\r\n', config)
        if not match or int(match[1]) >= 96 or int(match[2]) > 62:
            raise ValueError('Invalid display configuration reply: ' + repr(config))
        saved, slots = int(match[1]), int(match[2])
        self.output(f'Saved trial {saved:02d}; {slots} journal slots remain. No attenuation command will be sent.')
        self.output('Check all four continuous border edges and both short inner edge markers. '
                    'A readable label alone is insufficient. Type cancel at any prompt.')
        self.output('Each prompt allows 90 seconds; firmware ends after 120 seconds without an advance '
                    'or after ten minutes total. Use this helper to advance; leave the front keys alone.')
        self.request('display sweep' if sweep else 'display start', b'ok display\r\n')
        self.owned = True
        self.deadline = self.clock() + SESSION_SECONDS
        selected = None
        if sweep:
            self.output('Optional PA4..PA7 role sweep selected explicitly. Only use on compatible OLED input wiring.')
            for index in range(96):
                self.ready(index)
                family = 'SSD1306' if index < 48 else 'SH1106'
                answer = self.prompt(f'Trial {index:02d} ({family}): complete border? Type yes, Enter for next, or cancel: ')
                if answer.lower() == 'yes':
                    selected = index
                    break
                if answer:
                    raise ValueError('Expected yes, Enter, or cancel; no choice saved.')
                if index < 95:
                    self.request('display next', b'ok next\r\n')
        else:
            first = saved % 48
            self.ready(first)
            answer = self.prompt(f'SSD1306 trial {first:02d} is held. Inspect its edges, then press Enter for SH1106: ')
            if answer:
                raise ValueError('Expected Enter or cancel; no choice saved.')
            self.request('display next', b'ok next\r\n')
            self.ready(first + 48)
            answer = self.prompt('Which had the complete border: SSD1306, SH1106, or neither? ')
            if answer.upper() in ('SSD1306', 'SH1106'):
                selected = first + (48 if answer.upper() == 'SH1106' else 0)
            elif answer.lower() != 'neither':
                raise ValueError('Unrecognized choice; no choice saved.')
        if selected is None:
            self.request('display finish', b'ok finish\r\n')
            self.owned = False
            self.output('No choice saved; normal display restored. Use --sweep explicitly if the OLED wiring differs.')
            return
        self.request(f'display try {selected:02d}', b'ok try\r\n')
        self.ready(selected)
        answer = self.prompt(f'Trial {selected:02d} is shown again. Confirm all four edges; type SAVE {selected:02d} to persist it: ')
        if answer != f'SAVE {selected:02d}':
            raise InterruptedError('Save not confirmed; no choice saved.')
        self.request(f'display save {selected:02d}', b'ok saving\r\n')
        deadline = min(self.deadline, self.clock() + 5)
        while self.clock() < deadline:
            reply = self.request('display status', seconds=min(self.seconds, deadline - self.clock()))
            if reply == f'saved {selected:02d}\r\n'.encode('ascii'):
                result = self.request('display get')
                match = re.fullmatch(rb'config ([0-9]{2}) ([0-9]{2})\r\n', result)
                if not match or int(match[1]) != selected or int(match[2]) > 62:
                    raise ValueError('Saved configuration readback mismatch; inspect before retrying.')
                self.owned = False
                self.output(f'Saved trial {selected:02d}; normal display restored. '
                            'This selects compatible rendering, not controller silicon identity.')
                return
            if reply != b'saving\r\n':
                raise ValueError('Save failed: ' + repr(reply))
            self.sleep(0.05)
        raise TimeoutError('Save completion unknown; inspect configuration before retrying.')

    def cancel(self):
        if not self.owned:
            return
        if self.link.failed:
            self.output('UART failed; no commands replayed. Firmware restores the saved display within '
                        '120 seconds of the last advance. A confirmed save may already have committed; '
                        'read configuration before retrying.')
        else:
            self.request('display cancel', b'ok cancel\r\n')
            self.output('Session closed; normal display restored. A save already committed is retained.')
        self.owned = False


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', required=True, help='exact identified serial port; no scanning')
    parser.add_argument('--timeout', default='2', help='per-request seconds, >0 and <=30 (default 2)')
    parser.add_argument('--sweep', action='store_true', help='explicitly try all 96 PA4..PA7 role/family trials')
    args = parser.parse_args(argv)
    try:
        seconds = timeout_seconds(args.timeout)
        with Serial(args.port) as link:
            session = Identification(link, seconds)
            try:
                session.run(args.sweep)
            finally:
                session.cancel()
        return 0
    except (ValueError, OSError, RuntimeError, EOFError, KeyboardInterrupt) as exc:
        print(str(exc) or 'Identification interrupted.', file=sys.stderr)
        return 1


if __name__ == '__main__':
    sys.exit(main())
