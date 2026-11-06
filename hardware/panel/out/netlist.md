# Cooler control panel - basic (4-digit display, keys, Wi-Fi/BLE) — netlist (generated from schematic.py, do not edit)

Parts: 109 · nets: 73 · check errors: 0


## Sheet 1 Bus interface and power

| Ref | Part | Value | Pins → nets | Note |
|---|---|---|---|---|
| J1 | Connector:Screw_Terminal_01x02 | BUS 2-WIRE | 1→`BUS1`, 2→`BUS2` | to drive J7, any polarity, up to 50 m 2 x 0.5 mm2 |
| F1 | Bourns MF-MSMF010 | 0.1A hold | 1→`BUS1`, 2→`BUS1F` |  |
| D1 | SMBJ33CA (bidirectional) | SMBJ33CA | 1→`BUS1F`, 2→`BUS2` |  |
| BR1 | Diotec ABS10 (1000 V 0.8 A) | ABS10 | 1/+→`BUSR`, 2/-→`GND`, 3→`BUS1F`, 4→`BUS2` |  |
| C1 | Device:C | 100nF 50V | 1→`BUSR`, 2→`GND` |  |
| R1 | Device:R | 68k 1% | 1→`BUSR`, 2→`BUS_SNS` |  |
| R2 | Device:R | 10k 1% | 1→`BUS_SNS`, 2→`GND` |  |
| C2 | Device:C | 1nF | 1→`BUS_SNS`, 2→`GND` | 8.7 us: passes 2400 bps, removes spikes |
| R3 | Device:R | 470k | 1→`BUS_RX`, 2→`BUS_SNS` | hysteresis ~0.5 V on the bus |
| R4 | Device:R | 4.02k 1% | 1→`+3V3`, 2→`VREF` |  |
| R5 | Device:R | 10k 1% | 1→`VREF`, 2→`GND` |  |
| C3 | Device:C | 100nF | 1→`VREF`, 2→`GND` |  |
| U1 | TI LMV331IDBVR (open drain) | LMV331 | 1/+→`BUS_SNS`, 2/V-→`GND`, 3/-→`VREF`, 4/~→`BUS_RX`, 5/V+→`+3V3` | output high = mark = UART idle |
| R6 | Device:R | 10k | 1→`+3V3`, 2→`BUS_RX` |  |
| C4 | Device:C | 100nF | 1→`+3V3`, 2→`GND` |  |
| R7 | Device:R | 1k | 1→`BUS_SNS`, 2→`BUS_ADC` | bus voltage monitor (diagnostics) |
| R8 | Device:R | 1k | 1→`BUS_TX`, 2→`TX_B` |  |
| R9 | Device:R | 10k | 1→`TX_B`, 2→`GND` | sink off during reset |
| Q1 | BCX56-16 (SOT-89, 1 W) | BCX56 | 1/B→`TX_B`, 2/C→`TX_C`, 3/E→`TX_E` |  |
| R10 | Device:R | 120R 1% | 1→`TX_E`, 2→`GND` | (3.1 V - 0.65 V) / 120 R = 20 mA |
| R11 | Device:R | 470R 1W | 1→`BUSR`, 2→`TX_C` | takes most of the dissipation (0.19 W) |
| U2 | LM317DCY | LM317 CC | 1/ADJ→`VSTORE`, 2/VO→`CC_OUT`, 3/VI→`BUSR` | current source 1.25 V / 24 R = 52 mA |
| R12 | Device:R | 24R 1% | 1→`CC_OUT`, 2→`VSTORE` |  |
| D2 | 1SMB5925BT3 (10 V 3 W) | 10V 3W | 1→`VSTORE`, 2→`GND` | absorbs the surplus of the constant current (max 0.52 W) |
| C5 | low-ESR 105 C | 1000uF 16V | 1→`VSTORE`, 2→`GND` | covers Wi-Fi TX bursts (~350 mA) |
| C6 | Device:C | 10uF 25V | 1→`VSTORE`, 2→`GND` |  |
| U3 | Diodes AP63203WU (fixed 3.3 V) | AP63203WU | 1/FB→`+3V3`, 2/EN→`VSTORE`, 3/IN→`VSTORE`, 4/GND→`GND`, 5/SW→`BUCK_SW`, 6/BST→`BUCK_BST` |  |
| C7 | Device:C | 100nF | 1→`BUCK_BST`, 2→`BUCK_SW` |  |
| L1 | Bourns SRN6045TA-6R8M | 6.8uH 1A | 1→`BUCK_SW`, 2→`+3V3` |  |
| C8 | Device:C | 22uF 10V | 1→`+3V3`, 2→`GND` |  |
| C9 | Device:C | 22uF 10V | 1→`+3V3`, 2→`GND` |  |
| D3 | SS14 | SS14 | 1→`VSTORE`, 2→`USB_VBUS` |  |

## Sheet 2 ESP32-S3 module and USB

| Ref | Part | Value | Pins → nets | Note |
|---|---|---|---|---|
| U4 | Espressif ESP32-S3-WROOM-1-N8 (NOT an R8 variant: IO35-37 are used) | ESP32-S3-WROOM-1-N8 | 1/GND→`GND`, 2/3V3→`+3V3`, 3/EN→`EN`, 4/IO4→`SDA`, 5/IO5→`SCL`, 6/IO6→`BUS_ADC`, 7/IO7→`NC_IO7`, 8/IO15→`NC_IO15`, 9/IO16→`NC_IO16`, 10/IO17→`BUS_TX`, 11/IO18→`BUS_RX`, 12/IO8→`NC_IO8`, 13/IO19→`USB_DN`, 14/IO20→`USB_DP`, 15/IO3→`NC_IO3`, 16/IO46→`NC_IO46`, 17/IO9→`BZ`, 18/IO10→`KEY_POWER`, 19/IO11→`KEY_UP`, 20/IO12→`KEY_DOWN`, 21/IO13→`KEY_MODE`, 22/IO14→`KEY_TIMER`, 23/IO21→`DIG5`, 24/IO47→`DIG4`, 25/IO48→`DIG3`, 26/IO45→`NC_IO45`, 27/IO0→`BOOT`, 28/IO35→`DIG2`, 29/IO36→`DIG1`, 30/IO37→`SEG_DP_G`, 31/IO38→`SEG_G_G`, 32/IO39→`SEG_F_G`, 33/IO40→`SEG_E_G`, 34/IO41→`SEG_D_G`, 35/IO42→`SEG_C_G`, 36/RXD0→`U0_RX`, 37/TXD0→`U0_TX`, 38/IO2→`SEG_B_G`, 39/IO1→`SEG_A_G`, 40/GND→`GND`, 41/GND→`GND` | antenna towards the board edge, no copper below the antenna |
| C10 | Device:C | 10uF | 1→`+3V3`, 2→`GND` |  |
| C11 | Device:C | 100nF | 1→`+3V3`, 2→`GND` |  |
| R13 | Device:R | 10k | 1→`+3V3`, 2→`EN` |  |
| C12 | Device:C | 1uF | 1→`EN`, 2→`GND` | power-on reset delay |
| R14 | Device:R | 10k | 1→`+3V3`, 2→`BOOT` |  |
| SW6 | Switch:SW_Push | BOOT | 1→`BOOT`, 2→`GND` | hold while plugging USB: download mode |
| J2 | GCT USB4105-GF-A | USB-C | A1→`GND`, A12→`GND`, B1→`GND`, B12→`GND`, A4→`USB_VBUS`, A9→`USB_VBUS`, B4→`USB_VBUS`, B9→`USB_VBUS`, A5→`USB_CC1`, B5→`USB_CC2`, A6→`USB_DP_C`, B6→`USB_DP_C`, A7→`USB_DN_C`, B7→`USB_DN_C`, A8→`NC_SBU1`, B8→`NC_SBU2`, S1→`GND` | factory programming / service (native USB serial-JTAG) |
| R15 | Device:R | 5.1k | 1→`USB_CC1`, 2→`GND` |  |
| R16 | Device:R | 5.1k | 1→`USB_CC2`, 2→`GND` |  |
| U5 | ST USBLC6-2SC6 | USBLC6-2SC6 | 1/I/O1→`USB_DP_C`, 2/GND→`GND`, 3/I/O2→`USB_DN_C`, 4/I/O2→`USB_DN_C`, 5/VBUS→`USB_VBUS`, 6/I/O1→`USB_DP_C` |  |
| R17 | Device:R | 22R | 1→`USB_DP_C`, 2→`USB_DP` |  |
| R18 | Device:R | 22R | 1→`USB_DN_C`, 2→`USB_DN` |  |

## Sheet 3 Display, status LEDs, keys, buzzer, sensor

| Ref | Part | Value | Pins → nets | Note |
|---|---|---|---|---|
| R20 | Device:R | 150R | 1→`SEG_A_G`, 2→`SEG_A` | ~8 mA peak per segment |
| R21 | Device:R | 150R | 1→`SEG_B_G`, 2→`SEG_B` | ~8 mA peak per segment |
| R22 | Device:R | 150R | 1→`SEG_C_G`, 2→`SEG_C` | ~8 mA peak per segment |
| R23 | Device:R | 150R | 1→`SEG_D_G`, 2→`SEG_D` | ~8 mA peak per segment |
| R24 | Device:R | 150R | 1→`SEG_E_G`, 2→`SEG_E` | ~8 mA peak per segment |
| R25 | Device:R | 150R | 1→`SEG_F_G`, 2→`SEG_F` | ~8 mA peak per segment |
| R26 | Device:R | 150R | 1→`SEG_G_G`, 2→`SEG_G` | ~8 mA peak per segment |
| R27 | Device:R | 150R | 1→`SEG_DP_G`, 2→`SEG_DP` | ~8 mA peak per segment |
| DS1 | Kingbright CC56-12SURKWA (0.56 in, super-bright red, common cathode) | CC56-12SURKWA | 11/a→`SEG_A`, 7/b→`SEG_B`, 4/c→`SEG_C`, 2/d→`SEG_D`, 1/e→`SEG_E`, 10/f→`SEG_F`, 5/g→`SEG_G`, 3/DPX→`SEG_DP`, 12/CC1→`DIG1_K`, 9/CC2→`DIG2_K`, 8/CC3→`DIG3_K`, 6/CC4→`DIG4_K` |  |
| Q2 | 2N7002 | 2N7002 | 1/G→`DIG1`, 2/S→`GND`, 3/D→`DIG1_K` |  |
| R28 | Device:R | 100k | 1→`DIG1`, 2→`GND` | off during reset |
| Q3 | 2N7002 | 2N7002 | 1/G→`DIG2`, 2/S→`GND`, 3/D→`DIG2_K` |  |
| R29 | Device:R | 100k | 1→`DIG2`, 2→`GND` | off during reset |
| Q4 | 2N7002 | 2N7002 | 1/G→`DIG3`, 2/S→`GND`, 3/D→`DIG3_K` |  |
| R30 | Device:R | 100k | 1→`DIG3`, 2→`GND` | off during reset |
| Q5 | 2N7002 | 2N7002 | 1/G→`DIG4`, 2/S→`GND`, 3/D→`DIG4_K` |  |
| R31 | Device:R | 100k | 1→`DIG4`, 2→`GND` | off during reset |
| Q6 | 2N7002 | 2N7002 | 1/G→`DIG5`, 2/S→`GND`, 3/D→`LED_K` |  |
| R32 | Device:R | 100k | 1→`DIG5`, 2→`GND` | off during reset |
| LED1 | green | POWER | 1→`LED_K`, 2→`SEG_A` | POWER indicator |
| LED2 | green | PUMP | 1→`LED_K`, 2→`SEG_B` | PUMP indicator |
| LED3 | green | FAN | 1→`LED_K`, 2→`SEG_C` | FAN indicator |
| LED4 | green | TIMER | 1→`LED_K`, 2→`SEG_D` | TIMER indicator |
| LED5 | red | ALARM | 1→`LED_K`, 2→`SEG_E` | ALARM indicator |
| LED6 | blue | WIFI | 1→`LED_K`, 2→`SEG_F` | WIFI indicator |
| SW1 | Switch:SW_Push | POWER | 1→`KEY_POWER`, 2→`GND` |  |
| R33 | Device:R | 10k | 1→`+3V3`, 2→`KEY_POWER` |  |
| C13 | Device:C | 10nF | 1→`KEY_POWER`, 2→`GND` | debounce / ESD |
| SW2 | Switch:SW_Push | UP | 1→`KEY_UP`, 2→`GND` |  |
| R34 | Device:R | 10k | 1→`+3V3`, 2→`KEY_UP` |  |
| C14 | Device:C | 10nF | 1→`KEY_UP`, 2→`GND` | debounce / ESD |
| SW3 | Switch:SW_Push | DOWN | 1→`KEY_DOWN`, 2→`GND` |  |
| R35 | Device:R | 10k | 1→`+3V3`, 2→`KEY_DOWN` |  |
| C15 | Device:C | 10nF | 1→`KEY_DOWN`, 2→`GND` | debounce / ESD |
| SW4 | Switch:SW_Push | MODE | 1→`KEY_MODE`, 2→`GND` |  |
| R36 | Device:R | 10k | 1→`+3V3`, 2→`KEY_MODE` |  |
| C16 | Device:C | 10nF | 1→`KEY_MODE`, 2→`GND` | debounce / ESD |
| SW5 | Switch:SW_Push | TIMER | 1→`KEY_TIMER`, 2→`GND` |  |
| R37 | Device:R | 10k | 1→`+3V3`, 2→`KEY_TIMER` |  |
| C17 | Device:C | 10nF | 1→`KEY_TIMER`, 2→`GND` | debounce / ESD |
| BZ1 | magnetic buzzer 3 V, 2.7 kHz (e.g. CUI CMT-1203) | 3V magnetic | 1→`+3V3`, 2→`BZ_DRV` |  |
| Q7 | 2N7002 | 2N7002 | 1/G→`BZ`, 2/S→`GND`, 3/D→`BZ_DRV` |  |
| R38 | Device:R | 100k | 1→`BZ`, 2→`GND` |  |
| D4 | 1N4148W | 1N4148W | 1→`+3V3`, 2→`BZ_DRV` |  |
| U6 (DNP) | Sensirion SHT40-AD1B | SHT40 | 1/SDA→`SDA`, 2/SCL→`SCL`, 3/VDD→`+3V3`, 4/VSS→`GND` | OPTION: away from the regulator, slot in the PCB |
| C18 | Device:C | 100nF | 1→`+3V3`, 2→`GND` |  |
| R39 | Device:R | 4.7k | 1→`+3V3`, 2→`SDA` |  |
| R40 | Device:R | 4.7k | 1→`+3V3`, 2→`SCL` |  |

## Sheet 4 Mechanics and test points

| Ref | Part | Value | Pins → nets | Note |
|---|---|---|---|---|
| H1 | Mechanical:MountingHole | M3 |  |  |
| H2 | Mechanical:MountingHole | M3 |  |  |
| H3 | Mechanical:MountingHole | M3 |  |  |
| H4 | Mechanical:MountingHole | M3 |  |  |
| FID1 | Mechanical:Fiducial | FID |  |  |
| FID2 | Mechanical:Fiducial | FID |  |  |
| FID3 | Mechanical:Fiducial | FID |  |  |
| TP1 | Connector:TestPoint | BUSR | 1→`BUSR` |  |
| TP2 | Connector:TestPoint | VSTORE | 1→`VSTORE` |  |
| TP3 | Connector:TestPoint | +3V3 | 1→`+3V3` |  |
| TP4 | Connector:TestPoint | GND | 1→`GND` |  |
| TP5 | Connector:TestPoint | BUS_TX | 1→`BUS_TX` |  |
| TP6 | Connector:TestPoint | BUS_RX | 1→`BUS_RX` |  |
| TP7 | Connector:TestPoint | U0_TX | 1→`U0_TX` |  |
| TP8 | Connector:TestPoint | U0_RX | 1→`U0_RX` |  |
| TP9 | Connector:TestPoint | EN | 1→`EN` |  |

## Net index

| Net | Nodes |
|---|---|
| `+3V3` | R4.1 U1.5 R6.1 C4.1 U3.1 L1.2 C8.1 C9.1 U4.2 C10.1 C11.1 R13.1 R14.1 R33.1 R34.1 R35.1 R36.1 R37.1 BZ1.1 D4.1 U6.3 C18.1 R39.1 R40.1 TP3.1 |
| `BOOT` | U4.27 R14.2 SW6.1 |
| `BUCK_BST` | U3.6 C7.1 |
| `BUCK_SW` | U3.5 C7.2 L1.1 |
| `BUS1` | J1.1 F1.1 |
| `BUS1F` | F1.2 D1.1 BR1.3 |
| `BUS2` | J1.2 D1.2 BR1.4 |
| `BUSR` | BR1.1 C1.1 R1.1 R11.1 U2.3 TP1.1 |
| `BUS_ADC` | R7.2 U4.6 |
| `BUS_RX` | R3.1 U1.4 R6.2 U4.11 TP6.1 |
| `BUS_SNS` | R1.2 R2.1 C2.1 R3.2 U1.1 R7.1 |
| `BUS_TX` | R8.1 U4.10 TP5.1 |
| `BZ` | U4.17 Q7.1 R38.1 |
| `BZ_DRV` | BZ1.2 Q7.3 D4.2 |
| `CC_OUT` | U2.2 R12.1 |
| `DIG1` | U4.29 Q2.1 R28.1 |
| `DIG1_K` | DS1.12 Q2.3 |
| `DIG2` | U4.28 Q3.1 R29.1 |
| `DIG2_K` | DS1.9 Q3.3 |
| `DIG3` | U4.25 Q4.1 R30.1 |
| `DIG3_K` | DS1.8 Q4.3 |
| `DIG4` | U4.24 Q5.1 R31.1 |
| `DIG4_K` | DS1.6 Q5.3 |
| `DIG5` | U4.23 Q6.1 R32.1 |
| `EN` | U4.3 R13.2 C12.1 TP9.1 |
| `GND` | BR1.2 C1.2 R2.2 C2.2 R5.2 C3.2 U1.2 C4.2 R9.2 R10.2 D2.2 C5.2 C6.2 U3.4 C8.2 C9.2 U4.1 U4.40 U4.41 C10.2 C11.2 C12.2 SW6.2 J2.A1 J2.A12 J2.B1 J2.B12 J2.S1 R15.2 R16.2 U5.2 Q2.2 R28.2 Q3.2 R29.2 Q4.2 R30.2 Q5.2 R31.2 Q6.2 R32.2 SW1.2 C13.2 SW2.2 C14.2 SW3.2 C15.2 SW4.2 C16.2 SW5.2 C17.2 Q7.2 R38.2 U6.4 C18.2 TP4.1 |
| `KEY_DOWN` | U4.20 SW3.1 R35.2 C15.1 |
| `KEY_MODE` | U4.21 SW4.1 R36.2 C16.1 |
| `KEY_POWER` | U4.18 SW1.1 R33.2 C13.1 |
| `KEY_TIMER` | U4.22 SW5.1 R37.2 C17.1 |
| `KEY_UP` | U4.19 SW2.1 R34.2 C14.1 |
| `LED_K` | Q6.3 LED1.1 LED2.1 LED3.1 LED4.1 LED5.1 LED6.1 |
| `NC_IO15` | U4.8 |
| `NC_IO16` | U4.9 |
| `NC_IO3` | U4.15 |
| `NC_IO45` | U4.26 |
| `NC_IO46` | U4.16 |
| `NC_IO7` | U4.7 |
| `NC_IO8` | U4.12 |
| `NC_SBU1` | J2.A8 |
| `NC_SBU2` | J2.B8 |
| `SCL` | U4.5 U6.2 R40.2 |
| `SDA` | U4.4 U6.1 R39.2 |
| `SEG_A` | R20.2 DS1.11 LED1.2 |
| `SEG_A_G` | U4.39 R20.1 |
| `SEG_B` | R21.2 DS1.7 LED2.2 |
| `SEG_B_G` | U4.38 R21.1 |
| `SEG_C` | R22.2 DS1.4 LED3.2 |
| `SEG_C_G` | U4.35 R22.1 |
| `SEG_D` | R23.2 DS1.2 LED4.2 |
| `SEG_DP` | R27.2 DS1.3 |
| `SEG_DP_G` | U4.30 R27.1 |
| `SEG_D_G` | U4.34 R23.1 |
| `SEG_E` | R24.2 DS1.1 LED5.2 |
| `SEG_E_G` | U4.33 R24.1 |
| `SEG_F` | R25.2 DS1.10 LED6.2 |
| `SEG_F_G` | U4.32 R25.1 |
| `SEG_G` | R26.2 DS1.5 |
| `SEG_G_G` | U4.31 R26.1 |
| `TX_B` | R8.2 R9.1 Q1.1 |
| `TX_C` | Q1.2 R11.2 |
| `TX_E` | Q1.3 R10.1 |
| `U0_RX` | U4.36 TP8.1 |
| `U0_TX` | U4.37 TP7.1 |
| `USB_CC1` | J2.A5 R15.1 |
| `USB_CC2` | J2.B5 R16.1 |
| `USB_DN` | U4.13 R18.2 |
| `USB_DN_C` | J2.A7 J2.B7 U5.3 U5.4 R18.1 |
| `USB_DP` | U4.14 R17.2 |
| `USB_DP_C` | J2.A6 J2.B6 U5.1 U5.6 R17.1 |
| `USB_VBUS` | D3.2 J2.A4 J2.A9 J2.B4 J2.B9 U5.5 |
| `VREF` | R4.2 R5.1 C3.1 U1.3 |
| `VSTORE` | U2.1 R12.2 D2.1 C5.1 C6.1 U3.2 U3.3 D3.1 TP2.1 |
