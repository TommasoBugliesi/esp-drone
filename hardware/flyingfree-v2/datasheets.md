# Datasheets

One line per part, with the facts that were checked and what they changed in the design. Read 2026-10-23.
Where the maker's site refused automated access, the source says so; download those by hand and check that
the revision matches.

The PDFs are not kept in the repository (size). Save them locally under `hardware/flyingfree-v2/datasheets/`,
which git ignores.

## Main parts

| Part | Datasheet | Checked facts | Effect on the design |
|---|---|---|---|
| ESP32-S3-MINI-1U-N8 | [module, v1.7](https://www.espressif.com/sites/default/files/documentation/esp32-s3-mini-1_mini-1u_datasheet_en.pdf), [chip, v2.2](https://www.espressif.com/sites/default/files/documentation/esp32-s3_datasheet_en.pdf), [TRM](https://www.espressif.com/sites/default/files/documentation/esp32-s3_technical_reference_manual_en.pdf), [hardware design guidelines](https://docs.espressif.com/projects/esp-hardware-design-guidelines/en/latest/esp32s3/) | -1U is **15.4 x 15.4 x 2.4 mm**, 65 bottom pads (LGA, no castellations), optional thermal pad. IO26 and IO33-37 usable on N8. **GPIO1-18 glitch low ~60 us at power-up**, 19/20 longer; 0, 21, 26, 33-48 quiet. Supply >= 0.5 A, 3.0-3.6 V, >= 10 uF at the entrance. EN RC 10 k / 1 uF. Wi-Fi TX peak 355 mA. | motors, servos and the 9 V enable moved to quiet pins (21, 33-38); smaller module in the floorplan |
| ICM-42688-P | [DS-000347 v1.9](https://d17t6iyxenbwp1.cloudfront.net/s3fs-public/2026-06/DS-000347%20ICM-42688-P%20v1.9.pdf) (TDK CDN, linked from the [product page](https://invensense.tdk.com/products/motion-tracking/6-axis/icm-42688-p/)) | LGA-14 2.5 x 3 x 0.91 mm. SPI 24 MHz. VDD and VDDIO 1.71-3.6 V. Gyro 2.8 mdps/rtHz. **RESV pin 7 must go to GND**; 2, 3, 10, 11 NC or GND. Pin 9 INT2, FSYNC or CLKIN (31-50 kHz). Caps: VDD 0.1 uF + 2.2 uF, VDDIO 10 nF. | pin list and caps in the schematic |
| MMC5983MA | [Rev A](https://www.memsic.com/Public/Uploads/uploadfile/files/20220119/MMC5983MADatasheetRevA.pdf) | LGA-16 3 x 3 x 1 mm. VDD and VDDIO **2.8-3.6 V** (no 1.8 V I/O). SPI 10 MHz, SCK idles high, so mode 3 (inferred, the datasheet does not name it). CS tied to VDDIO selects I2C. Continuous up to 1000 Hz (BW=11, 1.2 mG rms); 100 Hz at 0.4 mG. **CAP pin >= 10 uF low ESR** for set/reset. INT active high. | 10 uF on CAP, SPI mode 3 in the driver |
| DPS310 | [v1.2 (Infineon community copy)](https://community.infineon.com/gfawx74859/attachments/gfawx74859/KnowledgeBaseArticles/10104/3/Infineon-DPS310-DataSheet.pdf); Infineon's own pages did not resolve | VLGA-8 2.0 x 2.5 mm, 0.65 mm pitch. 0x77 with SDO high. CSB high for I2C. 0.2 Pa rms in high precision. Lifecycle not confirmed (Infineon now promotes the DPS368). | chosen barometer; check the lifecycle before the BOM freeze |
| BMP390 (alternative) | [rev 1.7](https://www.bosch-sensortec.com/media/boschsensortec/downloads/datasheets/bst-bmp390-ds002.pdf) | 10-pin LGA 2 x 2 mm. 0x76/0x77, SDO must not float. 0.9 Pa rms. | fallback if the DPS310 is not available |
| BMP280 (reference) | [rev 1.26](https://www.bosch-sensortec.com/media/boschsensortec/downloads/datasheets/bst-bmp280-ds001.pdf) | 8-pin LGA 2.0 x 2.5 mm, 1.3 Pa | not chosen |
| SPL06-001 | [LCSC copy](https://www.lcsc.com/datasheet/C2684428.pdf) | listed obsolete by a distributor | rejected |
| MP28167-A | [Rev 1.0 (RS copy)](https://docs.rs-online.com/04d0/A700000013716514.pdf), [product page](https://www.monolithicpower.com/en/mp28167-a.html) | VIN 2.8-22 V operating, **26 V absolute** (EN 26 V, OUT 24 V). UVLO 2.65 V rising. VREF 1.000 V by default; R1 = R2 (VOUT - VREF) / VREF; table 1 for 5 V: 430 k / 107 k, L 4.7 uH. EN 1.10 V rising, 2 MOhm internal pull-down. Pins: 1 IN, 2/11 GND, 3 EN, 4 ALT, 5 SCL, 6 SDA (to VCC if unused), 7 OC, 8 FB, 9 VCC (1 uF), 10 AGND, 12 OUT, 13 BST2, 14 SW2, 15 SW1, 16 BST1. COUT >= 5 x 22 uF. Default forced PWM; output discharge 60-100 Ohm for up to 50 ms when off. | real pin list, dividers computed; TVS changed (below); USB-only case to bench-test |
| TLV62569 | [SLVSDG1C](https://www.ti.com/lit/ds/symlink/tlv62569.pdf) | VIN 2.5-5.5 V, 2 A, 3 A limit (SOT-23). VFB 0.6 V. SOT-23-5: 1 EN, 2 GND, 3 SW, 4 VIN, 5 FB. CIN 4.7 uF, COUT 10-47 uF, L 2.2 uH. | values in the schematic |
| LP5907-3.3 | [SNVS798Q](https://www.ti.com/lit/ds/symlink/lp5907.pdf) | VIN 2.2-5.5 V, 250 mA, PSRR 82 dB at 1 kHz, < 6.5 uVrms. SOT-23-5: 1 IN, 2 GND, 3 EN, 4 NC, 5 OUT. 1 uF in and out. TI points to the TPS7A20 as the newer part. | as planned |
| AT7456E | [Chinese datasheet (third-party copy)](https://file01.andesource.com/dkpdf/AT7456E_001.pdf); no copy on the maker's site | **3.15-5.25 V, characterised at 5 V.** TTL inputs (VIH 2.0 V). **SDOUT swings to DVDD.** HTSSOP-28 with exposed pad (AGND). 27 MHz crystal between CLKIN and XFB. The pin table lists pin 13 twice: check the pinout figure. | 5 V supply; SDOUT buffered by a 74LVC1G125 so it neither exceeds 3.3 V nor loads the shared MISO; OSD moved to the top side |
| MAX7456 (reference) | [Analog Devices](https://www.analog.com/media/en/technical-documentation/data-sheets/MAX7456.pdf) | not read (timed out) | the register map the Betaflight driver follows |

## Protection, interface and logic

| Part | Datasheet | Checked facts | Effect |
|---|---|---|---|
| SMBJ13A | [Littelfuse SMBJ series](https://www.littelfuse.com/assetdocs/littelfuse-tvs-diode-smbj-datasheet) (site refused automated access; figures from distributors) | SMBJ14A: VR 14 V, clamp **23.2 V** at 25.9 A, above the MP28167-A's 22 V operating limit. SMBJ13A: VR 13 V, clamp about 21.5 V. | **SMBJ13A** chosen; confirm the 13A clamp in the official sheet |
| USBLC6-2SC6 | [ST](https://www.st.com/resource/en/datasheet/usblc6-2.pdf) (read: LCSC copy, rev 5, 2011) | SOT-23-6: 1/6 I/O1, 3/4 I/O2, 2 GND, 5 VBUS. 100 nF on VBUS. +-8 kV contact. | pin list, 100 nF added |
| USB4105-GF-A | [GCT drawing](https://gct.co/files/drawings/usb4105.pdf) | USB 2.0, 16 contacts, through-hole shell stakes | as planned |
| SN74AHCT1G125 | [TI SCLS378P](https://www.ti.com/lit/ds/symlink/sn74ahct1g125.pdf) | VCC 4.5-5.5 V, VIH 2.0 V. SOT-23-5: 1 OE#, 2 A, 3 GND, 4 Y, 5 VCC. | LED strip buffer, TI part used |
| SN74LVC1G125 | [TI](https://www.ti.com/lit/ds/symlink/sn74lvc1g125.pdf) | not yet read: confirm 5.5 V tolerant input at VCC 3.3 V and the same pinout | OSD SDOUT buffer |
| AO3400A | [AOS](https://www.aosmd.com/res/datasheets/AO3400A.pdf) | 30 V, 5.7 A, RDS(on) < 48 mOhm at 2.5 V gate | buzzer and 9 V enable switch |
| 24LC256 | [Microchip DS20001203W](https://ww1.microchip.com/downloads/en/DeviceDoc/24AA256-24LC256-24FC256-Data-Sheet-20001203W.pdf) | 2.5-5.5 V, 64-byte page, 5 ms write | optional EEPROM |

## Connectors and passives

| Part | Datasheet | Checked facts | Effect |
|---|---|---|---|
| Hirose DM3AT-SF-PEJM5 | Hirose DM3 series (official PDF not reached; distributors) | push-push, 1.68 mm high | **replaces Molex 104031-0811, which is push-pull** |
| JST SH (SM06B/SM08B-SRSS-TB) | [JST eSH](https://www.jst-mfg.com/product/pdf/eng/eSH.pdf) | 1.0 mm pitch, **1 A per contact**, side entry about 2.95 mm high | a 2 A air unit needs its power on more than one contact or a pad |
| Power inductor, 4.7 uH | [Coilcraft XGL4020](https://www.coilcraft.com/en-us/products/power/high-voltage-inductors/xgl/xgl4020/), [Wurth WE-XHMI](https://www.we-online.com/en/components/products/WE-XHMI) | XGL4020-472: Isat 3.0 A at 20 % drop, marginal for the buck-boost peak at 2.8 V input | **5 x 5 mm class** with Isat >= 4 A |

## Video systems

| Unit | Source | Input | Effect |
|---|---|---|---|
| DJI O3 Air Unit | DJI support page | 7.4-26.4 V; no official power figure, about 2 A at 9 V by third-party measurements | 9 V rail, 2S and up |
| DJI O4 Air Unit (not Pro) | dji.com specs | **3.7-13.2 V** | can run from VBAT on 1S-3S through JP1 |
| DJI O4 Air Unit Pro | dji.com specs | 7.4-26.4 V | 9 V rail |
| Walksnail Avatar HD | retailers only | 6-25.2 V | 9 V rail |

## Still to read

MAX7456, SN74LVC1G125, the Littelfuse SMBJ sheet, the Hirose DM3AT drawing, the Nexperia BAT54S and
PMEG3020, the 5 x 5 mm inductor, and the BOOT switch.
