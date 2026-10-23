#!/usr/bin/env python3
"""Bridge and drone simulator for testing the ground station (or a phone app) without hardware.

    python3 tools/groundstation/sim_bridge.py      prints a port name, e.g. /dev/pts/5
    python3 tools/groundstation/groundstation.py fly --port /dev/pts/5

It speaks the bridge protocol on a pseudo terminal and applies the drone's link rules: arming on a
rising ARM edge with thrust 0, kill latch released by ARM and KILL both clear, failsafe after 250 ms
without control. Attitude follows the sticks, battery drains slowly. Linux and macOS only (pty).
"""
import os
import struct
import sys
import time
import tty

import espdrone_link as L

LINK_TIMEOUT = 0.25


def main():
    master, slave = os.openpty()
    tty.setraw(slave)
    print('simulated bridge on %s (Ctrl+C to stop)' % os.ttyname(slave), flush=True)
    os.set_blocking(master, False)

    parser = L.FrameParser()
    sta = bytes([0x7c, 0xdf, 0xa1, 0x00, 0x00, 0x01])
    drone_mac, channel = bytes(6), 6
    armed = killed = failsafe = active = False
    prev_arm = False
    last_rx = 0.0
    sticks = dict(roll=0, pitch=0, yaw=0, thrust=0)
    seq_ack = tx_ok = rx = 0
    att = [0.0, 0.0, 0.0]
    battery = 11.6
    next_status = next_tlm = next_info = 0.0
    tlm_seq = 0

    def send(body):
        os.write(master, body + L.crc16(body).to_bytes(2, 'little'))

    while True:
        now = time.monotonic()
        try:
            data = os.read(master, 512)
        except BlockingIOError:
            data = b''
        for f in parser.feed(data):
            if f['type'] == 'bridge_config':
                drone_mac, channel = f['drone_mac'], f['channel']
            elif f['type'] == 'control':
                tx_ok += 1
                arm = bool(f['flags'] & L.FLAG_ARM)
                if not active:
                    active, failsafe, prev_arm = True, False, arm   # no inherited arm
                if f['flags'] & L.FLAG_KILL:
                    killed, armed = True, False
                elif killed and not arm:
                    killed = False
                elif not killed:
                    if arm and not prev_arm:
                        armed = f['thrust'] == 0
                    elif not arm:
                        armed = False
                prev_arm = arm
                seq_ack, last_rx = f['seq'], now
                sticks = {k: f[k] for k in sticks}
        if active and now - last_rx > LINK_TIMEOUT:
            active, failsafe, armed = False, True, False
            sticks = dict(roll=0, pitch=0, yaw=0, thrust=0)

        att[0] += (sticks['roll'] / 1000 * 15 - att[0]) * 0.05
        att[1] += (sticks['pitch'] / 1000 * 15 - att[1]) * 0.05
        att[2] = (att[2] + sticks['yaw'] / 1000 * 30 * 0.01 + 180) % 360 - 180
        thrust = sticks['thrust'] * 300 // 1000 if armed else 0
        motors = tuple((48 + thrust) if armed else 0 for _ in range(4))
        battery = max(9.0, battery - (0.00002 if armed else 0.000002))
        flags = (L.STATUS_ARMED if armed else 0) | (L.STATUS_KILLED if killed else 0) | \
                (L.STATUS_FAILSAFE if failsafe else 0)
        drone_up = active or failsafe

        if drone_up and now >= next_status:
            next_status = now + 0.1
            send(bytes([L.MAGIC, L.VERSION, L.PKT_STATUS, flags]) + seq_ack.to_bytes(2, 'little') +
                 int(battery * 1000).to_bytes(2, 'little'))
            rx += 1
        if active and now >= next_tlm:
            next_tlm = now + 0.05

            send(struct.pack('<BBBBHhhhhhhH4HH', L.MAGIC, L.VERSION, L.PKT_TELEMETRY, flags, tlm_seq & 0xFFFF,
                             int(att[0] * 100), int(att[1] * 100), int(att[2] * 100), 0, 0,
                             int(sticks['yaw'] / 1000 * 300), thrust, *motors, int(battery * 1000)))
            tlm_seq += 1
            rx += 1
        if now >= next_info:
            next_info = now + 0.5

            unicast = L.BRIDGE_INFO_UNICAST if any(drone_mac) else 0
            send(struct.pack('<BBBB6s6sBBHHHH', L.MAGIC, L.VERSION, L.PKT_BRIDGE_INFO,
                             unicast | (L.BRIDGE_INFO_HOST_ACTIVE if active else 0) |
                             (L.BRIDGE_INFO_DRONE_ACTIVE if drone_up else 0),
                             sta, drone_mac, channel, 0, tx_ok & 0xFFFF, 0, 0, rx & 0xFFFF))
        time.sleep(0.005)


if __name__ == '__main__':
    try:
        main()
    except KeyboardInterrupt:
        sys.exit(0)
