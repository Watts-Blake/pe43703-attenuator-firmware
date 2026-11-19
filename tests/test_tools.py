import copy
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import Mock

sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'tools'))
from image import read_elf
from mailbox import parse_db, plan
from install import stage, execute, confirmation

ELF = Path(__file__).resolve().parents[1]/'build/attenuator.elf'
IDENTITY = {'device':'STM32F103C8T6', 'unit':'example-unit', 'serial':'EXAMPLE_ONLY',
            'idcode':'0x20036410', 'uid':'0123456789abcdef13579bdf'}


class Images(unittest.TestCase):
    def test_image(self):
        image = read_elf(ELF)
        self.assertEqual(image['binary'], ELF.with_suffix('.bin').read_bytes())
        self.assertEqual(image['symbols']['mailbox'][:2], (0x20000000,128))

    def test_corrupt_elf_rejected(self):
        for offset in (0,4,5,18):
            with self.subTest(offset=offset), tempfile.TemporaryDirectory() as tmp:
                data = bytearray(ELF.read_bytes()); data[offset] ^= 0xff
                f = Path(tmp)/'bad.elf'; f.write_bytes(data)
                with self.assertRaises((ValueError,IndexError)):
                    read_elf(f)

    def test_exact_parser(self):
        for qdb in range(128):
            self.assertEqual(parse_db(f'{qdb//4}.{(qdb%4)*25:02d}'), qdb)
        for value in ('nan','inf','-1','32','31.76','0.1','.25','1.250','1e1',' 1','1\n'):
            with self.subTest(value=value), self.assertRaises(ValueError):
                parse_db(value)


class Installation(unittest.TestCase):
    def test_plans_and_confirmation(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)/'staged'
            manifest = stage(ELF, root, IDENTITY)
            runner = Mock()
            for phase in ('remove-rdp','flash','verify','start'):
                with self.assertRaises(ValueError):
                    execute(root,phase,'yes',runner)
                runner.assert_not_called()
                execute(root,phase,confirmation(manifest,phase),runner)
                runner.assert_called_once()
                args, kw = runner.call_args
                self.assertEqual(args[0],['openocd','-f',str((root/f'{phase}.tcl').resolve())])
                self.assertTrue(kw['check']); self.assertEqual(kw['timeout'],120)
                runner.reset_mock()
            # Mutating even one image byte invalidates every execution.
            data = bytearray((root/'candidate.bin').read_bytes()); data[-1] ^= 1
            (root/'candidate.bin').write_bytes(data)
            with self.assertRaises(ValueError):
                execute(root,'remove-rdp',confirmation(manifest,'remove-rdp'),runner)
            runner.assert_not_called()

    def test_changed_plan_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)/'staged'; manifest = stage(ELF,root,IDENTITY)
            with (root/'remove-rdp.tcl').open('a') as out: out.write('reset run\n')
            runner = Mock()
            with self.assertRaises(ValueError):
                execute(root,'remove-rdp',confirmation(manifest,'remove-rdp'),runner)
            runner.assert_not_called()

    def test_identity_rejected(self):
        for key,value in (('serial','bad;exec'),('uid','0'),('device','STM32F4'),
                          ('idcode','0x20036411'),('unit','bad name'),('idcode','0x410\n')):
            identity = copy.copy(IDENTITY); identity[key] = value
            with self.subTest(key=key), tempfile.TemporaryDirectory() as tmp:
                with self.assertRaises(ValueError): stage(ELF,Path(tmp)/'staged',identity)


# Execute generated Tcl with simulated memory and command stubs. OpenOCD is never
# launched; source/init/adapter/reset/flash commands are local Tcl procedures.
TCL_MOCK = r'''
rename source original_source
proc source {args} {}
proc find {x} {return $x}
proc transport {args} {}
proc adapter {args} {}
proc gdb_port {args} {}
proc tcl_port {args} {}
proc telnet_port {args} {}
proc init {} {}
proc echo {s} {puts $s}
proc sleep {ms} {}
proc shutdown {} {puts FINISHED}
for {set i 0} {$i < 32} {incr i} {set mem($i) 0}
set mem(0) 0x50453433
set mem(1) 1
set mem(2) 128
set mem(3) 1
set mem(4) 2
set mem(7) 1
proc read_memory {address width count} {
    global mem image reads torn
    if {$address >= 0x08000000 && $address < 0x08010000} {
        return $image([expr {$address}])
    }
    if {$address == 0x20000010 && $torn} {
        incr reads
        if {$reads % 2 == 0} {return 4} else {return 2}
    }
    set out {}
    for {set i 0} {$i < $count} {incr i} {
        lappend out $mem([expr {($address-0x20000000)/4 + $i}])
    }
    return $out
}
set reads 0
set torn 0
proc write_memory {address width words} {
    global mem
    set index [expr {($address-0x20000000)/4}]
    if {$index != 8 && $mem(8) != 0} {error "payload written without invalidation"}
    foreach word $words {set mem($index) $word; incr index}
    if {$address == 0x20000020 && $mem(8) != 0} {
        set seq $mem(8)
        if {$mem(15) != ((~$seq)&0xffffffff) ||
            $mem(14) != ((0xa5c35a3c ^ $seq ^ $mem(9) ^ $mem(10) ^ $mem(11) ^ $mem(12) ^ $mem(13))&0xffffffff)} {
            error "bad published command"
        }
        if {$mem(11) == 2} {set mem(5) $mem(12); incr mem(7)}
        if {$mem(11) == 3} {set mem(6) $mem(12); incr mem(7)}
        set mem(16) $seq
        set mem(18) $mem(5)
        set mem(19) $mem(6)
        set mem(20) $mem(7)
    }
}
'''


@unittest.skipUnless(shutil.which('tclsh'), 'tclsh required for generated-plan behavior tests')
class MailboxPlans(unittest.TestCase):
    def run_plan(self,action,value=None,setup=''):
        elf = read_elf(ELF)
        image = '\n'.join(f'set image({addr}) {{{" ".join(str(b) for b in data)}}}'
                          for addr,data in elf['loads']) + '\n'
        script = TCL_MOCK + image + setup + '\n' + plan(elf,'EXAMPLE_ONLY',action,value)
        # Tclsh stdin mode can report errors and exit zero; use a file.
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp)/'test.tcl'; path.write_text(script)
            return subprocess.run(['tclsh',str(path)],capture_output=True,text=True,timeout=5)

    def test_commands(self):
        for action,value,expected in (('status',None,'ack=0'),('get',None,'ack=2'),
                                      ('set','31.75','result_qdb=127')):
            result = self.run_plan(action,value)
            self.assertEqual(result.returncode,0,result.stderr)
            self.assertIn(expected,result.stdout); self.assertIn('FINISHED',result.stdout)
        for p in range(6):
            result = self.run_plan('select',str(p))
            self.assertEqual(result.returncode,0,result.stderr)
            self.assertIn(f'result_permutation={p}', result.stdout)

    def test_refuses_stale_schema_pending_fault_and_torn_reads(self):
        for setup in ('set mem(0) 0','set mem(1) 2','set mem(2) 124',
                      'set mem(3) 2','set mem(8) 4','set torn 1',
                      'set mem(16) 0xfffffffe; set mem(8) 0xfffffffe',
                      'set image(134217728) {0}'):
            with self.subTest(setup=setup):
                result = self.run_plan('set','0',setup)
                self.assertNotEqual(result.returncode,0)
                self.assertNotIn('FINISHED',result.stdout)

    def test_install_tcl_identity_and_phase_guards(self):
        from install import tcl_plan
        import os
        for phase in ('remove-rdp','flash','verify','start'):
            manifest = {'identity': IDENTITY, 'bin_sha256':'1'*64}
            code = tcl_plan(IDENTITY,phase,ELF.with_suffix('.bin'),'1'*64)
            for bad in ('none','confirmation','uid','idcode','protection'):
                # Every generated command is stubbed; no hardware program runs.
                setup = TCL_MOCK + r"""
rename init old_init
proc init {} {puts INIT}
proc reset {args} {puts "RESET $args"}
proc stm32f1x {args} {puts "FLASHCTL $args"}
proc flash {args} {puts "FLASH $args"}
proc verify_image {args} {puts VERIFY}
rename read_memory old_read_memory
proc read_memory {address width count} {
    global bad phase
    if {$address == 0xe0042000} {
        if {$bad eq "idcode"} {return 0} else {return 0x20036410}
    }
    if {$address == 0x1ffff7e8} {
        if {$bad eq "uid"} {return {0 0 0}} else {return {0x01234567 0x89abcdef 0x13579bdf}}
    }
    if {$address == 0x4002201c} {
        if {($phase eq "remove-rdp") != ($bad eq "protection")} {return 2} else {return 0}
    }
    error "unexpected read"
}
"""
                setup += f'set bad {bad}\nset phase {phase}\n'
                env = {'PATH':os.environ.get('PATH',''),
                       'PE43703_INSTALL_CONFIRMED': confirmation(manifest,phase)}
                if bad == 'confirmation': env['PE43703_INSTALL_CONFIRMED'] = 'wrong'
                with tempfile.TemporaryDirectory() as tmp:
                    path = Path(tmp)/'test.tcl'; path.write_text(setup+code)
                    result = subprocess.run(['tclsh',str(path)],env=env,capture_output=True,text=True,timeout=5)
                if bad != 'none':
                    self.assertNotEqual(result.returncode,0,result.stderr)
                    self.assertNotIn('RESET', result.stdout)
                    self.assertNotIn('FLASHCTL', result.stdout)
                else:
                    self.assertEqual(result.returncode,0,result.stderr)
                    self.assertIn('RESET halt',result.stdout)
                    if phase == 'remove-rdp': self.assertIn('FLASHCTL unlock 0',result.stdout)
                    else: self.assertNotIn('FLASHCTL',result.stdout)
                    if phase == 'flash': self.assertIn('FLASH write_image erase',result.stdout)
                    if phase == 'start': self.assertIn('RESET run',result.stdout)

    def test_no_serial_injection(self):
        with self.assertRaises(ValueError): plan(read_elf(ELF),'x;exec bad','get')


if __name__ == '__main__':
    unittest.main()
