import copy
import json
import os
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
from install import stage, execute, confirmation, tcl_plan, option_values, identity_plan

ELF = Path(__file__).resolve().parents[1]/'build/attenuator.elf'
FIXTURES = Path(__file__).resolve().parent/'fixtures/openocd-012'
PROTECTED = {'device':'STM32F103C8T6', 'unit':'example-unit', 'serial':'EXAMPLE_ONLY',
    'profile':'native-swd', 'dpidr':'0x1ba01477', 'idcode':'0x20036410',
    'kind':'protected-physical', 'physical_confirmation':'sole-wired-target-confirmed',
    'uid':None, 'obr':'0x03fffffe', 'wrpr':'0xffffffff'}
IDENTITY = dict(PROTECTED, kind='uid-bound', uid='0123456789abcdef13579bdf', obr='0x03fffffc')


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


# Real included OpenOCD scripts execute below. Only low-level physical commands
# are stubbed, including the modeled normal-examination hook and FPEC hardware.
# This tests configured effects; it cannot prove absence of opaque adapter effects.
TCL_MODEL = r'''
proc find {x} {global fixtures; return "$fixtures/$x"}
set speed 0
set deferred 0
set examined 0
set resets 0
set selected {}
set driver {}
set hook {}
set initializations 0
proc adapter {op args} {
    global speed driver
    if {$op eq "speed"} {set speed [lindex $args 0]}
    if {$op eq "driver"} {if {[llength $args]} {set driver [lindex $args 0]} else {return $driver}}
    if {$op eq "name"} {return $driver}
}
proc transport {op args} {global transport_name; if {[llength $args]} {set transport_name [lindex $args 0]}; return $transport_name}
proc using_jtag {} {return 0}
proc using_swd {} {return 1}
proc using_hla {} {return 0}
proc swd {args} {}
proc st-link {args} {}
proc dap {args} {}
proc stm32f1x.dap {args} {global bad; if {$bad eq "dpidr"} {return 0}; return 0x1ba01477}
proc cortex_m {args} {}
proc reset_config {args} {}
proc tpiu {args} {}
proc stm32f1x.tpiu {args} {}
proc flash {args} {if {[lindex $args 0] ne "bank"} {puts "FLASH $args"}}
proc add_usage_text {args} {}
proc add_help_text {args} {}
proc target {args} {}
proc targets {name} {global selected; set selected $name}
proc stm32f1x.cpu {op args} {
    global hook deferred examined resets bad
    if {$op eq "configure"} {
        if {[lsearch -exact $args -defer-examine] >= 0} {set deferred 1}
        set pos [lsearch -exact $args -event]
        if {$pos >= 0 && [lindex $args [expr {$pos+1}]] eq "examine-end"} {
            set hook [lindex $args [expr {$pos+2}]]
        }
    }
    if {$op eq "arp_examine"} {
        if {$resets && $bad eq "reset-examine"} {error "post-reset examination failed"}
        set examined 1
        puts CORE_EXAMINE
        uplevel #0 $hook
    }
}
proc init {} {
    global deferred hook speed selected initializations examined
    incr initializations
    puts "INIT speed=$speed target=$selected deferred=$deferred"
    if {!$deferred} {set examined 1; puts IMPLICIT_CORE_EXAMINE; uplevel #0 $hook}
}
proc poll {args} {}
proc gdb_port {args} {}
proc tcl_port {args} {}
proc telnet_port {args} {}
proc echo {s} {puts $s}
proc sleep {ms} {}
proc shutdown {} {puts FINISHED}
proc reset {args} {
    global examined deferred resets
    incr resets
    if {$deferred} {set examined 0}
    puts "RESET $args"
}
proc halt {args} {
    global examined
    if {!$examined} {error "Target not examined yet"}
    puts HALTED
}
proc verify_image {args} {global bad; if {$bad eq "verify"} {error "verify failed"}; puts VERIFY}
proc stm32f1x {args} {error "unsafe unlock handler must never run"}
for {set i 0} {$i < 32} {incr i} {set mem($i) 0}
set mem(0) 0x50453433
set mem(1) 1
set mem(2) 128
set mem(3) 1
set mem(4) 2
set mem(7) 1
set bad none
set phase mailbox
set reads 0
set torn 0
set sr 0
set cr 0x80
set obr 0x03fffffc
set wrpr 0xffffffff
set opt(0) 0x5aa5
for {set i 1} {$i < 8} {incr i} {set opt($i) 0x00ff}
proc read_memory {address width count} {
    global mem image reads torn bad phase obr wrpr opt sr cr selected examined resets
    if {$selected eq "stm32f1x.cpu" && !$examined} {error "Target not examined yet"}
    if {$address >= 0x08000000 && $address < 0x08010000} {return $image([expr {$address}])}
    if {$address == 0xe0042004} {return 0}
    if {$address == 0xe0042000} {
        puts IDCODE_READ
        if {$bad eq "idcode" || ($resets && $bad eq "reset-idcode")} {return 0}
        return 0x20036410
    }
    if {$address == 0x1ffff7e8} {
        puts UID_READ
        if {$phase eq "remove-rdp"} {error "protected UID read forbidden"}
        if {$bad eq "uid-unavailable"} {error "UID unreadable"}
        if {$bad eq "uid"} {return {0 0 0}}
        return {0x01234567 0x89abcdef 0x13579bdf}
    }
    if {$address == 0x1ffff7e0} {
        puts DENSITY_READ
        if {$phase eq "remove-rdp"} {error "protected density read forbidden"}
        return 64
    }
    if {$address == 0x4002201c} {
        if {$bad eq "option-read"} {error "OBR read failed"}
        if {$bad eq "protection"} {return [expr {$obr ^ 2}]}
        return $obr
    }
    if {$address == 0x40022020} {if {$bad eq "wrpr-read"} {error "WRPR read failed"}; return $wrpr}
    if {$address == 0x40021000} {return 3}
    if {$address == 0x4002200c} {if {$bad eq "busy"} {return 1}; return $sr}
    if {$address == 0x40022010} {return $cr}
    if {$address >= 0x1ffff800 && $address < 0x1ffff810} {
        set out {}
        for {set n 0} {$n < $count} {incr n} {lappend out $opt([expr {($address-0x1ffff800)/2+$n}])}
        return $out
    }
    if {$address == 0x20000010 && $torn} {incr reads; return [expr {$reads % 2 ? 2 : 4}]}
    set out {}
    for {set n 0} {$n < $count} {incr n} {lappend out $mem([expr {($address-0x20000000)/4+$n}])}
    return $out
}
proc write_memory {address width words} {
    global mem cr sr opt bad selected examined
    if {$selected eq "stm32f1x.cpu" && !$examined} {error "Target not examined yet"}
    if {$address == 0xe0042004} {puts IMPLICIT_TARGET_WRITE; return}
    if {$address >= 0x40000000 && $address < 0x50000000} {
        puts [format "MMIO_WRITE %x" $address]
        set value [lindex $words 0]
        if {$address == 0x40022008 && $value == 0xcdef89ab} {set cr 0x200}
        if {$address == 0x4002200c} {set sr [expr {$sr & ~$value}]}
        if {$address == 0x40022010} {
            set cr $value
            if {$value == 0x260} {
                puts OPTION_ERASE
                if {$bad eq "erase-fail"} {set sr 0x10} else {
                    for {set n 0} {$n < 8} {incr n} {set opt($n) 0xffff}
                    set sr 0x20
                }
            }
        }
        return
    }
    if {$address >= 0x1ffff800 && $address < 0x1ffff810} {
        if {$cr != 0x210 || $width != 16} {error "invalid option programming state/width"}
        set value [lindex $words 0]
        puts [format "OPTION_PROGRAM %x %x" $address $value]
        if {$bad eq "write-fail"} {set sr 0x4; return}
        if {$bad eq "write-transport"} {error "option bus failure"}
        set opt([expr {($address-0x1ffff800)/2}]) [expr {$value | ((~$value & 255) << 8)}]
        set sr 0x20
        return
    }
    puts MAILBOX_WRITE
    set index [expr {($address-0x20000000)/4}]
    if {$index != 8 && $mem(8) != 0} {error "payload without invalidation"}
    foreach word $words {set mem($index) $word; incr index}
    if {$address == 0x20000020 && $mem(8) != 0} {
        set seq $mem(8)
        if {$mem(15) != ((~$seq)&0xffffffff) ||
            $mem(14) != ((0xa5c35a3c ^ $seq ^ $mem(9) ^ $mem(10) ^ $mem(11) ^ $mem(12) ^ $mem(13))&0xffffffff)} {error "bad published command"}
        if {$mem(11) == 2} {set mem(5) $mem(12); incr mem(7)}
        if {$mem(11) == 3} {set mem(6) $mem(12); incr mem(7)}
        set mem(16) $seq
        set mem(18) $mem(5)
        set mem(19) $mem(6)
        set mem(20) $mem(7)
    }
}
proc mww {address value} {write_memory $address 32 [list $value]}
'''


def run_tcl(code, setup='', env=None):
    elf = read_elf(ELF)
    image = '\n'.join(f'set image({addr}) {{{" ".join(str(b) for b in data)}}}'
                      for addr,data in elf['loads'])+'\n'
    script = f'set fixtures {{{FIXTURES}}}\n'+TCL_MODEL+image+setup+'\n'+code
    with tempfile.TemporaryDirectory() as tmp:
        path = Path(tmp)/'test.tcl'; path.write_text(script)
        return subprocess.run(['tclsh',str(path)],env=env,capture_output=True,text=True,timeout=5)


@unittest.skipUnless(shutil.which('tclsh'), 'tclsh required for generated-plan behavior tests')
class Plans(unittest.TestCase):
    def test_protected_identity_collection_is_read_only(self):
        r=run_tcl(identity_plan('EXAMPLE_ONLY'),'set phase remove-rdp\nset obr 0x03fffffe')
        self.assertEqual(r.returncode,0,r.stderr)
        self.assertIn('DPIDR=0x1ba01477',r.stdout)
        self.assertIn('OBR=0x03fffffe',r.stdout)
        for text in ('MMIO_WRITE','CORE_EXAMINE','UID_READ','DENSITY_READ','RESET'):
            self.assertNotIn(text,r.stdout)

    def mailbox(self,action,value=None,setup='',profile='native-swd'):
        return run_tcl(plan(read_elf(ELF),'EXAMPLE_ONLY',action,value,
                           idcode=IDENTITY['idcode'],uid=IDENTITY['uid'],profile=profile),setup)

    def test_dependency_model_reproduces_original_hook(self):
        # Verify that the included real script WOULD write target state if not disabled.
        code = 'source [find target/stm32f1x.cfg]\ninit\n'
        r=run_tcl(code,'set selected stm32f1x.cpu')
        self.assertEqual(r.returncode,0,r.stderr)
        self.assertIn('IMPLICIT_TARGET_WRITE',r.stdout)
        self.assertIn('speed=1000',r.stdout)

    def test_commands_and_both_native_profiles(self):
        for profile in ('native-swd','native-dapdirect'):
            for action,value,expected in (('status',None,'ack=0'),('get',None,'ack=2'),('set','31.75','result_qdb=127')):
                r=self.mailbox(action,value,profile=profile)
                self.assertEqual(r.returncode,0,r.stderr)
                self.assertIn(expected,r.stdout)
                self.assertIn('speed=100 target=attenuator.mem deferred=1',r.stdout)
                self.assertNotIn('IMPLICIT_',r.stdout)
                self.assertNotIn('CORE_EXAMINE',r.stdout)
                if action=='status': self.assertNotIn('MAILBOX_WRITE',r.stdout)
        for p in range(6):
            r=self.mailbox('select',str(p))
            self.assertEqual(r.returncode,0,r.stderr)
            self.assertIn(f'result_permutation={p}',r.stdout)

    def test_wrong_mcu_with_same_image_never_touches_mailbox(self):
        for bad in ('dpidr','idcode','uid','uid-unavailable'):
            for action,value in (('status',None),('get',None),('set','20'),('select','3')):
                r=self.mailbox(action,value,'set bad '+bad)
                self.assertNotEqual(r.returncode,0)
                self.assertNotIn('MAILBOX_WRITE',r.stdout)
                self.assertNotIn('ack=',r.stdout)
                self.assertNotIn('IMPLICIT_',r.stdout)

    def test_refuses_schema_pending_fault_and_torn_reads(self):
        for setup in ('set mem(0) 0','set mem(1) 2','set mem(2) 124','set mem(3) 2',
                      'set mem(8) 4','set torn 1','set mem(16) 0xfffffffe; set mem(8) 0xfffffffe',
                      'set image(134217728) {0}'):
            r=self.mailbox('set','0',setup)
            self.assertNotEqual(r.returncode,0)
            self.assertNotIn('FINISHED',r.stdout)
            self.assertNotIn('IMPLICIT_',r.stdout)

    def install(self,phase,bad='none',confirm=True):
        i=PROTECTED if phase in ('remove-rdp','post-rdp-status') else IDENTITY
        code=tcl_plan(i,phase,ELF.with_suffix('.bin'),'1'*64)
        env={'PATH':os.environ.get('PATH',''),'PE43703_INSTALL_CONFIRMED':
             confirmation({'identity':i,'bin_sha256':'1'*64},phase) if confirm else 'wrong'}
        obr=i['obr'] if phase!='post-rdp-status' else '0x03fffffc'
        return run_tcl(code,f'set phase {phase}\nset bad {bad}\nset obr {obr}',env)

    def test_two_stage_identity_success_and_no_implicit_writes(self):
        for phase in ('remove-rdp','post-rdp-status','flash','verify','start'):
            r=self.install(phase)
            self.assertEqual(r.returncode,0,r.stderr)
            self.assertIn('PE43703_PHASE_COMPLETE '+phase,r.stdout)
            self.assertIn('speed=100 target=attenuator.mem deferred=1',r.stdout)
            self.assertNotIn('IMPLICIT_',r.stdout)
            if phase=='remove-rdp':
                self.assertNotIn('UID_READ',r.stdout)
                self.assertNotIn('DENSITY_READ',r.stdout)
                self.assertEqual(r.stdout.count('OPTION_ERASE\n'),1)
                self.assertIn('RDP_PROGRAMMED_POR_REQUIRED',r.stdout)
                programs=[x for x in r.stdout.splitlines() if x.startswith('OPTION_PROGRAM')]
                self.assertEqual(len(programs),8)
                self.assertEqual(programs[-1],'OPTION_PROGRAM 1ffff800 a5')
                self.assertNotIn('FLASH ',r.stdout)
            elif phase=='post-rdp-status':
                self.assertIn('POST_POR_UID=0123456789abcdef13579bdf',r.stdout)
                self.assertNotIn('RESET',r.stdout)
                self.assertNotIn('CORE_EXAMINE',r.stdout)
                self.assertNotIn('MMIO_WRITE',r.stdout)
            else:
                self.assertLess(r.stdout.index('UID_READ'),r.stdout.index('CORE_EXAMINE'))
                self.assertIn('RESET halt',r.stdout)
                self.assertIn('VERIFY',r.stdout)
                if phase=='start': self.assertIn('RESET run',r.stdout)

    def test_phase_identity_failures_before_mutation(self):
        for phase in ('remove-rdp','post-rdp-status','flash','verify','start'):
            bads=['idcode','dpidr','protection','option-read','wrpr-read']
            if phase!='remove-rdp': bads+=['uid','uid-unavailable']
            for bad in bads:
                r=self.install(phase,bad)
                self.assertNotEqual(r.returncode,0,(phase,bad,r.stderr))
                self.assertNotIn('MMIO_WRITE',r.stdout)
                self.assertNotIn('CORE_EXAMINE',r.stdout)
                self.assertNotIn('RESET',r.stdout)
            r=self.install(phase,confirm=False)
            self.assertNotEqual(r.returncode,0)
            self.assertNotIn('INIT',r.stdout)

    def test_reset_deferral_regression_and_post_reset_guards(self):
        for phase in ('remove-rdp','flash','verify','start'):
            i=PROTECTED if phase=='remove-rdp' else IDENTITY
            code=tcl_plan(i,phase,ELF.with_suffix('.bin'),'1'*64)
            env={'PATH':os.environ.get('PATH',''),'PE43703_INSTALL_CONFIRMED':
                 confirmation({'identity':i,'bin_sha256':'1'*64},phase)}
            setup=f'set phase {phase}\nset obr {i["obr"]}'
            # Execute the former lifecycle against a reset that invalidates
            # examination. It must fail before FPEC/image operations.
            old=code.replace('stm32f1x.cpu arp_examine\nhalt 1000\n','',1)
            r=run_tcl(old,setup,env)
            self.assertNotEqual(r.returncode,0)
            self.assertIn('Target not examined yet',r.stderr)
            self.assertNotIn('MMIO_WRITE',r.stdout)
            self.assertNotIn('VERIFY',r.stdout)
            r=self.install(phase)
            self.assertEqual(r.returncode,0,r.stderr)
            events=r.stdout.splitlines()
            reset=events.index('RESET halt')
            self.assertLess(events.index('IDCODE_READ'),events.index('CORE_EXAMINE'))
            self.assertEqual(events[reset+1:reset+4],['CORE_EXAMINE','HALTED','IDCODE_READ'])
            for bad in ('reset-examine','reset-idcode'):
                r=self.install(phase,bad)
                self.assertNotEqual(r.returncode,0,(phase,bad,r.stderr))
                for forbidden in ('MMIO_WRITE','OPTION_ERASE_BEGIN','FLASH ','VERIFY',
                                  'PE43703_PHASE_COMPLETE'):
                    self.assertNotIn(forbidden,r.stdout)

    def test_fpec_partial_failure_never_claims_success_or_retries(self):
        for bad in ('busy','erase-fail','write-fail','write-transport'):
            r=self.install('remove-rdp',bad)
            self.assertNotEqual(r.returncode,0,(bad,r.stderr))
            self.assertNotIn('RDP_PROGRAMMED_POR_REQUIRED',r.stdout)
            self.assertNotIn('PE43703_PHASE_COMPLETE',r.stdout)
            self.assertLessEqual(r.stdout.count('OPTION_ERASE\n'),1)

    def test_verify_failure_never_starts(self):
        r=self.install('start','verify')
        self.assertNotEqual(r.returncode,0)
        self.assertNotIn('RESET run',r.stdout)


class Installation(unittest.TestCase):
    def test_execute_runs_real_included_config_and_checked_fpec(self):
        for bad in ('none','erase-fail','write-fail','option-read','wrpr-read'):
            with self.subTest(bad=bad),tempfile.TemporaryDirectory() as tmp:
                root=Path(tmp)/'stage';m=stage(ELF,root,PROTECTED)
                def runner(argv,**kw):
                    return run_tcl(Path(argv[-1]).read_text(),
                                   f'set phase remove-rdp\nset obr 0x03fffffe\nset bad {bad}',kw['env'])
                if bad=='none':
                    execute(root,'remove-rdp',confirmation(m,'remove-rdp'),runner)
                else:
                    with self.assertRaises(ValueError):
                        execute(root,'remove-rdp',confirmation(m,'remove-rdp'),runner)
                r=json.loads((root/'attempt-remove-rdp/receipt.json').read_text())
                self.assertEqual(r['completion'],'option-programmed-POR-required' if bad=='none' else 'unknown')

    def test_identity_validation_and_stage_separation(self):
        for key,value in (('serial','bad;exec'),('device','STM32F4'),('idcode','0x20036411'),
                          ('unit','bad name'),('idcode','0x20036410\n'),('uid','0'*24)):
            i=dict(IDENTITY);i[key]=value
            with self.subTest(key=key),tempfile.TemporaryDirectory() as tmp:
                with self.assertRaises(ValueError): stage(ELF,Path(tmp)/'stage',i)
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp)
            a=stage(ELF,root/'erase',PROTECTED)
            b=stage(ELF,root/'program',IDENTITY)
            self.assertEqual(set(a['plans']),{'remove-rdp','post-rdp-status'})
            self.assertEqual(set(b['plans']),{'flash','verify','start'})
        for i in (dict(PROTECTED,uid=IDENTITY['uid']),dict(PROTECTED,physical_confirmation=None),dict(IDENTITY,uid=None)):
            with tempfile.TemporaryDirectory() as tmp,self.assertRaises((ValueError,TypeError)):
                stage(ELF,Path(tmp)/'stage',i)

    def test_execute_transcript_confirmation_and_one_attempt(self):
        for identity in (PROTECTED,IDENTITY):
            with tempfile.TemporaryDirectory() as tmp:
                root=Path(tmp)/'stage';m=stage(ELF,root,identity)
                for phase in m['plans']:
                    runner=Mock(return_value=subprocess.CompletedProcess([],0,
                        f'RDP_PROGRAMMED_POR_REQUIRED\nPE43703_PHASE_COMPLETE {phase}\n',''))
                    with self.assertRaises(ValueError): execute(root,phase,'yes',runner)
                    runner.assert_not_called()
                    execute(root,phase,confirmation(m,phase),runner)
                    runner.assert_called_once()
                    self.assertFalse(runner.call_args.kwargs['check'])
                    self.assertTrue((root/f'attempt-{phase}/stdout.log').is_file())
                    with self.assertRaises(FileExistsError): execute(root,phase,confirmation(m,phase),runner)
                    runner.assert_called_once()

    def test_false_success_option_failures_and_missing_evidence(self):
        outputs=[('stm32x failed to erase options',''),('','stm32x failed to unlock device'),
                 ('PE43703_PHASE_COMPLETE remove-rdp',''),
                 ('RDP_PROGRAMMED_POR_REQUIRED\nPE43703_PHASE_COMPLETE remove-rdp','Error: transport')]
        for stdout,stderr in outputs:
            with tempfile.TemporaryDirectory() as tmp:
                root=Path(tmp)/'stage';m=stage(ELF,root,PROTECTED)
                runner=Mock(return_value=subprocess.CompletedProcess([],0,stdout,stderr))
                with self.assertRaises(ValueError): execute(root,'remove-rdp',confirmation(m,'remove-rdp'),runner)
                r=json.loads((root/'attempt-remove-rdp/receipt.json').read_text())
                self.assertEqual(r['completion'],'unknown')
                self.assertEqual((root/'attempt-remove-rdp/stdout.log').read_text(),stdout)
                self.assertEqual((root/'attempt-remove-rdp/stderr.log').read_text(),stderr)

    def test_timeout_retains_unknown_completion_and_blocks_replay(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp)/'stage';m=stage(ELF,root,PROTECTED)
            runner=Mock(side_effect=subprocess.TimeoutExpired('stub',120,output=b'partial',stderr=b'error'))
            with self.assertRaises(subprocess.TimeoutExpired): execute(root,'remove-rdp',confirmation(m,'remove-rdp'),runner)
            self.assertEqual((root/'attempt-remove-rdp/stdout.log').read_bytes(),b'partial')
            with self.assertRaises(FileExistsError): execute(root,'remove-rdp',confirmation(m,'remove-rdp'),runner)
            runner.assert_called_once()

    def test_changed_image_or_plan_is_rejected_before_attempt(self):
        for name in ('candidate.bin','remove-rdp.tcl'):
            with tempfile.TemporaryDirectory() as tmp:
                root=Path(tmp)/'stage';m=stage(ELF,root,PROTECTED)
                path=root/name;path.write_bytes(path.read_bytes()+b'bad')
                runner=Mock()
                with self.assertRaises(ValueError): execute(root,'remove-rdp',confirmation(m,'remove-rdp'),runner)
                runner.assert_not_called()
                self.assertFalse((root/'attempt-remove-rdp').exists())

    def test_option_snapshot_preserves_all_bytes(self):
        i=dict(PROTECTED,obr='0x012345fe',wrpr='0x76543210')
        self.assertEqual(option_values(i),[0x7f,0xd1,0x48,0x10,0x32,0x54,0x76,0xa5])
        if shutil.which('tclsh'):
            token=confirmation({'identity':i,'bin_sha256':'1'*64},'remove-rdp')
            r=run_tcl(tcl_plan(i,'remove-rdp',ELF.with_suffix('.bin'),'1'*64),
                      'set phase remove-rdp\nset obr 0x012345fe\nset wrpr 0x76543210',
                      {'PATH':os.environ.get('PATH',''),'PE43703_INSTALL_CONFIRMED':token})
            self.assertEqual(r.returncode,0,r.stderr)
            actual=[line for line in r.stdout.splitlines() if line.startswith('OPTION_PROGRAM')]
            self.assertEqual(actual,['OPTION_PROGRAM 1ffff802 7f','OPTION_PROGRAM 1ffff804 d1',
                'OPTION_PROGRAM 1ffff806 48','OPTION_PROGRAM 1ffff808 10','OPTION_PROGRAM 1ffff80a 32',
                'OPTION_PROGRAM 1ffff80c 54','OPTION_PROGRAM 1ffff80e 76','OPTION_PROGRAM 1ffff800 a5'])

    def test_no_serial_injection(self):
        with self.assertRaises(ValueError):
            plan(read_elf(ELF),'x;exec bad','get',idcode=IDENTITY['idcode'],uid=IDENTITY['uid'])


if __name__ == '__main__':
    unittest.main()
