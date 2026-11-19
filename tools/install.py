"""Stage installation plans offline. Only the explicit execute subcommand opens SWD.

Never call execute from a build, test, import, or image inspection command.
Generated plans contain private unit identity and must stay out of version control.
"""
import argparse
import hashlib
import json
import os
import re
import subprocess
from pathlib import Path
from image import read_elf
from mailbox import connection

PHASES = ('remove-rdp', 'flash', 'verify', 'start')


def digest(blob):
    return hashlib.sha256(blob).hexdigest()


def validate_identity(identity):
    if identity['device'] != 'STM32F103C8T6':
        raise ValueError('only STM32F103C8T6 is supported')
    if not re.fullmatch(r'[A-Za-z0-9_-]{1,64}', identity['unit']):
        raise ValueError('unit label must contain only letters, digits, _ or -')
    connection(identity['serial'])
    if not re.fullmatch(r'[0-9a-fA-F]{24}', identity['uid']):
        raise ValueError('UID must be three 32-bit hex words in register-read order')
    if not re.fullmatch(r'0x[0-9a-fA-F]{8}', identity['idcode']):
        raise ValueError('IDCODE must be 0x plus eight hexadecimal digits')
    idcode = int(identity['idcode'], 0)
    if idcode & 0xfff != 0x410 or not 0 <= idcode <= 0xffffffff:
        raise ValueError('expected exact IDCODE for medium-density STM32F103')


def confirmation(manifest, phase):
    action = {'remove-rdp': 'ERASE-IRREVERSIBLY', 'flash': 'FLASH',
              'verify': 'VERIFY-HALT', 'start': 'RESET-START'}[phase]
    i = manifest['identity']
    return f"{action} {i['unit']} {i['device']} {i['serial']} {i['idcode']} {i['uid']} {manifest['bin_sha256']}"


def tcl_plan(identity, phase, bin_path, bin_hash):
    validate_identity(identity)
    # A conservative path alphabet avoids Tcl command/variable substitution.
    path = str(Path(bin_path).resolve())
    if not re.fullmatch(r'[A-Za-z0-9_./:-]+', path):
        raise ValueError('stage directory path must use letters/digits/_/./:/- only')
    uid = identity['uid']
    expected = ' '.join('0x' + uid[i:i+8] for i in range(0,24,8))
    token = confirmation({'identity': identity, 'bin_sha256': bin_hash}, phase)
    code = f'''# Staged only: execution requires the exact operator confirmation.
if {{![info exists ::env(PE43703_INSTALL_CONFIRMED)] || $::env(PE43703_INSTALL_CONFIRMED) ne {{{token}}}}} {{
    error "Run the explicit install.py execute command and confirm this phase"
}}
''' + connection(identity['serial']) + f'''
if {{[lindex [read_memory 0xe0042000 32 1] 0] != {identity['idcode']}}} {{ error "IDCODE mismatch" }}
set uid [read_memory 0x1ffff7e8 32 3]
foreach actual $uid expected {{{expected}}} {{
    if {{$actual != $expected}} {{ error "unit UID mismatch; no action taken" }}
}}
set obr [lindex [read_memory 0x4002201c 32 1] 0]
if {{$obr & 1}} {{ error "option-byte error; stop" }}
'''
    if phase == 'remove-rdp':
        code += '''if {!($obr & 2)} { error "RDP already inactive; stale erase plan" }
reset halt
# DESTRUCTIVE: changing RDP to unprotected MASS ERASES stock firmware.
# OpenOCD may rewrite other option bytes: review installed driver/version.
stm32f1x unlock 0
echo "STOP: option reload/power-on reset is a separate attended action; do not retry unlock."
'''
    else:
        code += 'if {$obr & 2} { error "RDP active; no automatic unlock" }\nreset halt\n'
        if phase == 'flash':
            code += f'flash write_image erase {{{path}}} 0x08000000 bin\nverify_image {{{path}}} 0x08000000 bin\n'
        elif phase == 'verify':
            code += f'verify_image {{{path}}} 0x08000000 bin\n'
        elif phase == 'start':
            code += f'verify_image {{{path}}} 0x08000000 bin\nreset run\n'
        else:
            raise ValueError('unknown installation phase')
    return code + 'shutdown\n'


def stage(elf_path, directory, identity):
    validate_identity(identity)
    elf = read_elf(elf_path)
    binary = Path(elf_path).with_suffix('.bin').read_bytes()
    if binary != elf['binary']:
        raise ValueError('BIN differs from selected ELF')
    directory = Path(directory).resolve()
    # Validate all generated text before creating output files.
    plans = {p: tcl_plan(identity, p, directory/'candidate.bin', digest(binary)) for p in PHASES}
    directory.mkdir(parents=True, exist_ok=False)
    (directory/'candidate.bin').write_bytes(binary)
    (directory/'candidate.elf').write_bytes(Path(elf_path).read_bytes())
    manifest = {'schema': 1, 'identity': identity, 'bin_sha256': digest(binary),
                'elf_sha256': elf['sha256'], 'plans': {}}
    for phase, text in plans.items():
        (directory/f'{phase}.tcl').write_text(text)
        manifest['plans'][phase] = digest(text.encode())
    (directory/'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    return manifest


def execute(directory, phase, confirm, runner=subprocess.run):
    directory = Path(directory).resolve()
    manifest = json.loads((directory/'manifest.json').read_text())
    if manifest['schema'] != 1 or phase not in PHASES:
        raise ValueError('invalid schema or phase')
    validate_identity(manifest['identity'])
    elf = read_elf(directory/'candidate.elf')
    binary = (directory/'candidate.bin').read_bytes()
    expected_plan = tcl_plan(manifest['identity'], phase, directory/'candidate.bin', manifest['bin_sha256'])
    plan_path = directory/f'{phase}.tcl'
    if (digest(binary) != manifest['bin_sha256'] or binary != elf['binary'] or
        elf['sha256'] != manifest['elf_sha256'] or
        plan_path.read_text() != expected_plan or
        digest(expected_plan.encode()) != manifest['plans'][phase]):
        raise ValueError('staged image/plan identity changed; refusing execution')
    token = confirmation(manifest, phase)
    if confirm != token:
        raise ValueError('confirmation must match exactly: ' + token)
    # No retries, fallback adapter, protection downgrade, or implicit start.
    return runner(['openocd', '-f', str(plan_path)], check=True, timeout=120,
                  env={'PATH': os.environ.get('PATH', ''), 'PE43703_INSTALL_CONFIRMED': token})


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest='command', required=True)
    create = sub.add_parser('stage', help='offline only: copy image and generate plans')
    create.add_argument('--elf', type=Path, required=True)
    create.add_argument('--directory', type=Path, required=True)
    for key in ('device', 'unit', 'serial', 'idcode', 'uid'):
        create.add_argument('--' + key, required=True)
    run = sub.add_parser('execute', help='DEVICE ACTION: execute one reviewed phase')
    run.add_argument('--directory', type=Path, required=True)
    run.add_argument('--phase', choices=PHASES, required=True)
    args = parser.parse_args()
    try:
        if args.command == 'stage':
            manifest = stage(args.elf, args.directory, {k: getattr(args,k) for k in
                             ('device','unit','serial','idcode','uid')})
            print('Staged only. No device contacted. Review every generated plan.')
            print('Irreversible erase confirmation: ' + confirmation(manifest,'remove-rdp'))
        else:
            manifest = json.loads((args.directory/'manifest.json').read_text())
            token = confirmation(manifest, args.phase)
            print('DEVICE ACTION. Review wiring, selected unit, image and plan.\nType exactly:\n' + token)
            execute(args.directory, args.phase, input('Confirmation: '))
    except (ValueError, OSError, KeyError, subprocess.SubprocessError) as error:
        parser.exit(2, f'{error}\n')


if __name__ == '__main__':
    main()
