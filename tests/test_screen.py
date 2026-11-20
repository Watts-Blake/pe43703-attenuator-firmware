"""Compare real firmware UI/render pixels with immutable approved PNGs."""
import ctypes
import hashlib
import os
from pathlib import Path
import shlex
import struct
import subprocess
import tempfile
import unittest
import zlib

ROOT = Path(__file__).resolve().parents[1]
GOLDEN = ROOT / 'tests/screen-golden'
OUT = ROOT / 'build/screen'
SOURCES = ['firmware/ui.c', 'firmware/core.c', 'firmware/display_id.c',
           'firmware/display_config.c', 'firmware/button_cal.c',
           'firmware/adc.c', 'firmware/oled.c', 'firmware/screen.c']


def decode_png(path):
    """Read strict 1-bit grayscale fixture PNGs, checking CRCs and dimensions."""
    data = path.read_bytes()
    assert data[:8] == b'\x89PNG\r\n\x1a\n'
    pos, compressed = 8, bytearray()
    while pos < len(data):
        length = struct.unpack('>I', data[pos:pos+4])[0]
        tag, chunk = data[pos+4:pos+8], data[pos+8:pos+8+length]
        assert zlib.crc32(tag + chunk) & 0xffffffff == struct.unpack('>I', data[pos+8+length:pos+12+length])[0]
        if tag == b'IHDR':
            width, height, depth, color, comp, filtering, interlace = struct.unpack('>IIBBBBB', chunk)
            assert (depth, color, comp, filtering, interlace) == (1, 0, 0, 0, 0)
        elif tag == b'IDAT':
            compressed.extend(chunk)
        pos += length + 12
    raw = zlib.decompress(compressed)
    stride = (width + 7) // 8
    assert len(raw) == height * (stride + 1)
    rows = []
    for y in range(height):
        row = raw[y*(stride+1):(y+1)*(stride+1)]
        assert row[0] == 0
        rows.append([bool(row[1+x//8] & (128 >> (x%8))) for x in range(width)])
    return rows


def write_png(path, rows):
    """Encode already-rendered pixels; no text or layout primitives."""
    width, height = len(rows[0]), len(rows)
    raw = bytearray()
    for row in rows:
        raw.append(0)
        for x in range(0, width, 8):
            raw.append(sum(int(v) << (7-i) for i, v in enumerate(row[x:x+8])))
    def chunk(tag, payload):
        return struct.pack('>I', len(payload)) + tag + payload + struct.pack('>I', zlib.crc32(tag+payload) & 0xffffffff)
    path.write_bytes(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 1, 0, 0, 0, 0)) +
                     chunk(b'IDAT', zlib.compress(bytes(raw), 9)) + chunk(b'IEND', b''))


def pixels(frame):
    return [[bool(frame[y//8*128+x] & (1 << (y%8))) for x in range(128)] for y in range(64)]


def upscale(rows):
    return [[v for v in row for _ in range(4)] for row in rows for _ in range(4)]


class ScreenTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        shared = Path(cls.temp.name) / 'screen.so'
        subprocess.run(shlex.split(os.environ.get('CC_HOST', 'cc')) + [
            '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-Wconversion',
            '-shared', '-fPIC', '-Ifirmware', 'tests/screen_bridge.c', *SOURCES,
            '-o', str(shared)], cwd=ROOT, check=True)
        cls.lib = ctypes.CDLL(str(shared))
        cls.lib.screen_model_new.argtypes = [ctypes.c_uint32]
        cls.lib.screen_model_new.restype = ctypes.c_void_p
        for name in ('free', 'poll', 'reject'):
            fn = getattr(cls.lib, 'screen_model_' + name)
            fn.argtypes, fn.restype = [ctypes.c_void_p], None
        for name in ('hold', 'advance'):
            fn = getattr(cls.lib, 'screen_model_' + name)
            fn.argtypes = [ctypes.c_void_p] + [ctypes.c_uint32] * (2 if name == 'hold' else 1)
            fn.restype = None
        cls.lib.screen_model_command.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_char_p]
        cls.lib.screen_model_frame.argtypes = [ctypes.c_void_p, ctypes.POINTER(ctypes.c_uint8)]
        cls.lib.screen_direct_frame.argtypes = [ctypes.c_uint32] * 5 + [ctypes.c_char, ctypes.POINTER(ctypes.c_uint8)]
        OUT.mkdir(parents=True, exist_ok=True)

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def setUp(self):
        self.models = []

    def tearDown(self):
        for model in self.models:
            self.lib.screen_model_free(model)

    def model(self, boot=True):
        m = self.lib.screen_model_new(boot)
        self.models.append(m)
        return m

    def command(self, m, text, expected):
        out = ctypes.create_string_buffer(16)
        self.lib.screen_model_command(m, (text+'\n').encode(), out)
        self.assertEqual(out.value, expected.encode())

    def press(self, m, raw):
        self.lib.screen_model_hold(m, 0, 80)
        self.lib.screen_model_hold(m, raw, 80)

    def frame(self, m):
        out = (ctypes.c_uint8 * 1024)()
        self.lib.screen_model_frame(m, out)
        return bytes(out)

    def direct(self, applied, pending=None, step=1, source=1, flags=1, label='A'):
        out = (ctypes.c_uint8 * 1024)()
        self.lib.screen_direct_frame(applied, applied if pending is None else pending, step, source,
                                     flags, label.encode(), out)
        return bytes(out)

    def setup_value(self, value, step=0, label='A', usb=False):
        m = self.model()
        if label == 'B':
            self.command(m, 'label save B', 'ok saving\r\n')
            for _ in range(8):
                self.lib.screen_model_poll(m)
            self.command(m, 'label status', 'saved B\r\n')
        self.command(m, 'set '+value, 'ok '+value+'\r\n')
        if not usb:
            self.press(m, 1352)  # apply through the real local OK path
        for _ in range(step):
            self.press(m, 809)
        if usb:
            self.command(m, 'profile 1234abcd', 'ok profile\r\n')
        return m

    def assert_render(self, name, frame, expected):
        rows = pixels(frame)
        self.assertEqual(rows, expected, name)
        (OUT / (name+'.oled')).write_bytes(frame)
        for scale, rendered in [('1x', rows), ('4x', upscale(rows))]:
            path = OUT / (name+'-'+scale+'.png')
            write_png(path, rendered)
            self.assertEqual(decode_png(path), rendered)

    def test_approved_states_and_contact_panels(self):
        local = self.setup_value('12.25', step=1)
        owned = self.setup_value('20.00', step=2, label='B', usb=True)
        pending = self.setup_value('12.25')
        self.press(pending, 2048)
        feedback = self.setup_value('20.00', step=2, label='B', usb=True)
        self.press(feedback, 2048)
        error = self.setup_value('3.00', step=1, usb=True)
        self.command(error, 'profile end 1234abcd', 'ok end\r\n')
        self.lib.screen_model_reject(error)
        main = dict(zip(('main-local', 'main-usb-owned', 'pending-edit', 'locked-feedback', 'error-no-latch'),
                        (local, owned, pending, feedback, error)))
        for name, model in main.items():
            expected = decode_png(GOLDEN / ('option-A-'+name+'-1x.png'))
            self.assert_render(name, self.frame(model), expected)
            self.assertEqual(upscale(expected), decode_png(GOLDEN / ('option-A-'+name+'-4x.png')))
        down = self.setup_value('2.00')
        for _ in range(8):
            self.press(down, 1016)
        up = self.setup_value('30.00', step=2, label='B')
        self.press(up, 2048)
        unknown = self.model(boot=False)
        self.command(unknown, 'label save B', 'ok saving\r\n')
        for _ in range(8):
            self.lib.screen_model_poll(unknown)
        self.press(unknown, 809)
        edge = [self.model(), self.setup_value('0.25', label='B'), self.setup_value('1.00', step=1),
                self.setup_value('5.00', step=2, label='B', usb=True), self.setup_value('31.75', step=2),
                unknown, down, up]
        sheet = decode_png(GOLDEN / 'option-A-edge-case-contact-sheet-1x.png')
        actual_sheet = [row[:] for row in sheet]
        for i, model in enumerate(edge):
            x, y = 4 + (i%4)*132, 4 + (i//4)*76
            expected = [row[x:x+128] for row in sheet[y:y+64]]
            frame = self.frame(model)
            self.assert_render('edge-'+str(i), frame, expected)
            for j, row in enumerate(pixels(frame)):
                actual_sheet[y+j][x:x+128] = row
        self.assertEqual(actual_sheet, sheet)
        self.assertEqual(upscale(sheet), decode_png(GOLDEN / 'option-A-edge-case-contact-sheet-4x.png'))
        write_png(OUT / 'edge-case-contact-sheet-1x.png', actual_sheet)
        write_png(OUT / 'edge-case-contact-sheet-4x.png', upscale(actual_sheet))

    def test_all_values_steps_labels_pending_preserves_hero(self):
        for label in 'AB':
            for step in (1, 4, 20):
                for qdb in range(128):
                    calm = pixels(self.direct(qdb, step=step, label=label))
                    for pending in (0, 127):
                        edit = pixels(self.direct(qdb, pending, step, label=label))
                        self.assertEqual(calm[:42], edit[:42])
                    owned = pixels(self.direct(qdb, step=step, label=label, source=0, flags=5))
                    self.assertEqual(calm[9:42], owned[9:42])
                    known_error = pixels(self.direct(qdb, step=step, label=label, flags=3))
                    self.assertEqual(calm[9:42], known_error[9:42])

    def test_unknown_never_uses_unconfirmed_numeric_value(self):
        unknown = self.direct(0, flags=0)
        for qdb in range(128):
            self.assertEqual(unknown, self.direct(qdb, flags=0))
            self.assertEqual(unknown, self.direct(qdb, flags=2))
        self.assertEqual(unknown, self.direct(0xffffffff, flags=1))
        # ERROR retains actual ownership, while ignoring a stale feedback bit.
        self.assertEqual(self.direct(73, flags=4), self.direct(0, flags=12))

    def test_ownership_feedback_expiry_and_recovery(self):
        m = self.setup_value('20.00', step=2, label='B', usb=True)
        calm = self.frame(m)
        self.press(m, 2048)
        feedback = self.frame(m)
        self.assertNotEqual(calm, feedback)
        self.assertEqual(pixels(calm)[:54], pixels(feedback)[:54])
        self.lib.screen_model_hold(m, 2048, 1600)
        self.assertEqual(self.frame(m), calm)  # held key cannot refresh feedback
        self.press(m, 1352)
        self.assertEqual(self.frame(m), feedback)
        self.lib.screen_model_advance(m, 10000)
        unlocked = self.frame(m)
        self.assertEqual(unlocked, self.direct(80, step=20, source=0, label='B'))
        self.lib.screen_model_hold(m, 1352, 80)
        self.assertEqual(self.frame(m), unlocked)  # held OK across expiry is inert
        self.press(m, 1016)
        self.assertEqual(self.frame(m), self.direct(80, 60, 20, source=0, label='B'))
        self.command(m, 'set 0.00', 'ok 0.00\r\n')
        self.assertEqual(self.frame(m), self.direct(0, step=20, source=0, label='B'))
        self.lib.screen_model_reject(m)
        self.assertEqual(self.frame(m), self.direct(0, step=20, flags=3, label='B'))
        self.command(m, 'set 1.00', 'ok 1.00\r\n')
        self.assertEqual(self.frame(m), self.direct(4, step=20, source=0, label='B'))
        no_boot = self.model(boot=False)
        self.assertEqual(self.frame(no_boot), self.direct(73, flags=0))
        self.command(no_boot, 'set 1.00', 'ok 1.00\r\n')
        self.assertEqual(self.frame(no_boot), self.direct(4, source=0))

    def test_source_and_failed_ownership_commands_never_invent_lock(self):
        m = self.model()
        self.assertEqual(self.frame(m), self.direct(0))  # boot is displayed as LOCAL
        self.command(m, 'profile keep 1234abcd', 'err lock\r\n')
        self.assertEqual(self.frame(m), self.direct(0))
        self.command(m, 'set 3.00', 'ok 3.00\r\n')
        self.assertEqual(self.frame(m), self.direct(12, source=0))
        self.command(m, 'profile set 1234abcd 20.00', 'err lock\r\n')
        self.assertEqual(self.frame(m), self.direct(12, source=0))
        self.command(m, 'profile 1234abcd', 'ok profile\r\n')
        self.command(m, 'profile end 11111111', 'err lock\r\n')
        self.assertEqual(self.frame(m), self.direct(12, source=0, flags=5))
        self.lib.screen_model_reject(m)
        self.assertEqual(self.frame(m), self.direct(12, source=0, flags=7))
        self.command(m, 'profile end 1234abcd', 'ok end\r\n')
        self.assertEqual(self.frame(m), self.direct(12, source=0, flags=3))
        self.command(m, 'set 3.00', 'ok 3.00\r\n')
        self.press(m, 1352)
        self.command(m, 'profile 1234abcd', 'ok profile\r\n')
        self.assertEqual(self.frame(m), self.direct(12, source=1, flags=5))
        self.command(m, 'profile end 1234abcd', 'ok end\r\n')
        self.assertEqual(self.frame(m), self.direct(12, source=1))

    def test_fixture_integrity(self):
        for line in (GOLDEN / 'SHA256SUMS').read_text().splitlines():
            digest, name = line.split('  ')
            self.assertEqual(hashlib.sha256((GOLDEN/name).read_bytes()).hexdigest(), digest)


if __name__ == '__main__':
    unittest.main()
