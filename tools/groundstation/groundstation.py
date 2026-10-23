#!/usr/bin/env python3
"""ESPDrone ground station: talk to the drone through the ESP-NOW USB bridge.

    groundstation.py info    --port /dev/ttyUSB0          bridge MAC, channel, counters
    groundstation.py config  --port /dev/ttyUSB0 --drone-mac 24:6f:28:aa:bb:cc --channel 6 --save
    groundstation.py monitor --port /dev/ttyUSB0          print status and telemetry, sends nothing
    groundstation.py fly     --port /dev/ttyUSB0          keyboard control in a window (pygame)

Keys in 'fly' (hold them; they act like a stick at --stick percent, Shift for full):
    W / S       pitch forward / back          A / D     roll left / right
    Q / E       yaw left / right              Up / Down thrust up / down (stays where you leave it)
    0 or X      thrust to zero                Space     arm / disarm (arming needs thrust 0)
    K or Esc    KILL (latches)                R         release the kill (sends ARM and KILL clear)
    closing the window or losing focus: thrust 0 and disarm, then KILL for 0.3 s on exit

Props off for every first test. The drone limits full thrust to ESPNOW_CTRL_THRUST_MAX (300 by default).
"""
import argparse
import sys
import time

import espdrone_link as L

SEND_HZ = 50
THRUST_RATE = 400          # thrust units per second while Up / Down is held


def open_link(args):
    try:
        return L.SerialLink(args.port, args.baud)
    except Exception as e:  # serial.SerialException, ImportError
        sys.exit('cannot open %s: %s (pip install -r tools/groundstation/requirements.txt)' % (args.port, e))


def flags_text(flags):
    names = [(L.STATUS_ARMED, 'ARMED'), (L.STATUS_KILLED, 'KILLED'), (L.STATUS_BATTERY_LOW, 'BATTERY-LOW'),
             (L.STATUS_FAILSAFE, 'FAILSAFE')]
    text = ' '.join(n for bit, n in names if flags & bit)
    return text or 'disarmed'


def print_info(info):
    print('bridge STA MAC %s  (put it in the drone: ESPNOW_CTRL_PEER_MAC)' % L.format_mac(info['sta_mac']))
    target = L.format_mac(info['drone_mac']) if info['flags'] & L.BRIDGE_INFO_UNICAST else 'broadcast'
    print('drone %s on channel %d' % (target, info['channel']))
    print('host active %s, drone answering %s' % (bool(info['flags'] & L.BRIDGE_INFO_HOST_ACTIVE),
                                                  bool(info['flags'] & L.BRIDGE_INFO_DRONE_ACTIVE)))
    print('sent %d, send errors %d, host gaps %d, frames from drone %d' %
          (info['tx_ok'], info['tx_fail'], info['host_timeouts'], info['rx_from_drone']))


def wait_info(link, timeout=3.0):
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        if link.bridge_info:
            return link.bridge_info
        time.sleep(0.05)
    sys.exit('no answer from the bridge (wrong port, or the bridge firmware is not flashed?)')


def cmd_info(args):
    link = open_link(args)
    print_info(wait_info(link))
    link.close()


def cmd_config(args):
    link = open_link(args)
    mac = bytes(6) if args.drone_mac == 'broadcast' else L.parse_mac(args.drone_mac)
    link.configure(mac, args.channel, save=args.save)
    time.sleep(0.7)
    link.bridge_info = None
    print_info(wait_info(link))
    link.close()


def cmd_monitor(args):
    link = open_link(args)
    print('monitoring, Ctrl+C to stop (nothing is sent to the drone, so it only answers while another '
          'transmitter keeps the link up)')
    try:
        while True:
            t, s = link.telemetry, link.status
            line = []
            if s:
                line.append('%-10s %.2f V ack %5d' % (flags_text(s['flags']), s['battery_v'], s['seq_ack']))
            if t:
                line.append('att %7.2f %7.2f %7.2f  gyro %6.1f %6.1f %6.1f  thr %5d  mot %s' % (
                    t['roll'], t['pitch'], t['yaw'], *t['gyro'], t['thrust'], t['motors']))
            print(' | '.join(line) if line else 'no frames from the drone', end='\r', flush=True)
            time.sleep(0.1)
    except KeyboardInterrupt:
        print()
    link.close()


def cmd_fly(args):
    try:
        import pygame
    except ImportError:
        sys.exit('fly needs pygame: pip install -r tools/groundstation/requirements.txt')

    link = open_link(args)
    pygame.init()
    screen = pygame.display.set_mode((760, 430))
    pygame.display.set_caption('ESPDrone ground station')
    font = pygame.font.SysFont('monospace', 18)
    big = pygame.font.SysFont('monospace', 26, bold=True)
    clock = pygame.time.Clock()

    thrust = 0.0
    arm = False
    kill = False
    stick = args.stick / 100.0
    message = 'Space to arm (thrust 0), Up/Down for thrust. Props off for the first test!'
    running = True
    last = time.monotonic()
    focus_lost = getattr(pygame, 'WINDOWFOCUSLOST', None)   # pygame 2
    max_frames = args.frames   # for automated tests: stop after this many frames (0 = never)

    while running:
        now = time.monotonic()
        dt, last = now - last, now
        for event in pygame.event.get():
            if event.type == pygame.QUIT:
                running = False
            elif focus_lost is not None and event.type == focus_lost:
                thrust, arm = 0.0, False
                message = 'window lost focus: thrust 0, disarmed'
            elif event.type == pygame.KEYDOWN:
                if event.key in (pygame.K_k, pygame.K_ESCAPE):
                    kill, arm, thrust = True, False, 0.0
                    message = 'KILL sent (latched). R releases it.'
                elif event.key == pygame.K_r:
                    kill, arm = False, False
                    message = 'kill released, disarmed'
                elif event.key == pygame.K_SPACE:
                    if arm:
                        arm = False
                        message = 'disarmed'
                    elif kill:
                        message = 'release the kill first (R)'
                    elif thrust > 0:
                        message = 'arming refused: bring thrust to 0 first'
                    else:
                        arm = True
                        message = 'ARM sent'
                elif event.key in (pygame.K_0, pygame.K_x, pygame.K_KP0):
                    thrust = 0.0

        keys = pygame.key.get_pressed()
        gain = 1.0 if (keys[pygame.K_LSHIFT] or keys[pygame.K_RSHIFT]) else stick
        roll = (keys[pygame.K_d] - keys[pygame.K_a]) * gain * L.AXIS_MAX
        pitch = (keys[pygame.K_w] - keys[pygame.K_s]) * gain * L.AXIS_MAX
        yaw = (keys[pygame.K_e] - keys[pygame.K_q]) * gain * L.AXIS_MAX
        if not kill:
            thrust += (keys[pygame.K_UP] - keys[pygame.K_DOWN]) * THRUST_RATE * dt
        thrust = min(max(thrust, 0.0), float(L.THRUST_MAX))

        link.send_control(roll, pitch, yaw, thrust, arm=arm, kill=kill)

        # Screen
        screen.fill((18, 22, 28))
        y = 14

        def line(text, color=(220, 220, 220), f=font):
            nonlocal y
            screen.blit(f.render(text, True, color), (16, y))
            y += f.get_linesize() + 2

        s, t, info = link.status, link.telemetry, link.bridge_info
        status_age = link.age('status')
        link_ok = status_age is not None and status_age < 0.5
        if not link_ok:
            line('NO LINK TO THE DRONE', (255, 90, 90), big)
        else:
            color = (255, 90, 90) if s['flags'] & (L.STATUS_KILLED | L.STATUS_FAILSAFE) else \
                (255, 200, 60) if s['flags'] & L.STATUS_ARMED else (120, 220, 120)
            line('%s   %.2f V' % (flags_text(s['flags']), s['battery_v']), color, big)
        line('')
        line('sent: thrust %4d   roll %5d   pitch %5d   yaw %5d   %s%s' %
             (thrust, roll, pitch, yaw, 'ARM ' if arm else '', 'KILL' if kill else ''))
        if t and link.age('telemetry') < 0.5:
            line('attitude  roll %7.2f   pitch %7.2f   yaw %7.2f deg' % (t['roll'], t['pitch'], t['yaw']))
            line('gyro      %7.1f   %7.1f   %7.1f deg/s' % t['gyro'])
            line('mixer     thrust %5d   motors %s' % (t['thrust'], ' '.join('%4d' % m for m in t['motors'])))
        else:
            line('no telemetry (drone firmware without it, or ESPNOW_CTRL_TELEMETRY_PERIOD_MS = 0)',
                 (150, 150, 150))
        line('')
        if info:
            line('bridge %s -> %s ch %d   sent %d  err %d  from drone %d' % (
                L.format_mac(info['sta_mac']),
                L.format_mac(info['drone_mac']) if info['flags'] & L.BRIDGE_INFO_UNICAST else 'broadcast',
                info['channel'], info['tx_ok'], info['tx_fail'], info['rx_from_drone']), (150, 150, 150))
        else:
            line('no answer from the bridge', (255, 90, 90))
        line('')
        line(message, (180, 200, 255))
        line('W/S pitch  A/D roll  Q/E yaw  Up/Down thrust  0 zero  Space arm  K/Esc kill  R release',
             (130, 130, 130))
        pygame.display.flip()
        clock.tick(SEND_HZ)
        if max_frames:
            max_frames -= 1
            running = max_frames > 0

    # Leave the drone safe: disarmed at zero thrust, then a kill burst
    for _ in range(5):
        link.send_control(0, 0, 0, 0, arm=False, kill=False)
        time.sleep(1.0 / SEND_HZ)
    end = time.monotonic() + 0.3
    while time.monotonic() < end:
        link.send_control(0, 0, 0, 0, kill=True)
        time.sleep(1.0 / SEND_HZ)
    link.close()
    pygame.quit()


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest='cmd', required=True)
    for name, func in (('info', cmd_info), ('config', cmd_config), ('monitor', cmd_monitor), ('fly', cmd_fly)):
        p = sub.add_parser(name)
        p.add_argument('--port', default='/dev/ttyUSB0')
        p.add_argument('--baud', type=int, default=L.BRIDGE_BAUDRATE)
        p.set_defaults(func=func)
        if name == 'config':
            p.add_argument('--drone-mac', required=True, help="the drone's AP MAC, or 'broadcast'")
            p.add_argument('--channel', type=int, default=6, help="the drone's WIFI_CHANNEL")
            p.add_argument('--save', action='store_true', help='keep it in the bridge across reboots')
        if name == 'fly':
            p.add_argument('--stick', type=int, default=50, help='stick deflection of a key, percent')
            p.add_argument('--frames', type=int, default=0, help=argparse.SUPPRESS)
    args = parser.parse_args()
    args.func(args)


if __name__ == '__main__':
    main()
