#!/usr/bin/env python3
"""
schematic.py - Source of truth for the cooler control panel (basic version:
4-digit 7-segment display, 6 status LEDs, 5 keys, buzzer, Wi-Fi/BLE).

Validated by hardware/v2/check_and_build.py --design hardware/panel against
the official KiCad 7.0.11 libraries; gen_kicad.py builds the KiCad project.

Everything on this board is SELV: it is powered only by the 2-wire panel bus
of the drive (24 V mark / 16 V space, current limited to 150 mA, isolated by
the drive's flyback transformer) or by USB for factory programming.

Bus interface (matches hardware/v2 sheet 7 and firmware/v2/phy):
  - polarity-free: bridge rectifier
  - constant input current: LM317 current source (52 mA) charges VSTORE,
    so Wi-Fi bursts do not modulate the bus current the drive listens to
  - drive -> panel: comparator on the bus voltage (threshold ~18.4 V)
  - panel -> drive: 20 mA current sink switched by UART TX (inverted)
Conventions as in hardware/v2/schematic.py (Device:D 1 = K, 2N7002 1 G 2 S 3 D).
"""

PARTS = []
_sheet = [""]


def sheet(name):
    _sheet[0] = name


def P(ref, sym, value, fp, pins, mpn="", note="", names=None, dnp=False):
    PARTS.append(dict(ref=ref, sym=sym, value=value, fp=fp, pins={str(k): v for k, v in pins.items()},
                      mpn=mpn, note=note, names={str(k): v for k, v in (names or {}).items()},
                      sheet=_sheet[0], dnp=dnp))


R0603 = "Resistor_SMD:R_0603_1608Metric"
R1206 = "Resistor_SMD:R_1206_3216Metric"
R2512 = "Resistor_SMD:R_2512_6332Metric"
C0603 = "Capacitor_SMD:C_0603_1608Metric"
C0805 = "Capacitor_SMD:C_0805_2012Metric"
C1206 = "Capacitor_SMD:C_1206_3216Metric"
SOT23 = "Package_TO_SOT_SMD:SOT-23"
SOD123 = "Diode_SMD:D_SOD-123"
LED0805 = "LED_SMD:LED_0805_2012Metric"
KEY = "Button_Switch_THT:SW_PUSH_6mm"


def R(ref, val, a, b, fp=R0603, note=""):
    P(ref, "Device:R", val, fp, {1: a, 2: b}, note=note)


def C(ref, val, a, b, fp=C0603, note=""):
    P(ref, "Device:C", val, fp, {1: a, 2: b}, note=note)


def NMOS(ref, g, d):
    P(ref, "Transistor_FET:2N7002", "2N7002", SOT23, {1: g, 2: "GND", 3: d}, mpn="2N7002",
      names={1: "G", 2: "S", 3: "D"})


# ========================================================================
sheet("1 Bus interface and power")
# ========================================================================
P("J1", "Connector:Screw_Terminal_01x02", "BUS 2-WIRE", "TerminalBlock_Phoenix:TerminalBlock_Phoenix_MKDS-1,5-2-5.08_1x02_P5.08mm_Horizontal",
  {1: "BUS1", 2: "BUS2"}, note="to drive J7, any polarity, up to 50 m 2 x 0.5 mm2")
P("F1", "Device:Polyfuse", "0.1A hold", "Fuse:Fuse_1812_4532Metric", {1: "BUS1", 2: "BUS1F"}, mpn="Bourns MF-MSMF010")
P("D1", "Device:D_TVS", "SMBJ33CA", "Diode_SMD:D_SMB", {1: "BUS1F", 2: "BUS2"}, mpn="SMBJ33CA (bidirectional)")
P("BR1", "Diode_Bridge:ABS10", "ABS10", "Diode_SMD:Diode_Bridge_Diotec_ABS",
  {1: "BUSR", 2: "GND", 3: "BUS1F", 4: "BUS2"}, names={1: "+", 2: "-"}, mpn="Diotec ABS10 (1000 V 0.8 A)")
C("C1", "100nF 50V", "BUSR", "GND", C0805)
# drive -> panel: bus voltage comparator (mark 22.4 V / space 14.4 V after the bridge, threshold 18.4 V)
R("R1", "68k 1%", "BUSR", "BUS_SNS")
R("R2", "10k 1%", "BUS_SNS", "GND")
C("C2", "1nF", "BUS_SNS", "GND", note="8.7 us: passes 2400 bps, removes spikes")
R("R3", "470k", "BUS_RX", "BUS_SNS", note="hysteresis ~0.5 V on the bus")
R("R4", "4.02k 1%", "+3V3", "VREF")
R("R5", "10k 1%", "VREF", "GND")
C("C3", "100nF", "VREF", "GND")
P("U1", "Comparator:LMV331", "LMV331", "Package_TO_SOT_SMD:SOT-23-5",
  {1: "BUS_SNS", 2: "GND", 3: "VREF", 4: "BUS_RX", 5: "+3V3"}, names={1: "+", 2: "V-", 3: "-", 4: "~", 5: "V+"},
  mpn="TI LMV331IDBVR (open drain)", note="output high = mark = UART idle")
R("R6", "10k", "+3V3", "BUS_RX")
C("C4", "100nF", "+3V3", "GND")
R("R7", "1k", "BUS_SNS", "BUS_ADC", note="bus voltage monitor (diagnostics)")
# panel -> drive: 20 mA current sink, on = space (UART TX inverted in firmware)
R("R8", "1k", "BUS_TX", "TX_B")
R("R9", "10k", "TX_B", "GND", note="sink off during reset")
P("Q1", "Device:Q_NPN_BCE", "BCX56", "Package_TO_SOT_SMD:SOT-89-3",
  {1: "TX_B", 2: "TX_C", 3: "TX_E"}, names={1: "B", 2: "C", 3: "E"}, mpn="BCX56-16 (SOT-89, 1 W)")
R("R10", "120R 1%", "TX_E", "GND", note="(3.1 V - 0.65 V) / 120 R = 20 mA")
R("R11", "470R 1W", "BUSR", "TX_C", R2512, "takes most of the dissipation (0.19 W)")
# constant-current input: the bus current stays at 52 mA whatever the Wi-Fi does
P("U2", "Regulator_Linear:LM317_SOT-223", "LM317 CC", "Package_TO_SOT_SMD:SOT-223-3_TabPin2",
  {1: "VSTORE", 2: "CC_OUT", 3: "BUSR"}, names={1: "ADJ", 2: "VO", 3: "VI"}, mpn="LM317DCY",
  note="current source 1.25 V / 24 R = 52 mA")
R("R12", "24R 1%", "CC_OUT", "VSTORE", R1206)
P("D2", "Device:D_Zener", "10V 3W", "Diode_SMD:D_SMB", {1: "VSTORE", 2: "GND"}, mpn="1SMB5925BT3 (10 V 3 W)",
  note="absorbs the surplus of the constant current (max 0.52 W)")
P("C5", "Device:C_Polarized", "1000uF 16V", "Capacitor_THT:CP_Radial_D10.0mm_P5.00mm", {1: "VSTORE", 2: "GND"},
  mpn="low-ESR 105 C", note="covers Wi-Fi TX bursts (~350 mA)")
C("C6", "10uF 25V", "VSTORE", "GND", C1206)
# 3.3 V buck (same circuit as the drive's U3)
P("U3", "Regulator_Switching:AP63203WU", "AP63203WU", "Package_TO_SOT_SMD:TSOT-23-6",
  {1: "+3V3", 2: "VSTORE", 3: "VSTORE", 4: "GND", 5: "BUCK_SW", 6: "BUCK_BST"},
  names={1: "FB", 2: "EN", 3: "IN", 4: "GND", 5: "SW", 6: "BST"}, mpn="Diodes AP63203WU (fixed 3.3 V)")
C("C7", "100nF", "BUCK_BST", "BUCK_SW")
P("L1", "Device:L", "6.8uH 1A", "Inductor_SMD:L_Bourns_SRN6045TA", {1: "BUCK_SW", 2: "+3V3"}, mpn="Bourns SRN6045TA-6R8M")
C("C8", "22uF 10V", "+3V3", "GND", C1206)
C("C9", "22uF 10V", "+3V3", "GND", C1206)
# USB 5 V for factory programming without the bus
P("D3", "Device:D_Schottky", "SS14", "Diode_SMD:D_SMA", {1: "VSTORE", 2: "USB_VBUS"}, mpn="SS14")

# ========================================================================
sheet("2 ESP32-S3 module and USB")
# ========================================================================
NC = lambda n: f"NC_{n}"
ESP = {   # signals leave the module on the side facing their destination (display right, keys below, bus left)
    1: ("GND", "GND"), 2: ("3V3", "+3V3"), 3: ("EN", "EN"),
    4: ("IO4", "SDA"), 5: ("IO5", "SCL"), 6: ("IO6", "BUS_ADC"), 7: ("IO7", NC("IO7")),
    8: ("IO15", NC("IO15")), 9: ("IO16", NC("IO16")), 10: ("IO17", "BUS_TX"), 11: ("IO18", "BUS_RX"),
    12: ("IO8", NC("IO8")), 13: ("IO19", "USB_DN"), 14: ("IO20", "USB_DP"), 15: ("IO3", NC("IO3")),
    16: ("IO46", NC("IO46")), 17: ("IO9", "BZ"), 18: ("IO10", "KEY_POWER"), 19: ("IO11", "KEY_UP"),
    20: ("IO12", "KEY_DOWN"), 21: ("IO13", "KEY_MODE"), 22: ("IO14", "KEY_TIMER"), 23: ("IO21", "DIG5"),
    24: ("IO47", "DIG4"), 25: ("IO48", "DIG3"), 26: ("IO45", NC("IO45")), 27: ("IO0", "BOOT"),
    28: ("IO35", "DIG2"), 29: ("IO36", "DIG1"), 30: ("IO37", "SEG_DP_G"),
    31: ("IO38", "SEG_G_G"), 32: ("IO39", "SEG_F_G"), 33: ("IO40", "SEG_E_G"), 34: ("IO41", "SEG_D_G"),
    35: ("IO42", "SEG_C_G"), 36: ("RXD0", "U0_RX"), 37: ("TXD0", "U0_TX"), 38: ("IO2", "SEG_B_G"),
    39: ("IO1", "SEG_A_G"), 40: ("GND", "GND"), 41: ("GND", "GND"),
}
P("U4", "RF_Module:ESP32-S3-WROOM-1", "ESP32-S3-WROOM-1-N8", "RF_Module:ESP32-S3-WROOM-1",
  {k: v[1] for k, v in ESP.items()}, names={k: v[0] for k, v in ESP.items()},
  mpn="Espressif ESP32-S3-WROOM-1-N8 (NOT an R8 variant: IO35-37 are used)",
  note="antenna towards the board edge, no copper below the antenna")
C("C10", "10uF", "+3V3", "GND", C0805)
C("C11", "100nF", "+3V3", "GND")
R("R13", "10k", "+3V3", "EN")
C("C12", "1uF", "EN", "GND", note="power-on reset delay")
R("R14", "10k", "+3V3", "BOOT")
P("SW6", "Switch:SW_Push", "BOOT", KEY, {1: "BOOT", 2: "GND"}, note="hold while plugging USB: download mode")
P("J2", "Connector:USB_C_Receptacle_USB2.0_16P", "USB-C", "Connector_USB:USB_C_Receptacle_GCT_USB4105-xx-A_16P_TopMnt_Horizontal",
  {"A1": "GND", "A12": "GND", "B1": "GND", "B12": "GND", "A4": "USB_VBUS", "A9": "USB_VBUS", "B4": "USB_VBUS",
   "B9": "USB_VBUS", "A5": "USB_CC1", "B5": "USB_CC2", "A6": "USB_DP_C", "B6": "USB_DP_C", "A7": "USB_DN_C",
   "B7": "USB_DN_C", "A8": NC("SBU1"), "B8": NC("SBU2"), "S1": "GND"},
  mpn="GCT USB4105-GF-A", note="factory programming / service (native USB serial-JTAG)")
R("R15", "5.1k", "USB_CC1", "GND")
R("R16", "5.1k", "USB_CC2", "GND")
P("U5", "Power_Protection:USBLC6-2SC6", "USBLC6-2SC6", "Package_TO_SOT_SMD:SOT-23-6",
  {1: "USB_DP_C", 2: "GND", 3: "USB_DN_C", 4: "USB_DN_C", 5: "USB_VBUS", 6: "USB_DP_C"},
  names={1: "I/O1", 2: "GND", 3: "I/O2", 4: "I/O2", 5: "VBUS", 6: "I/O1"}, mpn="ST USBLC6-2SC6")
R("R17", "22R", "USB_DP_C", "USB_DP")
R("R18", "22R", "USB_DN_C", "USB_DN")

# ========================================================================
sheet("3 Display, status LEDs, keys, buzzer, sensor")
# ========================================================================
# 4-digit common-cathode display + 6 LEDs multiplexed in 5 phases (DIG1..4, DIG5 = LEDs)
SEGS = ["A", "B", "C", "D", "E", "F", "G", "DP"]
for i, sname in enumerate(SEGS):
    R(f"R{20+i}", "150R", f"SEG_{sname}_G", f"SEG_{sname}", note="~8 mA peak per segment")
P("DS1", "Display_Character:CC56-12SURKWA", "CC56-12SURKWA", "Display_7Segment:CA56-12EWA",
  {11: "SEG_A", 7: "SEG_B", 4: "SEG_C", 2: "SEG_D", 1: "SEG_E", 10: "SEG_F", 5: "SEG_G", 3: "SEG_DP",
   12: "DIG1_K", 9: "DIG2_K", 8: "DIG3_K", 6: "DIG4_K"},
  names={11: "a", 7: "b", 4: "c", 2: "d", 1: "e", 10: "f", 5: "g", 3: "DPX", 12: "CC1", 9: "CC2", 8: "CC3", 6: "CC4"},
  mpn="Kingbright CC56-12SURKWA (0.56 in, super-bright red, common cathode)")
for i in range(5):
    NMOS(f"Q{2+i}", f"DIG{i+1}", f"DIG{i+1}_K" if i < 4 else "LED_K")
    R(f"R{28+i}", "100k", f"DIG{i+1}", "GND", note="off during reset")
for i, (name, seg) in enumerate([("POWER", "A"), ("PUMP", "B"), ("FAN", "C"), ("TIMER", "D"), ("ALARM", "E"), ("WIFI", "F")]):
    P(f"LED{i+1}", "Device:LED", name, LED0805, {1: "LED_K", 2: f"SEG_{seg}"},
      mpn="red" if name == "ALARM" else ("blue" if name == "WIFI" else "green"), note=f"{name} indicator")
# keys (to GND, external pull-ups: long cable to a membrane is possible)
for i, k in enumerate(["POWER", "UP", "DOWN", "MODE", "TIMER"]):
    P(f"SW{i+1}", "Switch:SW_Push", k, KEY, {1: f"KEY_{k}", 2: "GND"})
    R(f"R{33+i}", "10k", "+3V3", f"KEY_{k}")
    C(f"C{13+i}", "10nF", f"KEY_{k}", "GND", note="debounce / ESD")
# buzzer (alarm, key click)
P("BZ1", "Device:Buzzer", "3V magnetic", "Buzzer_Beeper:Buzzer_12x9.5RM7.6", {1: "+3V3", 2: "BZ_DRV"},
  mpn="magnetic buzzer 3 V, 2.7 kHz (e.g. CUI CMT-1203)")
NMOS("Q7", "BZ", "BZ_DRV")
R("R38", "100k", "BZ", "GND")
P("D4", "Device:D", "1N4148W", SOD123, {1: "+3V3", 2: "BZ_DRV"}, mpn="1N4148W")
# optional temperature / humidity sensor for an automatic mode (not fitted in the basic version)
P("U6", "Sensor_Humidity:SHT4x", "SHT40", "Sensor_Humidity:Sensirion_DFN-4_1.5x1.5mm_P0.8mm_SHT4x_NoCentralPad",
  {1: "SDA", 2: "SCL", 3: "+3V3", 4: "GND"}, names={1: "SDA", 2: "SCL", 3: "VDD", 4: "VSS"},
  mpn="Sensirion SHT40-AD1B", dnp=True, note="OPTION: away from the regulator, slot in the PCB")
C("C18", "100nF", "+3V3", "GND")
R("R39", "4.7k", "+3V3", "SDA")
R("R40", "4.7k", "+3V3", "SCL")

# ========================================================================
sheet("4 Mechanics and test points")
# ========================================================================
for i in range(4):
    P(f"H{i+1}", "Mechanical:MountingHole", "M3", "MountingHole:MountingHole_3.2mm_M3", {})
for i in range(3):
    P(f"FID{i+1}", "Mechanical:Fiducial", "FID", "Fiducial:Fiducial_1mm_Mask2mm", {})
for i, n in enumerate(["BUSR", "VSTORE", "+3V3", "GND", "BUS_TX", "BUS_RX", "U0_TX", "U0_RX", "EN"]):
    P(f"TP{i+1}", "Connector:TestPoint", n, "TestPoint:TestPoint_Pad_D1.5mm", {1: n})

# ---------------------------------------------------------------- design checks
POWER_NETS = ["+3V3", "GND", "VSTORE", "BUSR", "USB_VBUS"]
MCU_CHECKS = []          # ESP32-S3 pins are checked by name against the KiCad module symbol
ISOLATION = None         # whole board is SELV
