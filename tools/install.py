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


# PM0075 sections2.3.5/2.4.1: checked FPEC operations. The OpenOCD unlock handler
# ignores option-read failure and returns success for some option-write failures.
# Do not call that handler, flash-bank probing, a loader or a density fallback.
FPEC = '''proc one {address} { return [lindex [read_memory $address 32 1] 0] }
proc fpec_wait {require_eop} {
    for {set n 0} {$n < 1000} {incr n} {
        set sr [one 0x4002200c]
        if {$sr & 0x14} { error "FPEC program/write-protection error; completion uncertain" }
        if {!($sr & 1)} {
            if {$require_eop && !($sr & 0x20)} { error "missing FPEC end-of-operation" }
            return
        }
        sleep 1
    }
    error "FPEC busy timeout; completion uncertain; never replay"
}
fpec_wait 0
# HSI is required for flash programming/erase; enable after unit validation/halt.
write_memory 0x40021000 32 [list [expr {[one 0x40021000] | 1}]]
set ready 0
for {set n 0} {$n < 1000} {incr n} {
    if {[one 0x40021000] & 2} { set ready 1; break }
    sleep 1
}
if {!$ready} { error "HSI not ready; no option erase" }
write_memory 0x40022004 32 {0x45670123}
write_memory 0x40022004 32 {0xcdef89ab}
write_memory 0x40022008 32 {0x45670123}
write_memory 0x40022008 32 {0xcdef89ab}
if {([one 0x40022010] & 0x280) != 0x200} { error "FPEC/option unlock failed" }
write_memory 0x4002200c 32 {0x20}
echo "OPTION_ERASE_BEGIN"
write_memory 0x40022010 32 {0x220}
write_memory 0x40022010 32 {0x260}
fpec_wait 1
set erased [read_memory 0x1ffff800 16 8]
if {[llength $erased] != 8} { error "short erased-option read" }
foreach word $erased { if {$word != 0xffff} { error "option erase incomplete" } }
write_memory 0x40022010 32 {0x210}
# Restore every non-RDP byte from the checked OBR/WRPR snapshot. RDP LAST:
# its transition to0xa5 triggers hardware mass erase of the main flash.
foreach address $option_addresses byte $option_values {
    write_memory 0x4002200c 32 {0x20}
    write_memory $address 16 [list $byte]
    fpec_wait 1
    set actual [lindex [read_memory $address 16 1] 0]
    if {$actual != ($byte | ((~$byte & 255) << 8))} { error "option programming verify failed" }
}
write_memory 0x40022010 32 {0x80}
if {!([one 0x40022010] & 0x80)} { error "FPEC lock failed" }
echo "RDP_PROGRAMMED_POR_REQUIRED"
'''


def option_values(i):
    obr, wrpr = int(i['obr'], 0), int(i['wrpr'], 0)
    return [(obr >> 2) & 255, (obr >> 10) & 255, (obr >> 18) & 255,
            *[(wrpr >> shift) & 255 for shift in (0, 8, 16, 24)], 0xa5]


def verify_options(i):
    values = option_values(i)
    expected = [values[-1], *values[:-1]]
    encoded = ' '.join(hex(b | ((~b & 255) << 8)) for b in expected)
    return f'''set options [read_memory 0x1ffff800 16 8]
if {{[llength $options] != 8}} {{ error "short option read" }}
foreach actual $options expected {{{encoded}}} {{
    if {{$actual != $expected}} {{ error "post-POR option preservation mismatch" }}
}}
'''


def tcl_plan(i, phase, bin_path, bin_hash):
    validate_identity(i)
    if phase not in phases(i):
        raise ValueError('phase does not match identity stage')
    path = str(Path(bin_path).resolve())
    if not re.fullmatch(r'[A-Za-z0-9_./:-]+', path):
        raise ValueError('stage path must use letters/digits/_/./:/- only')
    token = confirmation({'identity': i, 'bin_sha256': bin_hash}, phase)
    code = f'''if {{![info exists ::env(PE43703_INSTALL_CONFIRMED)] || $::env(PE43703_INSTALL_CONFIRMED) ne {{{token}}}}} {{
    error "Missing exact deliberate phase confirmation; no adapter opened"
}}
''' + connection(i['serial'], i['profile']) + chip_guard(i['idcode'], i['dpidr'])
    if i['kind'] == 'protected-physical':
        code += option_guard(i, after_por=phase == 'post-rdp-status')
        if phase == 'remove-rdp':
            # No protected UID/density read and no flash-driver bank probe.
            code += '''targets stm32f1x.cpu
stm32f1x.cpu arp_examine
reset halt
''' + chip_guard(i['idcode'], i['dpidr']) + option_guard(i)
            addresses = ' '.join(hex(a) for a in (*range(0x1ffff802, 0x1ffff810, 2), 0x1ffff800))
            code += f'set option_addresses {{{addresses}}}\n'
            code += 'set option_values {' + ' '.join(str(v) for v in option_values(i)) + '}\n' + FPEC
            code += 'echo "STOP: actual power-on reset required; never replay removal."\n'
        else:
            code += verify_options(i) + '''set uid [read_memory 0x1ffff7e8 32 3]
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
''' + guard
        if phase == 'flash':
            code += f'flash write_image erase {{{path}}} 0x08000000 bin\n'
        code += f'verify_image {{{path}}} 0x08000000 bin\n'
        if phase == 'start':
            code += 'reset run\n'
    return code + f'echo "PE43703_PHASE_COMPLETE {phase}"\nshutdown\n'


def stage(elf_path, directory, identity):
    validate_identity(identity)
    elf = read_elf(elf_path)
    binary = Path(elf_path).with_suffix('.bin').read_bytes()
    if binary != elf['binary']:
        raise ValueError('BIN differs from selected ELF')
    directory = Path(directory).resolve()
    plans = {p: tcl_plan(identity, p, directory/'candidate.bin', digest(binary)) for p in phases(identity)}
    directory.mkdir(parents=True, exist_ok=False)
    (directory/'candidate.bin').write_bytes(binary)
    (directory/'candidate.elf').write_bytes(Path(elf_path).read_bytes())
    manifest = {'schema': 2, 'identity': identity, 'bin_sha256': digest(binary),
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
    if manifest['schema'] != 2 or phase not in phases(i):
        raise ValueError('invalid schema or identity phase')
    elf = read_elf(directory/'candidate.elf')
    binary = (directory/'candidate.bin').read_bytes()
    expected = tcl_plan(i, phase, directory/'candidate.bin', manifest['bin_sha256'])
    plan = directory/f'{phase}.tcl'
    if (digest(binary) != manifest['bin_sha256'] or binary != elf['binary'] or
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
        if (result.returncode != 0 or re.search(r'(?im)(?:error\s*:|\bfailed\b|\bfailure\b)', raw) or
            f'PE43703_PHASE_COMPLETE {phase}' not in raw or
            (phase == 'remove-rdp' and 'RDP_PROGRAMMED_POR_REQUIRED' not in raw)):
            raise ValueError('device operation failed/ambiguous; inspect retained transcript; do not replay')
        receipt['completion'] = 'option-programmed-POR-required' if phase == 'remove-rdp' else 'completed'
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
            stage(a.elf, a.directory, i)
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
