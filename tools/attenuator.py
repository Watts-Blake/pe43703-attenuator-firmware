"""Single-port USB attenuator set/get (Windows, macOS and Linux)."""
import argparse
from decimal import Decimal, InvalidOperation
import re
import sys
from serial_port import Serial, timeout_seconds


def db(value):
    if not isinstance(value, str) or not re.fullmatch(r'[0-9]{1,2}(?:\.[0-9]{1,2})?', value):
        raise ValueError('dB must be 0..31.75 in exact 0.25 steps')
    try:
        number = Decimal(value)
        if not 0 <= number <= Decimal('31.75') or number * 4 != (number * 4).to_integral_value():
            raise ValueError('dB must be 0..31.75 in exact 0.25 steps')
    except InvalidOperation as exc:
        raise ValueError('invalid dB') from exc
    return format(number, '.2f')


def get(link, seconds):
    reply = link.request(b'get\n', seconds=seconds)
    if not re.fullmatch(rb'(?:[0-9]|[12][0-9]|3[01])\.(?:00|25|50|75)\r\n', reply):
        raise ValueError('invalid GET reply: ' + repr(reply))
    return reply[:-2].decode('ascii')


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest='command', required=True)
    for action in ('set', 'get'):
        sub = commands.add_parser(action)
        sub.add_argument('--port', required=True, help='exact identified port; no scanning')
        sub.add_argument('--timeout', default='2', help='per-request seconds, >0 and <=30 (default 2)')
        if action == 'set':
            sub.add_argument('db', help='0..31.75 in exact quarter-dB steps')
    args = parser.parse_args(argv)
    try:
        seconds = timeout_seconds(args.timeout)
        value = db(args.db) if args.command == 'set' else None
        with Serial(args.port) as link:
            if value is not None:
                link.request(f'set {value}\n'.encode('ascii'), f'ok {value}\r\n'.encode('ascii'), seconds=seconds)
            result = get(link, seconds)
            if value is not None and result != value:
                raise ValueError('SET acknowledged but GET mismatch; application unknown')
        print(result, flush=True)
        return 0
    except (ValueError, OSError, RuntimeError) as exc:
        print(str(exc), file=sys.stderr)
        return 1


if __name__ == '__main__':
    sys.exit(main())
