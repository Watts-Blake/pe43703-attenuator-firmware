"""Stage explicit OpenOCD mailbox operations, without executing OpenOCD.

Output contains the selected adapter serial: keep generated plans private.
"""
import argparse
import re
from pathlib import Path
from image import read_elf

MAGIC, VERSION, SIZE, CHECK = 0x50453433, 1, 128, 0xa5c35a3c
MAPS = ((11, 12, 13), (11, 13, 12), (12, 11, 13), (12, 13, 11), (13, 11, 12), (13, 12, 11))


def parse_db(text):
    if not re.fullmatch(r'[0-9]{1,2}(\.[0-9]{1,2})?', text):
        raise ValueError('use 0..31.75 dB in exact 0.25 dB steps')
    whole, _, fraction = text.partition('.')
    fraction = int(fraction.ljust(2, '0') or '0')
    if int(whole) > 31 or fraction % 25:
        raise ValueError('use 0..31.75 dB in exact 0.25 dB steps')
    return int(whole)*4 + fraction//25


def connection(serial):
    if not re.fullmatch(r'[A-Za-z0-9_-]{1,64}', serial):
        raise ValueError('adapter serial must be an exact 1..64 character identifier')
    return f'''# Generated offline. Running this file contacts exactly the selected adapter.
source [find interface/stlink.cfg]
transport select hla_swd
adapter serial {{{serial}}}
adapter speed 100
source [find target/stm32f1x.cfg]
gdb_port disabled
tcl_port disabled
telnet_port disabled
init
'''


def image_guard(elf):
    # read_memory does not run target algorithms or halt the CPU.
    code = '''proc assert_bytes {address expected} {
    set actual [read_memory $address 8 [llength $expected]]
    foreach a $actual e $expected {
        if {$a != $e} { error "running flash differs from selected ELF" }
    }
}
'''
    for address, blob in elf['loads']:
        code += f'assert_bytes 0x{address:08x} {{' + ' '.join(str(b) for b in blob) + '}\n'
    return code


SNAPSHOT = '''proc snapshot {} {
    for {set retry 0} {$retry < 20} {incr retry} {
        set before [lindex [read_memory 0x20000010 32 1] 0]
        if {$before & 1} { sleep 1; continue }
        set words [read_memory 0x20000000 32 32]
        set after [lindex [read_memory 0x20000010 32 1] 0]
        if {$before == $after && $before == [lindex $words 4] && !($after & 1)} {
            if {[lindex $words 0] != 0x50453433 || [lindex $words 1] != 1 ||
                [lindex $words 2] != 128} { error "mailbox schema mismatch" }
            return $words
        }
    }
    error "no coherent mailbox snapshot"
}
proc show_status {words} {
    set maps {{11 12 13} {11 13 12} {12 11 13} {12 13 11} {13 11 12} {13 12 11}}
    set p [lindex $words 6]
    if {$p < 0 || $p > 5} { error "invalid mapping in mailbox" }
    echo "state=[lindex $words 3] last_qdb=[lindex $words 5] permutation=$p SI/CLK/LE(PB)=[lindex $maps $p] issued=[lindex $words 7] ack=[lindex $words 16] status=[lindex $words 17] result_qdb=[lindex $words 18] result_permutation=[lindex $words 19] result_issued=[lindex $words 20]"
}
set words [snapshot]
show_status $words
'''


def plan(elf, serial, action, value=None):
    code = connection(serial) + image_guard(elf) + SNAPSHOT
    if action == 'status':
        return code + 'shutdown\n'
    op = {'get': 1, 'set': 2, 'select': 3}[action]
    arg = parse_db(value) if action == 'set' else int(value) if action == 'select' else 0
    if action == 'select' and arg not in range(6):
        raise ValueError('permutation must be 0..5')
    code += f'''if {{[lindex $words 3] != 1}} {{ error "firmware not ready" }}
set ack [lindex $words 16]
set published [lindex $words 8]
if {{$published != 0 && $published != $ack}} {{ error "pending command; read status before recovery" }}
if {{$ack >= 0xfffffffe}} {{ error "sequence exhausted; deliberate reset required" }}
set seq [expr {{$ack + 2}}]
set inverse [expr {{(~$seq) & 0xffffffff}}]
set check [expr {{(0xa5c35a3c ^ $seq ^ 0x50453433 ^ 1 ^ {op} ^ {arg}) & 0xffffffff}}]
# Invalidate first, write complete payload, publish sequence LAST.
write_memory 0x20000020 32 {{0}}
write_memory 0x20000024 32 [list 0x50453433 1 {op} {arg} 0 $check $inverse]
write_memory 0x20000020 32 [list $seq]
set complete 0
for {{set retry 0}} {{$retry < 100}} {{incr retry}} {{
    set words [snapshot]
    if {{[lindex $words 16] == $seq}} {{ set complete 1; break }}
    sleep 1
}}
if {{!$complete}} {{ error "ack timeout; do not replay: inspect status" }}
show_status $words
if {{[lindex $words 17] != 0}} {{ error "firmware rejected command" }}
shutdown
'''
    return code


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--elf', type=Path, required=True)
    parser.add_argument('--serial', required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('action', choices=('status', 'get', 'set', 'select'))
    parser.add_argument('value', nargs='?')
    args = parser.parse_args()
    if (args.action in ('set', 'select')) != (args.value is not None):
        parser.error('set/select need one value; status/get take none')
    try:
        text = plan(read_elf(args.elf), args.serial, args.action, args.value)
        with args.output.open('x') as stream:
            stream.write(text)
    except (ValueError, OSError, IndexError) as error:
        parser.exit(2, f'{error}\n')
    print('Staged only; no adapter opened. Review the generated Tcl before deliberate execution.')


if __name__ == '__main__':
    main()
