"""Unit tests of the Python link code: python3 -m unittest tools/groundstation/test_espdrone_link.py"""
import os
import struct
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import espdrone_link as L  # noqa: E402


class LinkTest(unittest.TestCase):
    def test_crc_check_value(self):
        # Standard check value of CRC-16/CCITT-FALSE, the same function as espnowLinkCrc16()
        self.assertEqual(L.crc16(b'123456789'), 0x29B1)

    def test_frame_sizes_match_the_c_structs(self):
        self.assertEqual(L.FRAME_LENGTH[L.PKT_CONTROL], 16)
        self.assertEqual(L.FRAME_LENGTH[L.PKT_STATUS], 10)
        self.assertEqual(L.FRAME_LENGTH[L.PKT_TELEMETRY], 32)
        self.assertEqual(L.FRAME_LENGTH[L.PKT_BRIDGE_CONFIG], 14)
        self.assertEqual(L.FRAME_LENGTH[L.PKT_BRIDGE_INFO], 28)

    def test_control_round_trip_and_clamping(self):
        f = L.control_frame(0x1FFFF, roll=1500, pitch=-2000, yaw=12.6, thrust=-5, arm=True)
        d = L.decode(f)
        self.assertEqual(d['type'], 'control')
        self.assertEqual(d['seq'], 0xFFFF)
        self.assertEqual((d['roll'], d['pitch'], d['yaw'], d['thrust']), (1000, -1000, 13, 0))
        self.assertEqual(d['flags'], L.FLAG_ARM)

    def test_control_layout(self):
        f = L.control_frame(7, roll=1, pitch=2, yaw=3, thrust=4, kill=True)
        self.assertEqual(f[:6], bytes([0xD7, 1, 1, L.FLAG_KILL, 7, 0]))
        self.assertEqual(struct.unpack('<hhhH', f[6:14]), (1, 2, 3, 4))

    def test_corrupted_frame_is_rejected(self):
        f = bytearray(L.control_frame(1, thrust=100))
        f[8] ^= 0x01
        self.assertIsNone(L.decode(bytes(f)))

    def test_telemetry_decode(self):
        body = struct.pack('<BBBBHhhhhhhH4HH', 0xD7, 1, 3, L.STATUS_ARMED, 9, 1234, -550, 17999,
                           1000, -3, 0, 300, 48, 1000, 2047, 60, 11100)
        d = L.decode(body + struct.pack('<H', L.crc16(body)))
        self.assertEqual(d['type'], 'telemetry')
        self.assertAlmostEqual(d['roll'], 12.34)
        self.assertAlmostEqual(d['pitch'], -5.5)
        self.assertEqual(d['gyro'], (100.0, -0.3, 0.0))
        self.assertEqual(d['motors'], (48, 1000, 2047, 60))
        self.assertAlmostEqual(d['battery_v'], 11.1)

    def test_parser_resynchronises(self):
        p = L.FrameParser()
        status = struct.pack('<BBBBHH', 0xD7, 1, 2, L.STATUS_FAILSAFE, 42, 3700)
        status += struct.pack('<H', L.crc16(status))
        noise = b'ets Jun  8 2016 00:22:57\r\n\xd7\x01\x02garbage'
        frames = p.feed(noise + status[:4])
        self.assertEqual(frames, [])
        frames = p.feed(status[4:] + L.control_frame(5))
        self.assertEqual([f['type'] for f in frames], ['status', 'control'])
        self.assertEqual(frames[0]['seq_ack'], 42)
        self.assertGreater(p.dropped, 0)

    def test_bridge_config(self):
        mac = L.parse_mac('24:6F:28:aa:bb:cc')
        d = L.decode(L.bridge_config_frame(mac, 6, save=True))
        self.assertEqual(d['drone_mac'], mac)
        self.assertEqual(d['channel'], 6)
        self.assertEqual(d['flags'], L.BRIDGE_CONFIG_SAVE)
        self.assertEqual(L.format_mac(mac), '24:6f:28:aa:bb:cc')
        with self.assertRaises(ValueError):
            L.bridge_config_frame(mac, 14)


if __name__ == '__main__':
    unittest.main()
