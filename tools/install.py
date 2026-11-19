"""Stage installation offline; only explicit execute phases open SWD.

Erase binds an attended sole-wired target. Programming binds its post-POR UID.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import time
from image import read_elf
from pack_image import bundle
from swd import PROFILES, connection, chip_guard, mcu_guard, identifier, hexword, validate_mcu

PHASES = ('remove-rdp', 'post-rdp-status', 'flash', 'verify', 'start')


def identity_plan(serial, profile='native-swd'):
    return connection(serial, profile) + '''echo [format "DPIDR=0x%08x" [stm32f1x.dap dpreg 0]]
echo [format "IDCODE=0x%08x" [lindex [read_memory 0xe0042000 32 1] 0]]
echo [format "OBR=0x%08x" [lindex [read_memory 0x4002201c 32 1] 0]]
echo [format "WRPR=0x%08x" [lindex [read_memory 0x40022020 32 1] 0]]
shutdown
'''


def digest(blob):
    return hashlib.sha256(blob).hexdigest()


def validate_identity(i):
    if i['device'] != 'STM32F103C8T6':
        raise ValueError('only STM32F103C8T6 is supported')
    identifier(i['unit'], 'unit label')
    connection(i['serial'], i['profile'])
    chip_guard(i['idcode'], i['dpidr'])
    obr = hexword(i['obr'], 'OBR')
    hexword(i['wrpr'], 'WRPR')
    if i['kind'] == 'protected-physical':
        if i.get('uid') is not None or i.get('physical_confirmation') != 'sole-wired-target-confirmed':
            raise ValueError('erase requires explicit sole-wired-target confirmation, without UID')
        if obr & 3 != 2:
            raise ValueError('erase requires observed active RDP and no option error')
    elif i['kind'] == 'uid-bound':
        validate_mcu(i['idcode'], i['uid'])
        if obr & 3:
            raise ValueError('programming requires post-POR inactive RDP and no option error')
    else:
        raise ValueError('unsupported installation identity kind')


def phases(i):
    return PHASES[:2] if i['kind'] == 'protected-physical' else PHASES[2:]


def confirmation(manifest, phase):
    i = manifest['identity']
    action = {'remove-rdp':'ERASE-IRREVERSIBLY', 'post-rdp-status':'POR-COMPLETED-CHECK',
              'flash':'FLASH', 'verify':'VERIFY-HALT', 'start':'RESET-START'}[phase]
    unit = i.get('uid') or i['physical_confirmation']
    return (f"{action} {i['unit']} {i['device']} {i['serial']} {i['dpidr']} "
            f"{i['idcode']} {unit} {i['obr']} {i['wrpr']} {manifest['bin_sha256']}")


def option_guard(i, after_por=False):
    obr = int(i['obr'], 0) & (~2 if after_por else 0xffffffff)
    return f'''if {{[lindex [read_memory 0x4002201c 32 1] 0] != {obr}}} {{ error "OBR changed or unexpected protection/options" }}
if {{[lindex [read_memory 0x40022020 32 1] 0] != {i['wrpr']}}} {{ error "WRPR changed" }}
'''


# The standard driver can return success after an option operation fails.
# Capture its diagnostics and defer authoritative RDP verification until POR.
PROBE_BEGIN = 'PE43703_PROBE_BEGIN'
PROBE_END = 'PE43703_PROBE_END'
EXPECTED_PROBE_LINES = (
    r'^(?:Error:[ \t]*)?Failed to read memory at 0x1ffff7e0[ \t\r]*$',
    r'^(?:Warn[ \t]*:[ \t]*)?STM32 flash size failed, probe inaccurate - assuming 128k flash[ \t\r]*$',
)
STANDARD_UNLOCK = r"""proc checked_driver {command} {
    set result [catch {capture $command} output]
    echo $output
    if {$result || [regexp -nocase {error|failed|failure|assuming|inaccurate} $output]} {
        error "driver diagnostic rejected; stop without replay"
    }
    return $output
}
echo "PE43703_PROBE_BEGIN"
set probe_rc [catch {capture {flash probe 0}} probe_output]
echo $probe_output
echo "PE43703_PROBE_END"
if {$probe_rc} {error "driver probe failed"}
set remaining $probe_output
regsub -all -nocase -line {^(Error:[ \t]*)?Failed to read memory at 0x1ffff7e0[ \t\r]*$} $remaining {} remaining
regsub -all -nocase -line {^(Warn[ \t]*:[ \t]*)?STM32 flash size failed, probe inaccurate - assuming 128k flash[ \t\r]*$} $remaining {} remaining
if {[regexp -nocase {error|failed|failure} $remaining]} {error "unexpected probe diagnostic"}
if {![string match {*flash size = 64 KiB*} $probe_output]} {error "configured64KiB bank not confirmed"}
echo "STANDARD_UNLOCK_BEGIN"
set output [checked_driver {stm32f1x unlock 0}]
if {![string match {*stm32x unlocked.*} $output]} {error "missing standard unlock marker"}
reset halt
stm32f1x.cpu arp_examine
halt 1000
echo [format "PRE_POR_OBR=0x%08x" [lindex [read_memory 0x4002201c 32 1] 0]]
echo "UNLOCK_DRIVER_RETURNED_POR_REQUIRED"
"""


def checked_transcript(raw, phase):
    if phase == 'remove-rdp':
        # Only the explicitly delimited probe can have this known RDP read
        # failure. The same error in unlock or any other phase stays fatal.
        if raw.count(PROBE_BEGIN) != 1 or raw.count(PROBE_END) != 1:
            return False
        if raw.index(PROBE_END) < raw.index(PROBE_BEGIN):
            return False
        before, tail = raw.split(PROBE_BEGIN)
        probe, after = tail.split(PROBE_END)
        if ('STANDARD_UNLOCK_BEGIN' not in after or
                'UNLOCK_DRIVER_RETURNED_POR_REQUIRED' not in after):
            return False
        for pattern in EXPECTED_PROBE_LINES:
            probe = re.sub(pattern, '', probe, flags=re.I | re.M)
        raw = before + probe + after
    return not re.search(r'(?im)(?:error\s*:|\bfailed\b|\bfailure\b)', raw)


def tcl_plan(i, phase, bin_path, bin_hash):
    validate_identity(i)
    if phase not in phases(i):
        raise ValueError('phase does not match identity stage')
    path = Path(bin_path).resolve().as_posix()
    if not re.fullmatch(r'[A-Za-z0-9_./:-]+', path):
        raise ValueError('stage path must use letters/digits/_/./:/- only')
    token = confirmation({'identity': i, 'bin_sha256': bin_hash}, phase)
    code = f'''if {{![info exists ::env(PE43703_INSTALL_CONFIRMED)] || $::env(PE43703_INSTALL_CONFIRMED) ne {{{token}}}}} {{
    error "Missing exact deliberate phase confirmation; no adapter opened"
}}
''' + ('set FLASH_SIZE 0x10000\n' if phase == 'remove-rdp' else '') + connection(i['serial'], i['profile']) + chip_guard(i['idcode'], i['dpidr'])
    if i['kind'] == 'protected-physical':
        code += option_guard(i, after_por=phase == 'post-rdp-status')
        if phase == 'remove-rdp':
            # No preerase UID requirement. Driver probe may encounter blocked
            # density; FLASH_SIZE explicitly fixes the bank geometry to64KiB.
            code += '''targets stm32f1x.cpu
stm32f1x.cpu arp_examine
reset halt
# Reset can clear examination while -defer-examine remains set. Explicitly
# re-examine and halt before post-reset identity reads or any programming.
stm32f1x.cpu arp_examine
halt 1000
''' + chip_guard(i['idcode'], i['dpidr']) + option_guard(i)
            code += STANDARD_UNLOCK
        else:
            code += '''echo [format "POST_POR_OBR=0x%08x" [lindex [read_memory 0x4002201c 32 1] 0]]
set uid [read_memory 0x1ffff7e8 32 3]
if {[llength $uid] != 3} { error "short post-POR UID read" }
if {([lindex $uid 0] == 0 && [lindex $uid 1] == 0 && [lindex $uid 2] == 0) ||
    ([lindex $uid 0] == 0xffffffff && [lindex $uid 1] == 0xffffffff && [lindex $uid 2] == 0xffffffff)} {
    error "invalid post-POR UID"
}
if {[lindex [read_memory 0x1ffff7e0 16 1] 0] != 64} { error "expected actual C8 flash density64KiB" }
echo [format "POST_POR_UID=%08x%08x%08x" [lindex $uid 0] [lindex $uid 1] [lindex $uid 2]]
'''
    else:
        guard = mcu_guard(i['idcode'], i['uid']) + option_guard(i)
        code += guard + '''if {[lindex [read_memory 0x1ffff7e0 16 1] 0] != 64} { error "flash density mismatch" }
targets stm32f1x.cpu
stm32f1x.cpu arp_examine
reset halt
# Initial attachment remains deferred; reset requires explicit examination too.
stm32f1x.cpu arp_examine
halt 1000
''' + guard
        if phase == 'flash':
            code += f'flash write_image erase {{{path}}} 0x08000000 bin\n'
        code += f'verify_image {{{path}}} 0x08000000 bin\n'
        if phase == 'start':
            code += 'reset run\n'
    return code + f'echo "PE43703_PHASE_COMPLETE {phase}"\nshutdown\n'


def stage(elf_path, directory, identity, loader_path=None):
    validate_identity(identity)
    elf = read_elf(elf_path)
    binary = Path(elf_path).with_suffix('.bin').read_bytes()
    if binary != elf['binary']:
        raise ValueError('BIN differs from selected ELF')
    loader_path = Path(loader_path) if loader_path else Path(elf_path).with_name('bootloader.elf')
    loader = read_elf(loader_path, loader=True)
    if loader_path.with_suffix('.bin').read_bytes() != loader['binary']:
        raise ValueError('loader BIN differs from selected ELF')
    binary = bundle(loader['binary'], binary)
    directory = Path(directory).resolve()
    plans = {p: tcl_plan(identity, p, directory/'candidate.bin', digest(binary)) for p in phases(identity)}
    directory.mkdir(parents=True, exist_ok=False)
    (directory/'candidate.bin').write_bytes(binary)
    (directory/'candidate.elf').write_bytes(Path(elf_path).read_bytes())
    (directory/'bootloader.elf').write_bytes(loader_path.read_bytes())
    manifest = {'schema': 3, 'loader_sha256': loader['sha256'], 'identity': identity, 'bin_sha256': digest(binary),
                'elf_sha256': elf['sha256'], 'plans': {}}
    for phase, text in plans.items():
        (directory/f'{phase}.tcl').write_text(text)
        manifest['plans'][phase] = digest(text.encode())
    (directory/'manifest.json').write_text(json.dumps(manifest, indent=2)+'\n')
    return manifest


def execute(directory, phase, confirm, runner=subprocess.run):
    directory = Path(directory).resolve()
    manifest = json.loads((directory/'manifest.json').read_text())
    i = manifest['identity']
    validate_identity(i)
    if manifest['schema'] != 3 or phase not in phases(i):
        raise ValueError('invalid schema or identity phase')
    elf = read_elf(directory/'candidate.elf')
    loader = read_elf(directory/'bootloader.elf', loader=True)
    binary = (directory/'candidate.bin').read_bytes()
    expected = tcl_plan(i, phase, directory/'candidate.bin', manifest['bin_sha256'])
    plan = directory/f'{phase}.tcl'
    if (digest(binary) != manifest['bin_sha256'] or binary != bundle(loader['binary'], elf['binary']) or
        loader['sha256'] != manifest['loader_sha256'] or
        elf['sha256'] != manifest['elf_sha256'] or plan.read_text() != expected or
        digest(expected.encode()) != manifest['plans'][phase]):
        raise ValueError('staged image/plan changed; refusing execution')
    token = confirmation(manifest, phase)
    if confirm != token:
        raise ValueError('confirmation must match exactly: '+token)
    # Consumed BEFORE adapter open, including failure/timeout. No automatic retry.
    attempt = directory/f'attempt-{phase}'
    attempt.mkdir(exist_ok=False)
    receipt = {'phase': phase, 'start_unix': time.time(), 'completion': 'unknown'}
    record = attempt/'receipt.json'
    record.write_text(json.dumps(receipt, indent=2)+'\n')
    print('DEVICE_OPERATION_BEGIN '+phase, flush=True)
    try:
        result = runner(['openocd', '-f', str(plan)], check=False, timeout=120,
                        capture_output=True, text=True,
                        env={'PATH': os.environ.get('PATH', ''), 'PE43703_INSTALL_CONFIRMED': token})
        stdout, stderr = result.stdout or '', result.stderr or ''
        (attempt/'stdout.log').write_text(stdout)
        (attempt/'stderr.log').write_text(stderr)
        raw = stdout+'\n'+stderr
        if (result.returncode != 0 or not checked_transcript(raw, phase) or
            f'PE43703_PHASE_COMPLETE {phase}' not in raw or
            (phase == 'remove-rdp' and 'UNLOCK_DRIVER_RETURNED_POR_REQUIRED' not in raw)):
            raise ValueError('device operation failed/ambiguous; inspect retained transcript; do not replay')
        receipt['completion'] = 'driver-returned-POR-required' if phase == 'remove-rdp' else 'completed'
        return result
    except subprocess.TimeoutExpired as error:
        for name, value in (('stdout', error.stdout), ('stderr', error.stderr)):
            (attempt/f'{name}.log').write_bytes(value.encode() if isinstance(value, str) else value or b'')
        raise
    except Exception as error:
        receipt['error'] = str(error)
        raise
    finally:
        receipt['end_unix'] = time.time()
        record.write_text(json.dumps(receipt, indent=2)+'\n')


def main():
    p = argparse.ArgumentParser(description=__doc__)
    sub = p.add_subparsers(dest='command', required=True)
    collect = sub.add_parser('stage-identity', help='offline only: generate protected peripheral identity reads')
    collect.add_argument('--serial', required=True)
    collect.add_argument('--profile', choices=PROFILES, default='native-swd')
    collect.add_argument('--output', type=Path, required=True)
    create = sub.add_parser('stage', help='offline only')
    create.add_argument('--elf', type=Path, required=True)
    create.add_argument('--loader-elf', type=Path, required=True)
    create.add_argument('--directory', type=Path, required=True)
    create.add_argument('--purpose', choices=('remove-rdp','program'), required=True)
    for key in ('device','unit','serial','dpidr','idcode','obr','wrpr'):
        create.add_argument('--'+key, required=True)
    create.add_argument('--profile', choices=PROFILES, default='native-swd')
    create.add_argument('--uid', help='real UID recorded AFTER unprotection and POR')
    create.add_argument('--physical-confirmation', choices=('sole-wired-target-confirmed',))
    run = sub.add_parser('execute', help='DEVICE ACTION: one phase; never part of tests/builds')
    run.add_argument('--directory', type=Path, required=True)
    run.add_argument('--phase', choices=PHASES, required=True)
    a = p.parse_args()
    try:
        if a.command == 'stage-identity':
            text = identity_plan(a.serial, a.profile)
            with a.output.open('x') as stream:
                stream.write(text)
            print('Staged only. Executing this file later opens the selected adapter.')
        elif a.command == 'stage':
            i = {k:getattr(a,k) for k in ('device','unit','serial','dpidr','idcode','obr','wrpr','profile')}
            i['kind'] = 'protected-physical' if a.purpose == 'remove-rdp' else 'uid-bound'
            i['uid'], i['physical_confirmation'] = a.uid, a.physical_confirmation
            stage(a.elf, a.directory, i, a.loader_elf)
            print('Staged only. No device contacted. Review every plan and identity policy.')
        else:
            manifest = json.loads((a.directory/'manifest.json').read_text())
            token = confirmation(manifest, a.phase)
            print('DEVICE ACTION. Type exactly:\n'+token)
            result = execute(a.directory, a.phase, input('Confirmation: '))
            print(result.stdout or '', end='')
            print(result.stderr or '', end='', file=sys.stderr)
    except (ValueError, OSError, KeyError, subprocess.SubprocessError) as error:
        p.exit(2, f'{error}\n')


if __name__ == '__main__':
    main()
