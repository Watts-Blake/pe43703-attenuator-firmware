"""Generate explicit native ST-Link attachment and identity guards offline."""
import re

PROFILES = {'native-swd': ('interface/stlink.cfg', 'swd'),
            'native-dapdirect': ('interface/stlink-dap.cfg', 'dapdirect_swd')}


def identifier(value, name):
    if not re.fullmatch(r'[A-Za-z0-9_-]{1,64}', value):
        raise ValueError(name + ' must be an exact 1..64 character identifier')
    return value


def hexword(value, name):
    if not re.fullmatch(r'0x[0-9a-fA-F]{8}', value):
        raise ValueError(name + ' must be 0x plus eight hexadecimal digits')
    return int(value, 0)


def validate_mcu(idcode, uid):
    if hexword(idcode, 'IDCODE') & 0xfff != 0x410:
        raise ValueError('expected exact medium-density STM32F103 IDCODE')
    if not isinstance(uid, str) or not re.fullmatch(r'[0-9a-fA-F]{24}', uid) or int(uid, 16) in (0, (1 << 96)-1):
        raise ValueError('real post-unprotection UID required: three 32-bit hex words in read order')


def connection(serial, profile='native-swd'):
    identifier(serial, 'adapter serial')
    if profile not in PROFILES:
        raise ValueError('select an explicit supported native transport profile')
    interface, transport = PROFILES[profile]
    return f'''# Generated offline; executing this file opens the selected adapter.
source [find {interface}]
transport select {transport}
adapter serial {{{serial}}}
source [find target/stm32f1x.cfg]
# Defer Cortex-M examination BEFORE init; remove the included target-write hook.
stm32f1x.cpu configure -defer-examine -event examine-end {{}}
target create attenuator.mem mem_ap -dap stm32f1x.dap -ap-num 0 -endian little
targets attenuator.mem
# Included target scripts set their own speed; this final value controls attach.
adapter speed 100
reset_config none
gdb_port disabled
tcl_port disabled
telnet_port disabled
init
poll off
'''


def chip_guard(idcode, dpidr='0x1ba01477'):
    if hexword(idcode, 'IDCODE') & 0xfff != 0x410:
        raise ValueError('unsupported IDCODE')
    if hexword(dpidr, 'DPIDR') != 0x1ba01477:
        raise ValueError('unsupported DPIDR')
    return f'''if {{[stm32f1x.dap dpreg 0] != {dpidr}}} {{ error "DPIDR mismatch" }}
if {{[lindex [read_memory 0xe0042000 32 1] 0] != {idcode}}} {{ error "IDCODE mismatch" }}
'''


def mcu_guard(idcode, uid):
    validate_mcu(idcode, uid)
    expected = ' '.join('0x'+uid[i:i+8] for i in range(0, 24, 8))
    return chip_guard(idcode) + f'''set uid [read_memory 0x1ffff7e8 32 3]
if {{[llength $uid] != 3}} {{ error "short UID read" }}
foreach actual $uid expected {{{expected}}} {{
    if {{$actual != $expected}} {{ error "unit UID mismatch" }}
}}
if {{[lindex [read_memory 0x4002201c 32 1] 0] & 3}} {{ error "RDP active or option-byte error" }}
'''
