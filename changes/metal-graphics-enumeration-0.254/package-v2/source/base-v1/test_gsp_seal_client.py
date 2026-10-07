import struct
import unittest

from gsp_seal_client import RestrictedBackend, BindingError
from gsp_seal_rehearsal import decode_seal


def valid_words():
    return [0x5254585345414c31, 1, 17, 3, 1, 1, 0, 0, 1,
            66404352, 677, 15535, 0xffffffff, 0xffffffff, 0xffffffff,
            1, 1, 1, 0xfb000000, 0x824000000, 0x820000000,
            0x7fffffe00000, 0xa1, 0x12345678, 0x173e00000, 0x17ff00000,
            511, 263, 0, 1, 0, 0]


class Backend(RestrictedBackend):
    def __init__(self): self.calls = []; self.closed = False
    def _invoke(self, selector, scalars, data, output_size):
        self.calls.append((selector, scalars, data, output_size))
        return bytes(output_size)
    def _close(self): pass


class SealTests(unittest.TestCase):
    def test_valid_seal(self):
        self.assertTrue(decode_seal(struct.pack('<32Q', *valid_words()), 17)['sealed'])

    def test_wrong_generation(self):
        with self.assertRaises(ValueError): decode_seal(struct.pack('<32Q', *valid_words()), 18)

    def test_partial_or_extra(self):
        for raw in (b'', bytes(255), bytes(257), bytearray(256)):
            with self.assertRaises(ValueError): decode_seal(raw, 17)

    def test_each_seal_gate(self):
        for index in (0, 1, 3, 4, 5, 9, 10, 11, 15, 16, 17, 24, 25, 26, 27, 29):
            words = valid_words(); words[index] ^= 1
            with self.subTest(index=index), self.assertRaises(ValueError):
                decode_seal(struct.pack('<32Q', *words), 17)

    def test_device_exposure_forbidden(self):
        for index in (6, 7, 28, 30, 31):
            words = valid_words(); words[index] = 1
            with self.subTest(index=index), self.assertRaises(ValueError):
                decode_seal(struct.pack('<32Q', *words), 17)

    def test_released_status_is_diagnostic(self):
        words = valid_words(); words[3] = 8
        for index in (4, 8, 29): words[index] = 0
        self.assertFalse(decode_seal(struct.pack('<32Q', *words), 17)['sealed'])

    def test_fixed_request_shapes(self):
        backend = Backend(); backend.prepare_startup(); backend.seal()
        self.assertEqual(len(backend.seal_info()), 256)
        self.assertEqual(backend.calls, [(10, (), b'', 0), (11, (), b'', 0), (12, (), b'', 256)])

    def test_closed_backend_rejects_new_selectors(self):
        backend = Backend(); backend.close()
        for method in (backend.prepare_startup, backend.seal, backend.seal_info):
            with self.assertRaises(BindingError): method()


if __name__ == '__main__': unittest.main()
