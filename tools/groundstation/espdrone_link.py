"""Wire format of the ESPDrone ESP-NOW link and of the USB bridge, in plain Python.

Mirrors components/core/crazyflie/hal/interface/espnow_link.h (frames between transmitter and
drone) and examples/espnow_bridge/main/bridge_protocol.h (frames between host and bridge). Every
frame: magic 0xD7, version 1, type, ..., CRC-16/CCITT-FALSE of the bytes before it, little endian.

This module has no dependencies; only SerialLink needs pyserial. It is meant to be ported as is
to the phone app.
"""
import struct
import threading
import time

MAGIC = 0xD7
VERSION = 1

PKT_CONTROL = 1
PKT_STATUS = 2
PKT_TELEMETRY = 3
PKT_BRIDGE_CONFIG = 0x10
PKT_BRIDGE_INFO = 0x11

FLAG_ARM = 1 << 0
FLAG_KILL = 1 << 1

STATUS_ARMED = 1 << 0
STATUS_KILLED = 1 << 1
STATUS_BATTERY_LOW = 1 << 2
STATUS_FAILSAFE = 1 << 3

BRIDGE_CONFIG_SAVE = 1 << 0
BRIDGE_INFO_UNICAST = 1 << 0
BRIDGE_INFO_HOST_ACTIVE = 1 << 1
BRIDGE_INFO_DRONE_ACTIVE = 1 << 2

BRIDGE_BAUDRATE = 460800
AXIS_MAX = 1000
THRUST_MAX = 1000

# Body without the trailing CRC, after magic, version, type
_CONTROL = struct.Struct('<BBBBHhhhH')            # 14 + crc = 16
_STATUS = struct.Struct('<BBBBHH')                # 8 + crc = 10
_TELEMETRY = struct.Struct('<BBBBHhhhhhhH4HH')    # 30 + crc = 32
_BRIDGE_CONFIG = struct.Struct('<BBBB6sBB')       # 12 + crc = 14
_BRIDGE_INFO = struct.Struct('<BBBB6s6sBBHHHH')   # 26 + crc = 28

FRAME_LENGTH = {
    PKT_CONTROL: _CONTROL.size + 2,
    PKT_STATUS: _STATUS.size + 2,
    PKT_TELEMETRY: _TELEMETRY.size + 2,
    PKT_BRIDGE_CONFIG: _BRIDGE_CONFIG.size + 2,
    PKT_BRIDGE_INFO: _BRIDGE_INFO.size + 2,
}


def crc16(data: bytes) -> int:
    """CRC-16/CCITT-FALSE: polynomial 0x1021, init 0xFFFF, no reflection, no final xor."""
    crc = 0xFFFF
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) if crc & 0x8000 else (crc << 1)
            crc &= 0xFFFF
    return crc


def _seal(body: bytes) -> bytes:
    return body + struct.pack('<H', crc16(body))


def _clamp(value, low, high):
    return max(low, min(high, int(round(value))))


def control_frame(seq, roll=0, pitch=0, yaw=0, thrust=0, arm=False, kill=False) -> bytes:
    """Sticks: roll, pitch, yaw -1000..1000 (positive = right, forward, clockwise), thrust 0..1000."""
    flags = (FLAG_ARM if arm else 0) | (FLAG_KILL if kill else 0)
    return _seal(_CONTROL.pack(MAGIC, VERSION, PKT_CONTROL, flags, seq & 0xFFFF,
                               _clamp(roll, -AXIS_MAX, AXIS_MAX), _clamp(pitch, -AXIS_MAX, AXIS_MAX),
                               _clamp(yaw, -AXIS_MAX, AXIS_MAX), _clamp(thrust, 0, THRUST_MAX)))


def bridge_config_frame(drone_mac: bytes, channel: int, save=False) -> bytes:
    """drone_mac: 6 bytes, all zero for broadcast. channel: the drone's Wi-Fi channel."""
    if len(drone_mac) != 6 or not 1 <= channel <= 13:
        raise ValueError('need a 6-byte MAC and a channel from 1 to 13')
    return _seal(_BRIDGE_CONFIG.pack(MAGIC, VERSION, PKT_BRIDGE_CONFIG,
                                     BRIDGE_CONFIG_SAVE if save else 0, bytes(drone_mac), channel, 0))


def parse_mac(text: str) -> bytes:
    parts = text.split(':')
    if len(parts) != 6:
        raise ValueError('MAC must look like aa:bb:cc:dd:ee:ff')
    return bytes(int(p, 16) for p in parts)


def format_mac(mac: bytes) -> str:
    return ':'.join('%02x' % b for b in mac)


def decode(frame: bytes):
    """Returns a dict for a valid frame, None otherwise."""
    if len(frame) < 5 or frame[0] != MAGIC or frame[1] != VERSION:
        return None
    ftype = frame[2]
    if FRAME_LENGTH.get(ftype) != len(frame):
        return None
    if struct.unpack('<H', frame[-2:])[0] != crc16(frame[:-2]):
        return None
    body = frame[:-2]
    if ftype == PKT_CONTROL:
        _, _, _, flags, seq, roll, pitch, yaw, thrust = _CONTROL.unpack(body)
        return dict(type='control', flags=flags, seq=seq, roll=roll, pitch=pitch, yaw=yaw, thrust=thrust)
    if ftype == PKT_STATUS:
        _, _, _, flags, seq_ack, battery_mv = _STATUS.unpack(body)
        return dict(type='status', flags=flags, seq_ack=seq_ack, battery_v=battery_mv / 1000.0)
    if ftype == PKT_TELEMETRY:
        v = _TELEMETRY.unpack(body)
        return dict(type='telemetry', flags=v[3], seq=v[4],
                    roll=v[5] / 100.0, pitch=v[6] / 100.0, yaw=v[7] / 100.0,
                    gyro=(v[8] / 10.0, v[9] / 10.0, v[10] / 10.0),
                    thrust=v[11], motors=tuple(v[12:16]), battery_v=v[16] / 1000.0)
    if ftype == PKT_BRIDGE_CONFIG:
        _, _, _, flags, mac, channel, _ = _BRIDGE_CONFIG.unpack(body)
        return dict(type='bridge_config', flags=flags, drone_mac=mac, channel=channel)
    if ftype == PKT_BRIDGE_INFO:
        _, _, _, flags, sta, drone, channel, _, tx_ok, tx_fail, host_to, rx = _BRIDGE_INFO.unpack(body)
        return dict(type='bridge_info', flags=flags, sta_mac=sta, drone_mac=drone, channel=channel,
                    tx_ok=tx_ok, tx_fail=tx_fail, host_timeouts=host_to, rx_from_drone=rx)
    return None


class FrameParser:
    """Splits a byte stream into frames; resynchronises on noise (for example the boot log)."""

    def __init__(self):
        self._buf = bytearray()
        self.dropped = 0

    def feed(self, data: bytes):
        self._buf += data
        out = []
        while self._buf:
            if self._buf[0] != MAGIC:
                del self._buf[0]
                self.dropped += 1
                continue
            if len(self._buf) < 3:
                break
            length = FRAME_LENGTH.get(self._buf[2])
            if length is None:
                del self._buf[0]
                self.dropped += 1
                continue
            if len(self._buf) < length:
                break
            frame = decode(bytes(self._buf[:length]))
            if frame is None:
                del self._buf[0]
                self.dropped += 1
                continue
            out.append(frame)
            del self._buf[:length]
        return out


class SerialLink:
    """The bridge on a serial port. Keeps the latest status, telemetry and bridge info."""

    def __init__(self, port, baudrate=BRIDGE_BAUDRATE):
        import serial   # pyserial, only needed for real hardware
        # dsrdtr/rtscts off and DTR/RTS low: do not reset the ESP32 through its auto-reset circuit
        self._ser = serial.Serial()
        self._ser.port = port
        self._ser.baudrate = baudrate
        self._ser.timeout = 0.02
        self._ser.dtr = False
        self._ser.rts = False
        self._ser.open()
        self._parser = FrameParser()
        self._lock = threading.Lock()
        self._seq = 0
        self.status = None
        self.telemetry = None
        self.bridge_info = None
        self.last_rx = {}
        self.counts = {}
        self._running = True
        self._thread = threading.Thread(target=self._reader, daemon=True)
        self._thread.start()

    def _reader(self):
        while self._running:
            try:
                data = self._ser.read(256)
            except Exception:
                break
            for frame in self._parser.feed(data):
                now = time.monotonic()
                with self._lock:
                    kind = frame['type']
                    self.last_rx[kind] = now
                    self.counts[kind] = self.counts.get(kind, 0) + 1
                    if kind == 'status':
                        self.status = frame
                    elif kind == 'telemetry':
                        self.telemetry = frame
                    elif kind == 'bridge_info':
                        self.bridge_info = frame

    def age(self, kind):
        """Seconds since the last frame of this kind, None if never."""
        with self._lock:
            t = self.last_rx.get(kind)
        return None if t is None else time.monotonic() - t

    def send_control(self, roll=0, pitch=0, yaw=0, thrust=0, arm=False, kill=False):
        self._ser.write(control_frame(self._seq, roll, pitch, yaw, thrust, arm, kill))
        self._seq = (self._seq + 1) & 0xFFFF

    def configure(self, drone_mac: bytes, channel: int, save=False):
        self._ser.write(bridge_config_frame(drone_mac, channel, save))

    @property
    def dropped_bytes(self):
        return self._parser.dropped

    def close(self):
        self._running = False
        self._thread.join(timeout=1)
        self._ser.close()
