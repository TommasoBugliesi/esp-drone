#!/usr/bin/env python3
"""Placement study of the FlyingFree V2 on a 36 x 36 mm board, top and bottom view.

Each block is a part with its courtyard (package plus the passives around it), from typical package sizes.
It answers three questions before any CAD work: does everything fit, can the IMU sit near the centre, and
can the magnetometer stay 15 mm from every inductor (SEN-04)?

Usage: python3 gen_floorplan.py   -> floorplan.svg, prints the distance checks
"""

import html
import math
import os

HERE = os.path.dirname(os.path.abspath(__file__))
B = 36.0                      # board edge, mm
HOLE = (B - 30.5) / 2         # hole centre offset from the edge
KEEPOUT_R = 3.25              # grommet keep-out radius (MEC-02)

# (ref, label, x, y, w, h, kind) in mm, origin bottom-left, seen from the TOP for both sides
TOP = [
    ("U5", "ESP32-S3-MINI-1U", 10.3, 20.6, 15.4, 15.4, "mcu"),
    ("U8", "IMU", 16.0, 15.0, 4.0, 4.0, "sensor"),
    ("U9", "MAG", 30.5, 23.0, 4.5, 4.5, "sensor"),
    ("U10", "BARO", 1.5, 22.5, 4.0, 4.0, "sensor"),
    ("L1", "L1", 6.3, 0.2, 5.5, 5.5, "inductor"),
    ("U1", "5V bb", 12.0, 0.3, 5.0, 5.0, "power"),
    ("U4", "9V bb", 19.0, 0.3, 5.0, 5.0, "power"),
    ("L3", "L3", 24.2, 0.2, 5.5, 5.5, "inductor"),
    ("L2", "L2", 1.5, 6.5, 2.5, 2.5, "inductor"),
    ("U2", "3V3", 4.5, 6.5, 3.0, 3.0, "power"),
    ("U3", "LDO", 21.0, 15.0, 3.0, 3.0, "power"),
    ("J2", "USB-C", 0.0, 11.0, 7.5, 9.0, "conn"),
    ("D1", "TVS", 13.0, 6.5, 5.5, 4.0, "power"),
    ("U6", "ESD", 8.0, 16.0, 3.0, 3.0, "misc"),
    ("U12", "OSD+XTAL+U13", 29.3, 7.5, 6.5, 11.5, "misc"),
]
BOTTOM = [
    ("J3", "microSD", 11.0, 16.0, 14.0, 15.0, "conn"),
    ("J4", "ESC 8p", 12.0, 0.0, 12.0, 4.5, "conn"),
    ("J8", "DJI 6p", 26.0, 13.0, 4.5, 10.0, "conn"),
    ("U11", "EEPROM", 27.0, 5.0, 3.5, 6.5, "misc"),
    ("SW1", "BOOT", 31.5, 22.0, 4.0, 3.0, "misc"),
    ("PADS", "UART/GPS pads", 6.5, 31.5, 23.0, 4.0, "pad"),
    ("PADS2", "VTX/CAM pads", 31.5, 8.0, 4.0, 12.0, "pad"),
    ("PADS3", "BZ/LED/M5/M6", 0.0, 22.0, 4.0, 8.0, "pad"),
    ("PADS4", "prog pads", 2.0, 8.0, 6.0, 8.0, "pad"),
]
COLORS = {"mcu": "#cfe0f5", "sensor": "#f7d9a8", "power": "#f2b8b8", "inductor": "#e58a8a",
          "conn": "#cde8c5", "misc": "#e4e4e4", "pad": "#f3f0c8"}

SCALE = 14  # px per mm


def centre(b):
    return b[2] + b[4] / 2, b[3] + b[5] / 2


def gap(a, b):
    """Edge-to-edge distance of two rectangles."""
    dx = max(b[2] - (a[2] + a[4]), a[2] - (b[2] + b[4]), 0)
    dy = max(b[3] - (a[3] + a[5]), a[3] - (b[3] + b[5]), 0)
    return math.hypot(dx, dy)


def overlaps(blocks):
    out = []
    for i, a in enumerate(blocks):
        for b in blocks[i + 1:]:
            if gap(a, b) == 0 and not (a[2] + a[4] <= b[2] or b[2] + b[4] <= a[2] or
                                       a[3] + a[5] <= b[3] or b[3] + b[5] <= a[3]):
                out.append(f"{a[0]} overlaps {b[0]}")
    return out


def keepout_hits(blocks):
    out = []
    holes = [(HOLE, HOLE), (B - HOLE, HOLE), (HOLE, B - HOLE), (B - HOLE, B - HOLE)]
    for b in blocks:
        for hx, hy in holes:
            nx = min(max(hx, b[2]), b[2] + b[4])
            ny = min(max(hy, b[3]), b[3] + b[5])
            if math.hypot(nx - hx, ny - hy) < KEEPOUT_R:
                out.append(f"{b[0]} enters the grommet keep-out at ({hx:.2f}, {hy:.2f})")
        if b[2] < 0 or b[3] < 0 or b[2] + b[4] > B or b[3] + b[5] > B:
            out.append(f"{b[0]} leaves the board outline")
    return out


def checks():
    by = {b[0]: b for b in TOP}
    lines = []
    cx, cy = centre(by["U8"])
    lines.append(f"IMU centre offset from the board centre: {math.hypot(cx - B / 2, cy - B / 2):.1f} mm (MEC-06: <= 5)")
    for ind in ("L1", "L2", "L3"):
        lines.append(f"MAG to {ind}: {gap(by['U9'], by[ind]):.1f} mm (SEN-04: >= 15)")
        lines.append(f"IMU to {ind}: {gap(by['U8'], by[ind]):.1f} mm (EMC-02: >= 10)")
    lines.append(f"BARO to L1: {gap(by['U10'], by['L1']):.1f} mm (EMC-02: >= 10)")
    lines.append(f"OSD clock to IMU: {gap(by['U12'], by['U8']):.1f} mm")
    for side, blocks in (("top", TOP), ("bottom", BOTTOM)):
        for p in overlaps(blocks) + keepout_hits(blocks):
            lines.append(f"{side}: {p}")
        area = sum(b[4] * b[5] for b in blocks)
        lines.append(f"{side}: {area:.0f} mm2 of {B * B - 4 * math.pi * KEEPOUT_R ** 2:.0f} mm2 usable")
    return lines


def view(blocks, ox, oy, title, mirror):
    s = SCALE
    out = [f"<text x='{ox}' y='{oy - 14}' font-size='16' font-weight='bold' fill='#1a1a1a'>{title}</text>",
           f"<rect x='{ox}' y='{oy}' width='{B * s}' height='{B * s}' rx='{2 * s}' fill='#1d5c3a' "
           f"stroke='#0d2e1c' stroke-width='2'/>"]
    for hx, hy in [(HOLE, HOLE), (B - HOLE, HOLE), (HOLE, B - HOLE), (B - HOLE, B - HOLE)]:
        px, py = ox + hx * s, oy + (B - hy) * s
        out.append(f"<circle cx='{px}' cy='{py}' r='{KEEPOUT_R * s}' fill='none' stroke='#ffd166' "
                   f"stroke-dasharray='4 3'/>")
        out.append(f"<circle cx='{px}' cy='{py}' r='{2.0 * s}' fill='#ffffff' stroke='#0d2e1c'/>")
    cx, cy = ox + B / 2 * s, oy + B / 2 * s
    out.append(f"<circle cx='{cx}' cy='{cy}' r='{5 * s}' fill='none' stroke='#ffffff' stroke-dasharray='2 3' "
               f"opacity='0.7'/>")
    for ref, lab, x, y, w, h, kind in blocks:
        xx = B - x - w if mirror else x
        px, py = ox + xx * s, oy + (B - y - h) * s
        out.append(f"<rect x='{px:.1f}' y='{py:.1f}' width='{w * s:.1f}' height='{h * s:.1f}' rx='3' "
                   f"fill='{COLORS[kind]}' stroke='#333' stroke-width='1'/>")
        fs = 11 if min(w, h) > 4.5 else 9
        out.append(f"<text x='{px + w * s / 2:.1f}' y='{py + h * s / 2 - 2:.1f}' text-anchor='middle' "
                   f"font-size='{fs}' font-weight='bold' fill='#1a1a1a'>{ref}</text>")
        if min(w, h) > 4.5:
            out.append(f"<text x='{px + w * s / 2:.1f}' y='{py + h * s / 2 + 11:.1f}' text-anchor='middle' "
                       f"font-size='9' fill='#333'>{lab}</text>")
    return out


def main():
    lines = checks()
    w, h = int(2 * B * SCALE + 3 * 60 + 500), int(B * SCALE + 200)
    out = [f"<svg xmlns='http://www.w3.org/2000/svg' width='{w}' height='{h}' viewBox='0 0 {w} {h}' "
           f"font-family='DejaVu Sans, Arial, sans-serif'>",
           f"<rect width='{w}' height='{h}' fill='#ffffff'/>"]
    out += view(TOP, 60, 60, "Top (all bottom-terminated parts, one hot-plate pass)", False)
    out += view(BOTTOM, 60 * 2 + B * SCALE, 60, "Bottom, seen from below (iron-soldered parts)", True)
    lx = 60 * 3 + 2 * B * SCALE
    out.append(f"<text x='{lx}' y='60' font-size='14' font-weight='bold' fill='#1a1a1a'>Checks</text>")
    for i, ln in enumerate(lines):
        out.append(f"<text x='{lx}' y='{84 + i * 17}' font-size='11' fill='#1a1a1a'>{html.escape(ln)}</text>")
    ly = 84 + len(lines) * 17 + 20
    for i, (k, c) in enumerate(COLORS.items()):
        out.append(f"<rect x='{lx}' y='{ly + i * 18}' width='14' height='12' fill='{c}' stroke='#333'/>")
        out.append(f"<text x='{lx + 22}' y='{ly + i * 18 + 11}' font-size='11'>{k}</text>")
    out.append(f"<text x='60' y='{h - 50}' font-size='11' fill='#555'>36 x 36 mm, 30.5 mm M3 holes. Yellow: "
               f"grommet keep-out. White dashed circle: IMU zone, 5 mm around the centre (MEC-06).</text>")
    out.append(f"<text x='60' y='{h - 32}' font-size='11' fill='#555'>Courtyards from typical package sizes, "
               f"to be replaced by the datasheet footprints. Not a layout: a feasibility check.</text>")
    out.append("</svg>")
    with open(os.path.join(HERE, "floorplan.svg"), "w") as f:
        f.write("\n".join(out))
    print("\n".join(lines))
    bad = [ln for ln in lines if "overlaps" in ln or "keep-out" in ln or "outline" in ln]
    return 1 if bad else 0


if __name__ == "__main__":
    raise SystemExit(main())
