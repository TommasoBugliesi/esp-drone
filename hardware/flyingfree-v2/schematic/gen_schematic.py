#!/usr/bin/env python3
"""Functional schematic of the FlyingFree V2 (ESP32-S3) board.

One description of the board (parts, pins, nets) produces:

* sheet-NN-*.svg   one SVG per schematic sheet, nets joined by labels
* ../bom.csv       bill of materials
* ../netlist.csv   every net and the pins on it

Pins carry their function names (VIN, SCLK, INT1...) and, where checked in the datasheet, the package pin
number ("IN 1"). Names without a number still have to be checked when the design is entered into CAD.

Usage: python3 gen_schematic.py   (no dependencies outside the standard library)
"""

import csv
import html
import os
import sys
from collections import defaultdict

HERE = os.path.dirname(os.path.abspath(__file__))
BOARD = "FlyingFree V2 (FF02), ESP32-S3"
REV = "draft A"
DATE = "2026-10-23"

POWER_NETS = {"GND", "VBAT", "VBUS", "+5V", "+3V3", "+3V3_SENS", "+9V", "VTX_PWR", "CAM_5V", "VTX_A_PWR"}

# Nets allowed to have a single connection (test pads, deliberate no-connects).
SINGLE_OK = {"NC"}  # also MAG_INT etc. reach a test pad, so they have 2 connections


class Part:
    def __init__(self, ref, value, mpn, package, sheet, group, left=(), right=(), dnp=False, note="",
                 kind="ic"):
        self.ref, self.value, self.mpn, self.package = ref, value, mpn, package
        self.sheet, self.group, self.dnp, self.note, self.kind = sheet, group, dnp, note, kind
        self.left, self.right = list(left), list(right)

    def pins(self):
        return self.left + self.right


PARTS = []
GROUP_NOTES = {}
SHEETS = {
    1: "Power: input, 5 V, 3.3 V",
    2: "Power: 9 V video rail, sensing",
    3: "MCU, USB and indicators",
    4: "Sensors and storage",
    5: "Connectors and motor outputs",
    6: "Video, OSD and pads",
}


def ic(ref, value, mpn, package, sheet, group, left, right, **kw):
    PARTS.append(Part(ref, value, mpn, package, sheet, group, left, right, **kw))


def two(ref, value, mpn, package, sheet, group, a, b, **kw):
    """Two-terminal part (resistor, capacitor, diode, inductor, jumper half)."""
    PARTS.append(Part(ref, value, mpn, package, sheet, group, [("1", a)], [("2", b)], kind="two", **kw))


def note(sheet, group, *lines):
    GROUP_NOTES[(sheet, group)] = lines


# ---------------------------------------------------------------- sheet 1: power
# Pin names carry the package pin number ("IN 1") where it was checked in the datasheet listed in
# ../datasheets.md. Names without a number are still to be checked.
S = 1
g = "Battery input and protection"
ic("J1", "VBAT pads", "-", "2 x pad 3x2 mm", S, g, [], [("VBAT", "VBAT"), ("GND", "GND")], kind="conn")
two("D1", "SMBJ13A", "Littelfuse SMBJ13A", "SMB (DO-214AA)", S, g, "VBAT", "GND", note="TVS, cathode to VBAT")
two("C1", "22uF 35V", "-", "1210 X5R", S, g, "VBAT", "GND")
two("C2", "22uF 35V", "-", "1210 X5R", S, g, "VBAT", "GND")
note(S, g, "1S-3S LiPo/LiHV, 2.8-13.2 V (PWR-01). Big low-ESR cap on the ESC.",
     "SMBJ13A: VR 13 V, clamp ~21.5 V < MP28167-A 22 V operating, 26 V abs max.",
     "(SMBJ14A clamps at 23.2 V: above 22 V, rejected.)")

g = "5 V rail, buck-boost (PWR-04)"
ic("U1", "MP28167-A", "MPS MP28167GQ-A", "QFN-16 3x3", S, g,
   [("IN 1", "VBAT"), ("EN 3", "EN_5V"), ("SW1 15", "SW1_5V"), ("BST1 16", "BST1_5V"), ("VCC 9", "VCC_5V"),
    ("SCL 5", "VCC_5V"), ("SDA 6", "VCC_5V"), ("GND 2,11", "GND"), ("AGND 10", "GND")],
   [("OUT 12", "+5V"), ("SW2 14", "SW2_5V"), ("BST2 13", "BST2_5V"), ("FB 8", "FB_5V"), ("OC 7", "NC"),
    ("ALT 4", "NC")])
two("L1", "4.7uH Isat>=4A shielded", "5x5 mm class (Coilcraft XGL5030 / Wurth WE-XHMI)", "5x5 mm", S, g,
    "SW1_5V", "SW2_5V")
two("C3", "22uF 25V", "-", "1206", S, g, "VBAT", "GND", note="CIN, at pin IN")
two("C4", "5x 22uF 10V", "-", "0805", S, g, "+5V", "GND", note="COUT >= 5 x 22 uF (DS)")
two("C5", "100nF", "-", "0402", S, g, "BST1_5V", "SW1_5V")
two("C6", "100nF", "-", "0402", S, g, "BST2_5V", "SW2_5V")
two("C7", "1uF", "-", "0402", S, g, "VCC_5V", "GND")
two("R1", "430k 1%", "-", "0402", S, g, "+5V", "FB_5V", note="DS table 1")
two("R2", "107k 1%", "-", "0402", S, g, "FB_5V", "GND", note="1.0 V x (1 + 430/107) = 5.02 V")
two("R3", "100k", "-", "0402", S, g, "VBAT", "EN_5V", note="EN on; internal UVLO 2.65 V")
note(S, g, "VREF 1.000 V (I2C default, I2C unused: SCL/SDA to VCC per DS).",
     "DS table 1 also lists RT 806k for 5 V: place it as the DS figure shows.",
     "Default forced PWM, 500 kHz. OC and ALT open.")

g = "USB power path (PWR-09)"
two("D2", "Schottky 2A", "Nexperia PMEG3020EH / SS24", "SOD-123F", S, g, "VBUS", "+5V",
    note="anode VBUS, cathode +5V")
note(S, g, "USB only: U1 is under UVLO, its 60-100 R discharge runs 50 ms max.",
     "With a battery, +5V = 5.0 V > VBUS - Vf: no back-feed into the host.")

g = "3.3 V rail, buck (PWR-05)"
ic("U2", "TLV62569", "TI TLV62569DBVR", "SOT-23-5", S, g,
   [("VIN 4", "+5V"), ("EN 1", "+5V"), ("GND 2", "GND")], [("SW 3", "SW_3V3"), ("FB 5", "FB_3V3")])
two("L2", "2.2uH Isat>=3A shielded", "Coilcraft XAL4020-222ME (TI EVM) or smaller", "4x4 / 2.5x2 mm", S, g,
    "SW_3V3", "+3V3")
two("C8", "4.7uF", "-", "0603", S, g, "+5V", "GND", note="CIN")
two("C9", "22uF", "-", "0805", S, g, "+3V3", "GND", note="COUT 10-47 uF")
two("R7", "100k 1%", "-", "0402", S, g, "+3V3", "FB_3V3")
two("R8", "22.1k 1%", "-", "0402", S, g, "FB_3V3", "GND")
note(S, g, "VFB 0.6 V: 0.6 x (1 + 100/22.1) = 3.31 V. 2 A, 3 A current limit.")

g = "Sensor 3.3 V, low-noise LDO (PWR-06)"
ic("U3", "LP5907-3.3", "TI LP5907MFX-3.3/NOPB", "SOT-23-5", S, g,
   [("IN 1", "+5V"), ("EN 3", "+5V"), ("GND 2", "GND")], [("OUT 5", "+3V3_SENS"), ("N/C 4", "NC")])
two("C10", "1uF", "-", "0402", S, g, "+5V", "GND")
two("C11", "1uF", "-", "0402", S, g, "+3V3_SENS", "GND", note="low ESR")
note(S, g, "250 mA, PSRR 82 dB at 1 kHz, < 6.5 uVrms.")

S = 2
g = "9 V video rail, buck-boost (PWR-07)"
ic("U4", "MP28167-A", "MPS MP28167GQ-A", "QFN-16 3x3", S, g,
   [("IN 1", "VBAT"), ("EN 3", "EN_9V"), ("SW1 15", "SW1_9V"), ("BST1 16", "BST1_9V"), ("VCC 9", "VCC_9V"),
    ("SCL 5", "VCC_9V"), ("SDA 6", "VCC_9V"), ("GND 2,11", "GND"), ("AGND 10", "GND")],
   [("OUT 12", "+9V_RAW"), ("SW2 14", "SW2_9V"), ("BST2 13", "BST2_9V"), ("FB 8", "FB_9V"), ("OC 7", "NC"),
    ("ALT 4", "NC")])
two("L3", "4.7uH Isat>=4A shielded", "5x5 mm class (Coilcraft XGL5030 / Wurth WE-XHMI)", "5x5 mm", S, g,
    "SW1_9V", "SW2_9V")
two("C12", "22uF 25V", "-", "1206", S, g, "VBAT", "GND", note="CIN")
two("C13", "5x 22uF 16V", "-", "0805", S, g, "+9V_RAW", "GND", note="COUT")
two("C14", "100nF", "-", "0402", S, g, "BST1_9V", "SW1_9V")
two("C15", "100nF", "-", "0402", S, g, "BST2_9V", "SW2_9V")
two("C16", "1uF", "-", "0402", S, g, "VCC_9V", "GND")
two("R9", "866k 1%", "-", "0402", S, g, "+9V_RAW", "FB_9V")
two("R10", "107k 1%", "-", "0402", S, g, "FB_9V", "GND", note="1.0 V x (1 + 866/107) = 9.09 V")
two("R11", "430k 1%", "-", "0402", S, g, "VBAT", "EN_9V")
two("R12", "100k 1%", "-", "0402", S, g, "EN_9V", "GND", note="with the internal 2M: on above ~6.0 V")
ic("Q1", "N-MOSFET", "AOS AO3400A", "SOT-23", S, g, [("G 1", "VTX_EN_N")], [("D 3", "EN_9V"), ("S 2", "GND")])
two("R14", "100k", "-", "0402", S, g, "VTX_EN_N", "+3V3", note="Q1 on at reset: 9 V off")
two("FB1", "ferrite 600R@100MHz 3A", "-", "0805", S, g, "+9V_RAW", "+9V")
note(S, g, "EN 1.10 V rising: 1.10 x (1 + 430k / (100k || 2M)) = 6.07 V.",
     "GPIO21 (VTX_EN_N, no power-up glitch) low = 9 V on.",
     "DJI O3: 7.4-26.4 V, ~2 A at 9 V. DJI O4 (not Pro): 3.7-13.2 V, VBAT via JP1.")

g = "Battery voltage and current sense (PWR-10, PWR-11)"
two("R15", "56k 1%", "-", "0402", S, g, "VBAT", "VBAT_ADC")
two("R16", "10k 1%", "-", "0402", S, g, "VBAT_ADC", "GND")
two("C17", "47nF", "-", "0402", S, g, "VBAT_ADC", "GND")
two("R17", "1k", "-", "0402", S, g, "CURR", "CURR_ADC")
two("C18", "10nF", "-", "0402", S, g, "CURR_ADC", "GND")
ic("D3", "BAT54S", "Nexperia BAT54S", "SOT-23", S, g, [("A1", "GND")], [("K1A2", "CURR_ADC"), ("K2", "+3V3")])
note(S, g, "16 V -> 2.42 V at GPIO1. Thevenin 8.5k x 47 nF = 0.4 ms.",
     "CURR from the ESC is clamped to 0..3.3 V before GPIO2.")

g = "Rail test points (TST-01)"
for i, n in enumerate(["VBAT", "+9V", "+5V", "+3V3", "+3V3_SENS", "GND"]):
    ic(f"TP{i + 1}", "test point", "-", "pad 1 mm", S, g, [("TP", n)], [], kind="conn")

# ---------------------------------------------------------------- sheet 3: MCU
S = 3
# GPIO1-18 glitch low for ~60 us at power-up (chip datasheet table 2-2); 19/20 glitch too (USB).
# Outputs that must stay quiet at reset (motors, servos, 9 V enable) therefore sit on 21 and 33-38.
GPIO = {
    0: "BOOT", 1: "VBAT_ADC", 2: "CURR_ADC", 3: "BUZZER", 4: "SPI3_SCK", 5: "SPI3_MOSI", 6: "SPI3_MISO",
    7: "MAG_CS", 8: "UART2_RX", 9: "IMU_INT1", 10: "IMU_CS", 11: "IMU_MOSI", 12: "IMU_SCK", 13: "IMU_MISO",
    14: "IMU_INT2", 15: "PINIO2", 16: "LED_STRIP_IO", 17: "UART1_TX", 18: "UART1_RX", 19: "USB_DN",
    20: "USB_DP", 21: "VTX_EN_N", 26: "UART2_TX", 33: "M1_IO", 34: "M2_IO", 35: "M3_IO", 36: "M4_IO",
    37: "M5_IO", 38: "M6_IO", 39: "OSD_CS", 40: "SD_CLK", 41: "SD_CMD", 42: "SD_D0",
    43: "UART0_TX", 44: "UART0_RX", 45: "LED_R", 46: "LED_G", 47: "I2C0_SDA", 48: "I2C0_SCL",
}
g = "ESP32-S3-MINI-1U-N8"
gp = sorted(GPIO)
ic("U5", "ESP32-S3-MINI-1U-N8", "Espressif ESP32-S3-MINI-1U-N8", "LGA-65 15.4x15.4x2.4", S, g,
   [("3V3", "+3V3"), ("EN", "EN"), ("GND", "GND")] + [(f"IO{n}", GPIO[n]) for n in gp[:18]],
   [(f"IO{n}", GPIO[n]) for n in gp[18:]],
   note="U.FL antenna to an external 2.4 GHz antenna (MEC-07)")
two("C19", "22uF", "-", "0805", S, g, "+3V3", "GND", note=">= 10 uF at the power entrance")
two("C20", "100nF", "-", "0402", S, g, "+3V3", "GND")
note(S, g, "All 39 GPIO used. Straps: IO0 BOOT, IO3 buzzer (pull-down), IO45/46 LEDs.",
     "IO1-18 glitch low ~60 us at power-up: no motor, servo or enable there.",
     "Motors IO33-38, 9 V enable IO21. ADC1 only: VBAT IO1, CURR IO2.")

g = "Reset and boot (MCU-02, MCU-04)"
two("R18", "10k", "-", "0402", S, g, "EN", "+3V3")
two("C21", "1uF", "-", "0402", S, g, "EN", "GND")
ic("SW1", "BOOT", "tactile 3x4 mm SMD", "SMD", S, g, [("1", "BOOT")], [("2", "GND")])
two("R19", "10k", "-", "0402", S, g, "BOOT", "+3V3")
ic("TP7", "EN pad", "-", "pad 1 mm", S, g, [("TP", "EN")], [], kind="conn")
note(S, g, "EN RC 10k / 1 uF (module datasheet and design guidelines).")

g = "USB-C (MCU-03)"
ic("J2", "USB-C 16p", "GCT USB4105-GF-A", "USB-C, TH shell legs", S, g,
   [("VBUS", "VBUS"), ("D+", "USB_C_DP"), ("D-", "USB_C_DN"), ("CC1", "CC1"), ("CC2", "CC2")],
   [("GND", "GND"), ("SHIELD", "GND")], kind="conn")
two("R20", "5.1k", "-", "0402", S, g, "CC1", "GND")
two("R21", "5.1k", "-", "0402", S, g, "CC2", "GND")
two("R22", "0R (22R option)", "-", "0402", S, g, "USB_C_DP", "USB_DP")
two("R23", "0R (22R option)", "-", "0402", S, g, "USB_C_DN", "USB_DN")
ic("U6", "USBLC6-2SC6", "ST USBLC6-2SC6", "SOT-23-6", S, g,
   [("I/O1 1,6", "USB_C_DP"), ("I/O2 3,4", "USB_C_DN")], [("VBUS 5", "VBUS"), ("GND 2", "GND")])
two("C22", "10uF", "-", "0603", S, g, "VBUS", "GND")
two("C36", "100nF", "-", "0402", S, g, "VBUS", "GND", note="at U6 VBUS (DS)")
note(S, g, "D+/D- pass through U6 (1-6, 3-4). Short traces to U6 VBUS and GND.")

g = "Status LEDs (IND-01, IND-02)"
two("R24", "1k", "-", "0402", S, g, "+3V3", "LED_PWR_A")
two("D4", "LED blue (power)", "-", "0603", S, g, "LED_PWR_A", "GND")
two("R25", "1k", "-", "0402", S, g, "LED_R", "LED_R_A")
two("D5", "LED red", "-", "0603", S, g, "LED_R_A", "GND")
two("R26", "1k", "-", "0402", S, g, "LED_G", "LED_G_A")
two("D6", "LED green", "-", "0603", S, g, "LED_G_A", "GND")

g = "Buzzer driver (IND-03)"
ic("Q2", "N-MOSFET", "AOS AO3400A", "SOT-23", S, g, [("G 1", "BUZ_G")], [("D 3", "BUZ-"), ("S 2", "GND")])
two("R27", "100R", "-", "0402", S, g, "BUZZER", "BUZ_G")
two("R28", "100k", "-", "0402", S, g, "BUZ_G", "GND", note="also holds strap IO3 low")
two("D7", "1N4148W", "-", "SOD-323", S, g, "BUZ-", "+5V", note="flyback, cathode +5V")
note(S, g, "AO3400A: RDS(on) < 48 mOhm at 2.5 V gate, fine from 3.3 V.",
     "IO3 glitches at power-up: at most a 60 us click.")

g = "LED strip buffer (IND-04)"
ic("U7", "74AHCT1G125", "TI SN74AHCT1G125DBVR", "SOT-23-5", S, g,
   [("A 2", "LED_STRIP_IO"), ("OE# 1", "GND"), ("VCC 5", "+5V")], [("Y 4", "LED_STRIP_Y"), ("GND 3", "GND")])
two("R29", "330R", "-", "0402", S, g, "LED_STRIP_Y", "LED_DIN")
two("C23", "100nF", "-", "0402", S, g, "+5V", "GND")
note(S, g, "VIH 2.0 V at VCC 5 V: takes 3.3 V in. Driven by I2S (RMT TX all DShot).")

# ---------------------------------------------------------------- sheet 4: sensors and storage
S = 4
g = "IMU ICM-42688-P on SPI2 (SEN-01, SEN-02)"
ic("U8", "ICM-42688-P", "TDK ICM-42688-P", "LGA-14 2.5x3x0.91", S, g,
   [("VDD 8", "+3V3_SENS"), ("VDDIO 5", "+3V3_SENS"), ("GND 6", "GND"), ("AP_SCLK 13", "IMU_SCK"),
    ("AP_SDI 14", "IMU_MOSI"), ("AP_SDO 1", "IMU_MISO"), ("AP_CS 12", "IMU_CS")],
   [("INT1 4", "IMU_INT1"), ("INT2/FSYNC/CLKIN 9", "IMU_INT2"), ("RESV 7", "GND"), ("RESV 2,3,10,11", "GND")])
two("C24", "100nF X7R", "-", "0402", S, g, "+3V3_SENS", "GND", note="VDD")
two("C26", "2.2uF X7R", "-", "0402", S, g, "+3V3_SENS", "GND", note="VDD")
two("C25", "10nF X7R", "-", "0402", S, g, "+3V3_SENS", "GND", note="VDDIO")
note(S, g, "Board centre, axes parallel to the edges (MEC-06). SPI2 alone, 24 MHz max.",
     "RESV pin 7 must go to GND; 2, 3, 10, 11 NC or GND (GND here).",
     "Pin 9: INT2 or 32 kHz CLKIN (31-50 kHz) from LEDC; firmware sets it first.")

g = "Magnetometer MMC5983MA on SPI3 (SEN-03, SEN-04)"
ic("U9", "MMC5983MA", "MEMSIC MMC5983MA", "LGA-16 3x3x1", S, g,
   [("VDD 2", "+3V3_SENS"), ("VDDIO 13", "+3V3_SENS"), ("GND 9,11", "GND"), ("SCL/SPI_SCK 1", "SPI3_SCK"),
    ("SDA/SPI_SDI 16", "SPI3_MOSI"), ("SPI_SDO 5", "SPI3_MISO"), ("SPI_CS 4", "MAG_CS")],
   [("INT 15", "MAG_INT"), ("CAP 10", "MAG_CAP"), ("NC 3,6-8,12,14", "NC")])
two("C28", "1uF", "-", "0402", S, g, "+3V3_SENS", "GND", note="VDD")
two("C27", "100nF", "-", "0402", S, g, "+3V3_SENS", "GND", note="VDDIO")
two("C29", "10uF low ESR", "-", "0603 X5R", S, g, "MAG_CAP", "GND", note="SET/RESET, at the pin")
ic("TP8", "MAG INT pad", "-", "pad 1 mm", S, g, [("TP", "MAG_INT")], [], kind="conn")
two("R30", "10k", "-", "0402", S, g, "MAG_CS", "+3V3", note="CS idle high")
note(S, g, "SPI mode 3 (inferred from the timing), 10 MHz max. VDD/VDDIO 2.8-3.6 V.",
     "CS driven by the S3 = SPI; CS tied to VDDIO would select I2C.",
     "1 kHz: BW=11, 1.2 mG rms; 100 Hz: 0.4 mG. >= 15 mm from inductors.")

g = "Barometer DPS310 on I2C0 (SEN-06)"
ic("U10", "DPS310", "Infineon DPS310XTSA1", "VLGA-8 2.0x2.5, 0.65 pitch", S, g,
   [("VDD 8", "+3V3_SENS"), ("VDDIO 6", "+3V3_SENS"), ("GND 1,7", "GND")],
   [("SDI 3", "I2C0_SDA"), ("SCK 4", "I2C0_SCL"), ("SDO 5", "+3V3_SENS"), ("CSB 2", "+3V3_SENS")])
two("C30", "100nF", "-", "0402", S, g, "+3V3_SENS", "GND")
note(S, g, "I2C 0x77 (SDO high). CSB high selects I2C. Under foam (MEC-08).",
     "Alternative on the same idea: BMP390 (10-pin 2x2, best noise).")

g = "I2C0 pull-ups and EEPROM (COM-04, STO-03)"
two("R31", "2.2k", "-", "0402", S, g, "I2C0_SDA", "+3V3")
two("R32", "2.2k", "-", "0402", S, g, "I2C0_SCL", "+3V3")
ic("U11", "24LC256", "Microchip 24LC256T-I/ST", "TSSOP-8", S, g,
   [("VCC", "+3V3"), ("GND", "GND"), ("A0", "GND"), ("A1", "GND"), ("A2", "GND")],
   [("SDA", "I2C0_SDA"), ("SCL", "I2C0_SCL"), ("WP", "GND")], dnp=True)
two("C31", "100nF", "-", "0402", S, g, "+3V3", "GND", dnp=True)
note(S, g, "EEPROM optional (DNP), 0x50. 64-byte page, 5 ms write: only when disarmed.")

g = "microSD, SDMMC 1-bit (STO-01, STO-02)"
ic("J3", "microSD push-push", "Hirose DM3AT-SF-PEJM5", "SMD, 1.68 mm high", S, g,
   [("VDD", "+3V3"), ("VSS", "GND"), ("CLK", "SD_CLK"), ("CMD", "SD_CMD")],
   [("DAT0", "SD_D0"), ("DAT1", "SD_D1"), ("DAT2", "SD_D2"), ("CD/DAT3", "SD_D3"), ("SHIELD", "GND")],
   kind="conn")
two("R33", "10k", "-", "0402", S, g, "SD_CMD", "+3V3")
two("R34", "10k", "-", "0402", S, g, "SD_D0", "+3V3")
two("R35", "10k", "-", "0402", S, g, "SD_D1", "+3V3")
two("R36", "10k", "-", "0402", S, g, "SD_D2", "+3V3")
two("R37", "10k", "-", "0402", S, g, "SD_D3", "+3V3")
two("C32", "10uF", "-", "0603", S, g, "+3V3", "GND")
two("C33", "100nF", "-", "0402", S, g, "+3V3", "GND")
note(S, g, "D1-D3 pulled up only (1-bit bus). DAT3 high selects SD mode.",
     "Molex 104031 rejected: push-pull, not push-push.")

# ---------------------------------------------------------------- sheet 5: connectors
S = 5
g = "ESC connector, 8-pin JST-SH (MOT-01)"
ic("J4", "ESC 8p SH1.0", "JST SM08B-SRSS-TB", "SH 1.0 mm side entry", S, g, [],
   [("1", "VBAT"), ("2", "GND"), ("3", "CURR"), ("4", "ESC_TLM"), ("5", "M1"), ("6", "M2"), ("7", "M3"),
    ("8", "M4")], kind="conn")
ic("TP9", "ESC TLM pad", "-", "pad 1 mm", S, g, [("TP", "ESC_TLM")], [], kind="conn")
note(S, g, "Pin order varies between ESCs: confirm against the chosen 4-in-1.",
     "SH contacts 1 A each: VBAT here feeds only the FC, not the motors.",
     "ESC_TLM unused (no UART left): telemetry via bidirectional DShot.")

g = "Motor and servo outputs (MOT-02..05)"
for i in range(1, 7):
    two(f"R{37 + i}", "100R", "-", "0402", S, g, f"M{i}_IO", f"M{i}")
ic("J5", "M5/M6 pads", "-", "pads", S, g, [], [("M5", "M5"), ("M6", "M6"), ("5V", "+5V"), ("GND", "GND")],
   kind="conn")
note(S, g, "IO33-38: no power-up glitch. No pull resistors (bidirectional DShot",
     "inverts the idle level). M5/M6 on MCPWM/LEDC: PWM or servo only.")

g = "Receiver UART1 (COM-01, COM-02)"
ic("J6", "RX pads", "-", "pads", S, g, [], [("5V", "+5V"), ("GND", "GND"), ("TX1", "UART1_TX"),
                                           ("RX1", "RX1_PAD")], kind="conn")
two("R44", "220R", "-", "0402", S, g, "RX1_PAD", "UART1_RX")
two("JP2", "solder jumper", "-", "SJ 2-pad", S, g, "DJI_SBUS", "RX1_PAD", note="DJI SBUS to UART1 RX")
note(S, g, "CRSF/ELRS or SBUS; SBUS inverted in the S3 UART, no inverter.")

g = "GPS UART0 and compass I2C (COM-01, COM-04, SEN-05)"
ic("J7", "GPS pads", "-", "pads", S, g, [], [("5V", "+5V"), ("GND", "GND"), ("TX0", "UART0_TX"),
                                            ("RX0", "RX0_PAD"), ("SDA", "I2C0_SDA"), ("SCL", "I2C0_SCL")],
   kind="conn")
two("R45", "220R", "-", "0402", S, g, "RX0_PAD", "UART0_RX")
note(S, g, "UART0 also carries the ROM boot log (MCU-07); a GPS ignores it.")

# ---------------------------------------------------------------- sheet 6: video, OSD, pads
S = 6
g = "HD VTX, DJI 6-pin JST-SH (VID-01, PWR-08)"
ic("J8", "DJI 6p SH1.0", "JST SM06B-SRSS-TB", "SH 1.0 mm", S, g, [],
   [("1 PWR", "VTX_PWR"), ("2 GND", "GND"), ("3 TX", "UART2_TX"), ("4 RX", "RX2_PAD"), ("5 GND", "GND"),
    ("6 SBUS", "DJI_SBUS")], kind="conn")
two("R46", "220R", "-", "0402", S, g, "RX2_PAD", "UART2_RX")
two("JP1A", "solder jumper", "-", "SJ 3-pad", S, g, "+9V", "VTX_PWR", note="default: 9 V (O3, O4 Pro)")
two("JP1B", "solder jumper", "-", "SJ 3-pad", S, g, "VBAT", "VTX_PWR", note="VBAT: O4 on 1S-3S")
note(S, g, "Pin 3 is the FC TX (to the air unit RX). Check the order on the unit.",
     "JP1 is one 3-pad jumper, drawn as two halves: close only one.",
     "SH contact 1 A: an O3 at 2 A needs the power on two pins or a pad.")

g = "Analog OSD AT7456E on SPI3 (VID-02, VID-03)"
ic("U12", "AT7456E", "Zhongke AT7456E", "HTSSOP-28 + EP", S, g,
   [("DVDD 3", "OSD_VDD"), ("AVDD 21", "OSD_VDD"), ("PVDD 24", "OSD_VDD"), ("DGND 4", "GND"),
    ("AGND 20, EP", "GND"), ("PGND 23", "GND"), ("SCLK 10", "SPI3_SCK"), ("SDIN 9", "SPI3_MOSI"),
    ("SDOUT 11", "OSD_SDO"), ("/CS 8", "OSD_CS"), ("/RESET 19", "OSD_RST")],
   [("VIN 22", "VIDEO_IN_C"), ("VOUT 26", "VIDEO_OUT"), ("SAG 25", "VIDEO_OUT"), ("CLKIN 5", "OSD_XIN"),
    ("XFB 6", "OSD_XFB"), ("CLKOUT 7", "NC"), ("LOS 12", "NC"), ("/VSYNC 17", "NC"), ("/HSYNC 18", "NC")],
   dnp=True)
two("Y1", "27MHz parallel", "-", "3225", S, g, "OSD_XIN", "OSD_XFB", dnp=True, note="load caps per DS figure")
two("FB2", "ferrite", "-", "0603", S, g, "+5V", "OSD_VDD", dnp=True, note="5 V: video levels")
two("C34", "3x 100nF", "-", "0402", S, g, "OSD_VDD", "GND", dnp=True, note="one per DVDD, AVDD, PVDD")
two("C37", "10uF", "-", "0603", S, g, "OSD_VDD", "GND", dnp=True)
two("C35", "1uF", "-", "0603", S, g, "VIDEO_IN", "VIDEO_IN_C", dnp=True, note="video coupling")
two("R47", "75R", "-", "0402", S, g, "VIDEO_IN", "GND", dnp=True)
ic("U13", "74LVC1G125", "TI SN74LVC1G125DBVR", "SOT-23-5", S, g,
   [("A 2", "OSD_SDO"), ("OE# 1", "OSD_CS"), ("VCC 5", "+3V3")], [("Y 4", "SPI3_MISO"), ("GND 3", "GND")],
   dnp=True)
two("C39", "100nF", "-", "0402", S, g, "+3V3", "GND", dnp=True)
two("R51", "10k", "-", "0402", S, g, "OSD_RST", "+3V3", dnp=True, note="TTL input, VIH 2.0 V")
two("C38", "100nF", "-", "0402", S, g, "OSD_RST", "GND", dnp=True, note="power-on reset")
two("R49", "10k", "-", "0402", S, g, "OSD_CS", "+3V3", note="CS idle high, fitted on all builds")
note(S, g, "AT7456E 3.15-5.25 V, run at 5 V (characterised there). TTL inputs take",
     "3.3 V; SDOUT swings to 5 V: U13 (5 V tolerant input, 3.3 V supply) buffers it",
     "and releases MISO when OSD_CS is high, so the magnetometer keeps the bus.",
     "HTSSOP with exposed pad: top side, hot plate (ASM-02). DS lists pin 13 twice.")

g = "Analog camera and VTX pads (VID-03, COM-03)"
ic("J9", "CAM pads", "-", "pads", S, g, [], [("5V", "CAM_5V"), ("GND", "GND"), ("VIN", "VIDEO_IN")],
   kind="conn")
ic("J10", "VTX pads", "-", "pads", S, g, [], [("PWR", "VTX_A_PWR"), ("GND", "GND"), ("VOUT", "VIDEO_OUT"),
                                             ("SA", "UART2_TX")], kind="conn")
two("FB3", "ferrite 1A", "-", "0603", S, g, "+5V", "CAM_5V")
two("JP3A", "solder jumper", "-", "SJ 3-pad", S, g, "+5V", "VTX_A_PWR", note="analog VTX on 5 V")
two("JP3B", "solder jumper", "-", "SJ 3-pad", S, g, "+9V", "VTX_A_PWR", note="analog VTX on 9 V")
note(S, g, "SmartAudio/Tramp on UART2 TX, single-wire half-duplex (COM-03).")

g = "Peripheral pads (IND-03, IND-04)"
ic("J11", "BUZ pads", "-", "pads", S, g, [], [("BZ+", "+5V"), ("BZ-", "BUZ-")], kind="conn")
ic("J12", "LED strip pads", "-", "pads", S, g, [], [("5V", "+5V"), ("GND", "GND"), ("DIN", "LED_DIN")],
   kind="conn")
ic("J13", "PINIO2 pad", "-", "pad", S, g, [], [("PINIO2", "PINIO2")], kind="conn")
ic("J14", "CURR pad", "-", "pad", S, g, [], [("CURR", "CURR")], kind="conn")
note(S, g, "PINIO2 on IO15 glitches at power-up: not for anything that must stay off.")

g = "Production pads (TST-03)"
ic("J15", "prog pads", "-", "pads", S, g, [], [("3V3", "+3V3"), ("GND", "GND"), ("EN", "EN"), ("IO0", "BOOT"),
                                              ("TX0", "UART0_TX"), ("RX0", "UART0_RX")], kind="conn")


# ---------------------------------------------------------------- checks
def build_nets():
    nets = defaultdict(list)
    for p in PARTS:
        for name, net in p.pins():
            nets[net].append((p.ref, name))
    return nets


def check(nets):
    problems = []
    for net, conns in sorted(nets.items()):
        if net in SINGLE_OK or net == "per datasheet":
            continue
        if len(conns) < 2:
            problems.append(f"net {net} has one connection: {conns}")
    refs = [p.ref for p in PARTS]
    for r in set(refs):
        if refs.count(r) > 1:
            problems.append(f"duplicate reference {r}")
    used = [n for n in GPIO.values()]
    if len(used) != len(set(used)):
        problems.append("a net is on two GPIO")
    if len(GPIO) != 39:
        problems.append(f"{len(GPIO)} GPIO assigned, expected 39")
    return problems


# ---------------------------------------------------------------- SVG rendering
W, H = 1760, 1244
MARGIN = 30
COLW = 540
FONT = "font-family='DejaVu Sans Mono, Menlo, Consolas, monospace'"
INK, NET, PWR, REF, DNP, FRAME = "#1a1a1a", "#1f5fa8", "#b03a2e", "#6b4a00", "#8a8a8a", "#c8c8c8"


def esc(s):
    return html.escape(str(s), quote=True)


def label(x, y, net, anchor):
    color = PWR if net in POWER_NETS else NET
    w = 7.2 * len(net) + 10
    bx = x if anchor == "start" else x - w
    return (f"<rect x='{bx:.1f}' y='{y - 10}' width='{w:.1f}' height='20' rx='3' fill='none' "
            f"stroke='{color}' stroke-width='1'/>"
            f"<text x='{bx + 5:.1f}' y='{y + 3}' fill='{color}' font-size='12' {FONT}>{esc(net)}</text>")


def draw_ic(p, x, y):
    rows = max(len(p.left), len(p.right), 1)
    bw, ph = 210, 24
    bh = rows * ph + 34
    bx = x + 150
    col = DNP if p.dnp else INK
    dash = 'stroke-dasharray="5 3"' if p.dnp else ""
    out = [f"<rect x='{bx}' y='{y}' width='{bw}' height='{bh}' fill='#fbfbf7' stroke='{col}' "
           f"stroke-width='1.6' {dash}/>",
           f"<text x='{bx + bw / 2}' y='{y + 14}' text-anchor='middle' fill='{REF}' font-size='12' "
           f"font-weight='bold' {FONT}>{esc(p.ref)}{' (DNP)' if p.dnp else ''}</text>",
           f"<text x='{bx + bw / 2}' y='{y + bh - 6}' text-anchor='middle' fill='{col}' font-size='11' "
           f"{FONT}>{esc(p.value)}</text>"]
    for i, (name, net) in enumerate(p.left):
        py = y + 30 + i * ph
        out.append(f"<line x1='{bx - 22}' y1='{py}' x2='{bx}' y2='{py}' stroke='{col}' stroke-width='1.2'/>")
        out.append(f"<text x='{bx + 5}' y='{py + 4}' fill='{col}' font-size='11' {FONT}>{esc(name)}</text>")
        out.append(label(bx - 24, py, net, "end"))
    for i, (name, net) in enumerate(p.right):
        py = y + 30 + i * ph
        out.append(f"<line x1='{bx + bw}' y1='{py}' x2='{bx + bw + 22}' y2='{py}' stroke='{col}' "
                   f"stroke-width='1.2'/>")
        out.append(f"<text x='{bx + bw - 5}' y='{py + 4}' text-anchor='end' fill='{col}' font-size='11' "
                   f"{FONT}>{esc(name)}</text>")
        out.append(label(bx + bw + 24, py, net, "start"))
    return out, bh + 12


def draw_two(p, x, y):
    col = DNP if p.dnp else INK
    dash = 'stroke-dasharray="4 2"' if p.dnp else ""
    a, b = p.left[0][1], p.right[0][1]
    cx = x + 255
    body = 120
    out = [label(cx - body / 2 - 26, y + 9, a, "end"),
           f"<line x1='{cx - body / 2 - 24}' y1='{y + 9}' x2='{cx - body / 2}' y2='{y + 9}' stroke='{col}'/>",
           f"<rect x='{cx - body / 2}' y='{y}' width='{body}' height='18' rx='2' fill='#fbfbf7' stroke='{col}' "
           f"stroke-width='1.2' {dash}/>",
           f"<text x='{cx}' y='{y + 13}' text-anchor='middle' fill='{REF}' font-size='11' font-weight='bold' "
           f"{FONT}>{esc(p.ref)}{'*' if p.dnp else ''}</text>",
           f"<line x1='{cx + body / 2}' y1='{y + 9}' x2='{cx + body / 2 + 24}' y2='{y + 9}' stroke='{col}'/>",
           label(cx + body / 2 + 26, y + 9, b, "start"),
           f"<text x='{cx}' y='{y + 31}' text-anchor='middle' fill='{col}' font-size='10' {FONT}>"
           f"{esc(p.value)}{(' - ' + esc(p.note)) if p.note else ''}</text>"]
    return out, 40


def render_sheet(num, title):
    groups = []
    for p in PARTS:
        if p.sheet == num and p.group not in groups:
            groups.append(p.group)
    items = []  # (height, svg-producing callable)
    for gname in groups:
        parts = [p for p in PARTS if p.sheet == num and p.group == gname]
        notes = GROUP_NOTES.get((num, gname), ())
        h = 26 + 16 * len(notes)
        for p in parts:
            h += (max(len(p.left), len(p.right), 1) * 24 + 46) if p.kind != "two" else 40
        items.append((gname, parts, notes, h + 10))

    out = [f"<svg xmlns='http://www.w3.org/2000/svg' width='{W}' height='{H}' viewBox='0 0 {W} {H}'>",
           f"<rect width='{W}' height='{H}' fill='#ffffff'/>",
           f"<rect x='10' y='10' width='{W - 20}' height='{H - 20}' fill='none' stroke='{INK}' stroke-width='2'/>"]
    x, y = MARGIN, MARGIN
    bottom = H - 110
    for gname, parts, notes, h in items:
        if y + h > bottom and y > MARGIN:
            x, y = x + COLW + 18, MARGIN
        if x + COLW > W - MARGIN:
            print(f"sheet {num}: group '{gname}' does not fit", file=sys.stderr)
        out.append(f"<rect x='{x}' y='{y}' width='{COLW}' height='{h - 6}' rx='6' fill='none' "
                   f"stroke='{FRAME}' stroke-width='1.2'/>")
        out.append(f"<text x='{x + 10}' y='{y + 18}' fill='{INK}' font-size='13' font-weight='bold' "
                   f"{FONT}>{esc(gname)}</text>")
        cy = y + 30
        for p in parts:
            svg, dh = (draw_two if p.kind == "two" else draw_ic)(p, x, cy)
            out += svg
            cy += dh
        for ln in notes:
            out.append(f"<text x='{x + 10}' y='{cy + 10}' fill='#555' font-size='11' font-style='italic' "
                       f"{FONT}>{esc(ln)}</text>")
            cy += 16
        y += h + 8

    # title block
    tx, ty = W - 560, H - 100
    out.append(f"<rect x='{tx}' y='{ty}' width='540' height='80' fill='#fbfbf7' stroke='{INK}' stroke-width='1.5'/>")
    out.append(f"<text x='{tx + 12}' y='{ty + 24}' font-size='16' font-weight='bold' fill='{INK}' {FONT}>"
               f"{esc(BOARD)}</text>")
    out.append(f"<text x='{tx + 12}' y='{ty + 46}' font-size='13' fill='{INK}' {FONT}>Sheet {num}/{len(SHEETS)}: "
               f"{esc(title)}</text>")
    out.append(f"<text x='{tx + 12}' y='{ty + 66}' font-size='11' fill='#555' {FONT}>{REV}, {DATE}. "
               f"Pin numbers only where checked in the datasheet.</text>")
    out.append(f"<text x='{MARGIN}' y='{H - 40}' font-size='11' fill='#555' {FONT}>Nets with the same label are "
               f"connected. Red labels: power. Dashed / *: not populated (DNP) by default.</text>")
    out.append("</svg>")
    return "\n".join(out)


def main():
    nets = build_nets()
    problems = check(nets)
    for num, title in SHEETS.items():
        slug = "".join(c if c.isalnum() else "-" for c in title.lower()).strip("-")
        while "--" in slug:
            slug = slug.replace("--", "-")
        fn = os.path.join(HERE, f"sheet-{num:02d}-{slug[:32].strip('-')}.svg")
        with open(fn, "w") as f:
            f.write(render_sheet(num, title))
    with open(os.path.join(HERE, "..", "netlist.csv"), "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["net", "connections"])
        for net in sorted(nets):
            w.writerow([net, " ".join(f"{r}.{n}" for r, n in nets[net])])
    with open(os.path.join(HERE, "..", "bom.csv"), "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["ref", "value", "part", "package", "sheet", "function", "populate", "note"])
        for p in PARTS:
            if p.kind == "conn" and p.mpn == "-":
                continue  # bare pads and test points are copper, not parts
            w.writerow([p.ref, p.value, p.mpn, p.package, p.sheet, p.group, "DNP" if p.dnp else "yes", p.note])
    for line in problems:
        print("CHECK:", line, file=sys.stderr)
    print(f"{len(PARTS)} parts, {len(nets)} nets, {len(problems)} problems")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
