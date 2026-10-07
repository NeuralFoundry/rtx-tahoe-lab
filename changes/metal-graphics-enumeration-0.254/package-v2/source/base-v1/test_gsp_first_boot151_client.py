import struct
import unittest
import gsp_rpc
from gsp_first_boot151_client import RestrictedBackend, BindingError
from gsp_first_boot151 import FIELDS, decode_launch, capture_record


class Backend(RestrictedBackend):
    def __init__(self, raw=b''):
        self.raw = raw; self.calls = []; self.closed = False; self.closes = 0
    def _invoke(self, selector, scalars, data, output_size):
        self.calls.append((selector, scalars, data, output_size))
        return self.raw[scalars[0]:scalars[0]+scalars[1]] if selector == 15 and self.raw else bytes(output_size)
    def _close(self): self.closes += 1


def words():
    row = dict.fromkeys(FIELDS, 0)
    row.update(magic=0x525458424f4f5431, abi=2, generation=17, phase=7,
               execution_attempted=1, start_mask=1, provider_open=1, pinned=1, region_owned=1, resources_held=1)
    return row


def pack(row):
    return struct.pack('<64Q', *(row[k] for k in FIELDS))


class LaunchTests(unittest.TestCase):
    def test_wire_size(self): self.assertEqual(len(FIELDS), 64)
    def test_retained_failure_is_diagnostic(self): self.assertEqual(decode_launch(pack(words()), 17)['phase'], 7)
    def test_short_long_mutable_rejected(self):
        for raw in (bytes(511), bytes(513), bytearray(512)):
            with self.assertRaises(ValueError): decode_launch(raw, 17)
    def test_wrong_generation(self):
        with self.assertRaises(ValueError): decode_launch(pack(words()), 18)
    def test_exposure_requires_retained_resources(self):
        for key in ('provider_open', 'pinned', 'region_owned', 'resources_held'):
            row = words(); row[key] = 0
            with self.subTest(key=key), self.assertRaises(ValueError): decode_launch(pack(row), 17)
    def test_invented_success_rejected(self):
        row = words(); row['boot_passed'] = 1
        with self.assertRaises(ValueError): decode_launch(pack(row), 17)
    def test_complete_boot_and_record(self):
        row = words()
        for key in ('borrowed_owner_verified', 'post_fwsec_seal', 'fwsec_passed', 'fwsec_start_attempted',
                    'fwsec_start_accepted', 'fwsec_cleanup', 'sec2_staged', 'gsp_reset_verified',
                    'sec2_start_attempted', 'sec2_start_accepted', 'sec2_halted', 'gsp_active', 'rom_stable',
                    'boot_passed', 'status_header_valid', 'record_captured'):
            row[key] = 1
        row.update(phase=17, start_mask=3, pci_command=6, sec2_command_enabled=6,
                   sec2_imem=137, sec2_dmem=98, sec2_matched=6272, gsp_riscv=0x80,
                   record_bytes=4096, record_function=0x1002)
        self.assertTrue(decode_launch(pack(row), 17)['record_captured'])
        for key in ('sec2_command_enabled', 'sec2_imem', 'sec2_dmem', 'sec2_matched', 'gsp_riscv', 'post_fwsec_seal'):
            invalid = dict(row); invalid[key] = 0
            with self.subTest(key=key), self.assertRaises(ValueError): decode_launch(pack(invalid), 17)
    def test_invalid_boolean_and_reserved(self):
        for key, value in (('record_captured', 2), ('last_poll_count', 201), ('start_mask', 4), ('phase', 18)):
            row = words(); row[key] = value
            with self.assertRaises(ValueError): decode_launch(pack(row), 17)
    def test_fixed_requests_and_close_does_not_abort(self):
        backend = Backend(); backend.launch(); backend.launch_info(); backend.first_record(4096, 4096)
        backend.close(); backend.close()
        self.assertEqual(backend.calls, [(13, (), b'', 0), (14, (), b'', 512), (15, (4096, 4096), b'', 4096)])
        self.assertEqual(backend.closes, 1)
        with self.assertRaises(BindingError): backend.launch()
    def test_capture_bounds(self):
        for offset, size in ((-1, 1), (65536, 1), (65535, 2), (0, 4097), (0, 0), (True, 1)):
            with self.assertRaises(ValueError): Backend().first_record(offset, size)
    def test_independent_record_decode(self):
        raw = gsp_rpc.encode_record(0x1001, struct.pack('<I', 7), result=0)
        row = dict(record_bytes=4096, record_function=0x1001, record_result=0, payload_bytes=4, init_done=1, sequencer=0)
        result, details = capture_record(Backend(raw), row)
        self.assertEqual(result, raw); self.assertTrue(details['init_done_observed'])
        row['record_result'] = 1
        with self.assertRaises(ValueError): capture_record(Backend(raw), row)
    def test_corrupt_packet_rejected(self):
        raw = bytearray(gsp_rpc.encode_record(0x1001, bytes(4), result=0)); raw[80] ^= 1
        row = dict(record_bytes=4096, record_function=0x1001, record_result=0, payload_bytes=4, init_done=1, sequencer=0)
        with self.assertRaises(ValueError): capture_record(Backend(bytes(raw)), row)


if __name__ == '__main__': unittest.main()
