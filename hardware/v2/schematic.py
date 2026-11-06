#!/usr/bin/env python3
"""
schematic.py - Source of truth for the BLDC cooler drive v2 schematic.

Every part, pin and net is defined here. `check_and_build.py` validates it
against the official KiCad symbol/footprint libraries and ST pin data, and
generates the human-readable netlist, the BOM and a KiCad netlist that the
PCB editor imports directly (File > Import > Netlist).

Conventions
  Device:R/C/L/Fuse/...  pins 1,2        Device:C_Polarized  1 = +
  Device:D, D_Zener, D_Schottky, LED     1 = K (cathode), 2 = A
  Device:Q_NMOS_GSD (SOT-23)             1 G, 2 S, 3 D
  Device:Q_NMOS_GDS (TO-220/247)         1 G, 2 D, 3 S
  Device:Q_NPN_BEC                       1 B, 2 E, 3 C
Nets: PGND = power ground (bus -, shunts), GND = hot signal ground (joined
to PGND only through NT1 at the shunt star point), GND_S = isolated SELV
ground, PE = protective earth.  +15V/+3V3/+3V3A are HOT (mains referenced).
+3V3S is SELV.
"""

PARTS = []
_sheet = [""]


def sheet(name):
    _sheet[0] = name


def P(ref, sym, value, fp, pins, mpn="", note="", names=None, dnp=False):
    """pins: {pin_number: net}; names: optional {pin_number: expected_pin_name}"""
    PARTS.append(dict(ref=ref, sym=sym, value=value, fp=fp, pins={str(k): v for k, v in pins.items()},
                      mpn=mpn, note=note, names={str(k): v for k, v in (names or {}).items()},
                      sheet=_sheet[0], dnp=dnp))


# --- footprint shortcuts -------------------------------------------------
R0603 = "Resistor_SMD:R_0603_1608Metric"
R1206 = "Resistor_SMD:R_1206_3216Metric"
R2512 = "Resistor_SMD:R_2512_6332Metric"
C0603 = "Capacitor_SMD:C_0603_1608Metric"
C0805 = "Capacitor_SMD:C_0805_2012Metric"
C1206 = "Capacitor_SMD:C_1206_3216Metric"
SOT23 = "Package_TO_SOT_SMD:SOT-23"
SOD123 = "Diode_SMD:D_SOD-123"
SMA = "Diode_SMD:D_SMA"
SMB = "Diode_SMD:D_SMB"
DO41 = "Diode_THT:D_DO-41_SOD81_P10.16mm_Horizontal"
DO201 = "Diode_THT:D_DO-201AD_P15.24mm_Horizontal"
LED0805 = "LED_SMD:LED_0805_2012Metric"


def R(ref, val, a, b, fp=R0603, note=""):
    P(ref, "Device:R", val, fp, {1: a, 2: b}, note=note)


def C(ref, val, a, b, fp=C0603, note=""):
    P(ref, "Device:C", val, fp, {1: a, 2: b}, note=note)


def CP(ref, val, plus, minus, fp, mpn="", note=""):
    P(ref, "Device:C_Polarized", val, fp, {1: plus, 2: minus}, mpn=mpn, note=note)


def D(ref, val, k, a, fp=SOD123, sym="Device:D", mpn=""):
    P(ref, sym, val, fp, {1: k, 2: a}, mpn=mpn or val)


def NMOS_SMD(ref, val, g, s, d):
    P(ref, "Transistor_FET:2N7002", val, SOT23, {1: g, 2: s, 3: d}, mpn=val, names={1: "G", 2: "S", 3: "D"})


# ========================================================================
sheet("1 AC input, protection, relays, mains sensing")
# ========================================================================
P("J1", "Connector:Screw_Terminal_01x03", "AC IN L/N/PE",
  "TerminalBlock_Phoenix:TerminalBlock_Phoenix_MKDS-1,5-3-5.08_1x03_P5.08mm_Horizontal",
  {1: "AC_L", 2: "AC_N", 3: "PE"}, mpn="Phoenix MKDS 1,5/3-5,08")
P("F1", "Device:Fuse", "10A T 250VAC", "Fuse:Fuseholder_Cylinder-5x20mm_Bulgin_FX0457_Horizontal_Closed",
  {1: "AC_L", 2: "AC_LF0"}, mpn="5x20 ceramic 10 A time-lag")
P("RV1", "Device:Varistor", "S14K385", "Varistor:RV_Disc_D15.5mm_W11mm_P7.5mm",
  {1: "AC_LF0", 2: "AC_N"}, mpn="EPCOS S14K385", note="385 VAC: survives 380-440 V neutral loss")
P("RV2", "Device:Varistor", "S14K385", "Varistor:RV_Disc_D15.5mm_W11mm_P7.5mm", {1: "AC_LF0", 2: "SURGE_PE"})
P("RV3", "Device:Varistor", "S14K385", "Varistor:RV_Disc_D15.5mm_W11mm_P7.5mm", {1: "AC_N", 2: "SURGE_PE"})
P("GDT1", "Device:GDT_2Pin", "GDT 600V", "Custom:GDT_Axial_P15.24mm", {1: "SURGE_PE", 3: "PE"},
  mpn="axial 2-electrode GDT 600 V (e.g. Bourns 2026-60 / EPCOS EC600X)")
P("CX1", "Device:C", "470nF X2", "Capacitor_THT:C_Rect_L18.0mm_W7.0mm_P15.00mm_FKS3_FKP3",
  {1: "AC_LF0", 2: "AC_N"}, mpn="X2 310VAC")
R("R1", "470k", "AC_LF0", "XDIS", R1206, "X-cap discharge")
R("R2", "470k", "XDIS", "AC_N", R1206, "X-cap discharge")
P("L1", "Device:Filter_EMI_CommonMode", "2x4.7mH 10A",
  "Custom:CMC_Toroid_4P_25x15mm",
  {1: "AC_LF0", 2: "AC_LF", 3: "AC_N", 4: "AC_NF"}, mpn="toroidal CM choke 2 x 4.7 mH, >= 10 A (e.g. Schaffner RN series); verify pin map")
P("CX2", "Device:C", "470nF X2", "Capacitor_THT:C_Rect_L18.0mm_W7.0mm_P15.00mm_FKS3_FKP3", {1: "AC_LF", 2: "AC_NF"})
P("CY1", "Device:C", "2.2nF Y2", "Capacitor_THT:C_Disc_D11.0mm_W5.0mm_P7.50mm", {1: "AC_LF", 2: "PE"}, mpn="Y2 300VAC")
P("CY2", "Device:C", "2.2nF Y2", "Capacitor_THT:C_Disc_D11.0mm_W5.0mm_P7.50mm", {1: "AC_NF", 2: "PE"}, mpn="Y2 300VAC")

# K1 main relay, K2 pre-charge relay with PTC (bus never connected unless Vin is OK)
P("K1", "Relay:FINDER-40.11", "Finder 40.11.9.012", "Relay_THT:Relay_SPDT_Finder_40.11",
  {"A1": "K1_A1", "A2": "K1_DRV", "11": "AC_LF", "14": "L_SW", "12": "NC_K1"},
  mpn="Finder 40.11.9.012.0000 (10 A, 12 V coil)", note="opens on Vin > 280 V within one half cycle")
P("K2", "Relay:FINDER-34.51", "Finder 34.51.7.012", "Relay_THT:Relay_SPDT_Finder_34.51_Vertical",
  {"A1": "K2_A1", "A2": "K2_DRV", "11": "AC_LF", "14": "PRE_L", "12": "NC_K2"}, mpn="Finder 34.51.7.012.0010")
P("PTC1", "Device:Thermistor_PTC", "PTC 120R inrush", "Capacitor_THT:C_Disc_D11.0mm_W5.0mm_P7.50mm",
  {1: "PRE_L", 2: "L_SW"}, mpn="EPCOS B59xxxJ120 series (energy-rated for 660 uF @ 373 V)",
  note="self-protecting if the pre-charge path stays closed into a fault")

# Auxiliary rectifier BEFORE the relays (the controller must run while K1/K2 are open)
D("D1", "1N4007", "AUX_RAW", "AC_LF", DO41)
D("D2", "1N4007", "AUX_RAW", "AC_NF", DO41)
D("D3", "1N4007", "AC_LF", "PGND", DO41)       # completes the bridge with BR1(N) when K1 is open
P("C1", "Device:C", "100nF 630V", "Capacitor_THT:C_Rect_L18.0mm_W6.0mm_P15.00mm_FKS3_FKP3", {1: "AUX_RAW", 2: "PGND"})

# Mains sensing (both lines vs ground, 1:301) -> |Vin| waveform, rms, zero cross, outage
for i, (src, net) in enumerate([("AC_LF", "VIN_L"), ("AC_NF", "VIN_N")]):
    base = 3 + 4 * i
    R(f"R{base}", "1M", src, f"{net}_A", R1206, "1206 200 V each, 3 in series")
    R(f"R{base+1}", "1M", f"{net}_A", f"{net}_B", R1206)
    R(f"R{base+2}", "1M", f"{net}_B", f"{net}_C", R1206)
    R(f"R{base+3}", "10k 0.1%", f"{net}_C", "GND")
    P(f"D{4+i}", "Diode:BAT54S", "BAT54S", SOT23, {1: "GND", 2: "+3V3A", 3: f"{net}_C"},
      names={3: "COM"}, mpn="BAT54S")
    C(f"C{2+i}", "1nF", f"{net}_C", "GND")
    R(f"R{11+i}", "1k", f"{net}_C", net)

# ========================================================================
sheet("2 Rectifier and PFC (CCM, NCP1653, 390 V)")
# ========================================================================
P("BR1", "Device:D_Bridge_+AA-", "GBU1010", "Diode_THT:Diode_Bridge_Vishay_GBU",
  {1: "RECT_P", 2: "L_SW", 3: "AC_NF", 4: "PFC_RTN"}, mpn="GBU1010 (10 A 1000 V) on heatsink")
P("C4", "Device:C", "1uF 630V", "Capacitor_THT:C_Rect_L26.5mm_W8.5mm_P22.50mm_MKS4", {1: "RECT_P", 2: "PFC_RTN"})
P("L2", "Device:L", "470uH 13A", "Inductor_THT:L_Toroid_Vertical_L46.0mm_W19.1mm_P21.80mm_Bourns_5700",
  {1: "RECT_P", 2: "SW_PFC"}, mpn="toroid ~46 mm OD (e.g. Kool Mu 0077083A7 or High Flux 58083), 470 uH at 11 A, Isat >= 13 A")
P("D6", "Device:D", "SiC 650V 8A", "Package_TO_SOT_THT:TO-220-2_Vertical", {1: "VBUS", 2: "SW_PFC"},
  mpn="Wolfspeed C3D08065A / Infineon IDH08G65C6", note="TO-220-2: pad 1 = K (verify part)")
P("D7", "Device:D", "P600M", DO201, {1: "VBUS", 2: "RECT_P"}, mpn="P600M / 1N5408 inrush bypass")
P("Q1", "Device:Q_NMOS_GDS", "650V 95mOhm SJ", "Package_TO_SOT_THT:TO-247-3_Vertical",
  {1: "PFC_G", 2: "SW_PFC", 3: "PGND"}, names={1: "G", 2: "D", 3: "S"}, mpn="Infineon IPW65R095C7 / ST STW40N60M6 (heatsink, insulated)")
R("R13", "10R", "PFC_DRV", "PFC_G", R0603)
R("R14", "10k", "PFC_G", "PGND", R0603)
D("D8", "BZT52C18", "PFC_G", "PGND", SOD123, sym="Device:D_Zener")
# current sense shunt in the rectifier return (NCP1653 senses a negative voltage)
R("R15", "50m 3W", "PFC_RTN", "PGND", R2512, "2 x 50 mOhm in parallel -> 25 mOhm")
R("R16", "50m 3W", "PFC_RTN", "PGND", R2512)
P("U1", "Regulator_Controller:NCP1653", "NCP1653", "Package_SO:SOIC-8_3.9x4.9mm_P1.27mm",
  {1: "PFC_FB", 2: "PFC_VCTRL", 3: "PFC_IN", 4: "PFC_CS", 5: "PFC_VM", 6: "PGND", 7: "PFC_DRV", 8: "PFC_VCC"},
  names={1: "FB/SD", 2: "VCONTROL", 3: "IN", 4: "CS", 5: "VM", 6: "GND", 7: "DRV", 8: "VCC"},
  mpn="onsemi NCP1653ADR2G (67 kHz)",
  note="controller passives = SEED values, confirm with the onsemi NCP1653 design worksheet")
R("R17", "10R", "+15V", "PFC_VCC", R0603)
CP("C5", "22uF 25V", "PFC_VCC", "PGND", "Capacitor_THT:CP_Radial_D8.0mm_P3.50mm")
C("C6", "100nF", "PFC_VCC", "PGND")
for k in range(4):   # output voltage feedback (current into FB), 4 x 470k = 1.88 M
    R(f"R{18+k}", "470k 1%", "VBUS" if k == 0 else f"PFC_FBD{k}", "PFC_FB" if k == 3 else f"PFC_FBD{k+1}", R1206)
C("C7", "1nF", "PFC_FB", "PGND")
for k in range(4):   # line voltage sense into IN, 4 x 1 M
    R(f"R{22+k}", "1M", "RECT_P" if k == 0 else f"PFC_IND{k}", "PFC_IN" if k == 3 else f"PFC_IND{k+1}", R1206)
C("C8", "1nF", "PFC_IN", "PGND")
R("R26", "4.7k", "PFC_RTN", "PFC_CS", R0603, "CS gain (verify)")
C("C9", "100pF", "PFC_CS", "PGND")
R("R27", "22k", "PFC_VM", "PGND", R0603, "multiplier output (verify)")
C("C10", "220pF", "PFC_VM", "PGND")
C("C11", "1uF", "PFC_VCTRL", "PGND", C0805, "voltage-loop compensation (verify)")
NMOS_SMD("Q2", "2N7002", "PFC_OFF", "PGND", "PFC_FB")      # MCU pulls FB/SD low -> PFC off
R("R28", "100k", "PFC_OFF", "GND")
# DC bus: 3 x Nichicon 1200 uF / 200 V in series (owner stock, reformed and tested before use)
#   = 400 uF / 600 V, hold-up ~21 ms at 1.2 kW, 100 Hz ripple ~25 V p-p.  Balancing 68k 1 W per cap,
#   lower mid-point measured by the MCU (VBM_SENSE) -> alarm on imbalance (aged / leaky capacitor).
SNAP35 = "Capacitor_THT:CP_Radial_D35.0mm_P10.00mm_SnapIn"
for k, (hi, lo) in enumerate((("VBUS", "VB_M2"), ("VB_M2", "VB_M1"), ("VB_M1", "PGND"))):
    ref = ("C12", "C13", "C61")[k]
    CP(ref, "1200uF 200V", hi, lo, SNAP35, mpn="Nichicon LS/LQ 1200 uF 200 V snap-in (3 in series)",
       note="series stack; reform 2 h through 1k before first use, check ESR/leakage")
    R(f"R{100+k}", "68k 1W", hi, lo, "Resistor_THT:R_Axial_DIN0414_L11.9mm_D4.5mm_P15.24mm_Horizontal",
      "balancing resistor, one per capacitor")
for k in range(3):   # bleeder + HV LED
    R(f"R{29+k}", "150k", "VBUS" if k == 0 else f"BLD{k}", f"BLD{k+1}", R1206)
P("LED1", "Device:LED", "red HV", LED0805, {1: "PGND", 2: "BLD3"}, mpn="high-efficiency red, 'HIGH VOLTAGE'")
# lower mid-point sense 1:101 (VB_M1 nominal 130 V -> 1.29 V; 200 V -> 1.98 V)
R("R103", "330k 1%", "VB_M1", "VBM1D", R1206)
R("R104", "330k 1%", "VBM1D", "VBM2D", R1206)
R("R105", "6.65k 0.1%", "VBM2D", "GND")
C("C62", "1nF", "VBM2D", "GND")
R("R106", "1k", "VBM2D", "VBM_SENSE")

# ========================================================================
sheet("3 Auxiliary supply (VIPer26 flyback, 1500 V pre-regulator)")
# ========================================================================
# source follower clamps the flyback input to ~395 V so 440 VAC (622 V peak) is survived
P("Q3", "Device:Q_NMOS_GDS", "1500V 4A", "Package_TO_SOT_THT:TO-247-3_Vertical",
  {1: "PRE_G", 2: "AUX_RAW", 3: "AUX_HV"}, names={1: "G", 2: "D", 3: "S"}, mpn="ST STW4N150 (on main heatsink, insulated)")
for k in range(4):
    R(f"R{32+k}", "1M", "AUX_RAW" if k == 0 else f"PREG{k}", "PRE_G" if k == 3 else f"PREG{k+1}", R1206)
for k in range(4):   # 4 x 100 V zener = 400 V gate clamp
    P(f"D{9+k}", "Device:D_Zener", "1N4764A 100V", DO41,
      {1: "PRE_G" if k == 0 else f"PREZ{k}", 2: f"PREZ{k+1}" if k < 3 else "PGND"}, mpn="1N4764A")
D("D13", "BZT52C15", "PRE_G", "AUX_HV", SOD123, sym="Device:D_Zener")   # gate-source protection
CP("C14", "10uF 450V", "AUX_HV", "PGND", "Capacitor_THT:CP_Radial_D10.0mm_P5.00mm")
P("T1", "Custom:FLYBACK_EF20", "EF20 flyback", "Transformer_THT:Transformer_Myrra_EF20_7408x",
  {1: "AUX_HV", 2: "VIP_D", 4: "T_H15", 5: "PGND", 6: "GND_S", 7: "T_S5", 8: "T_S31"},
  mpn="custom, see calc/transformer spec", note="pins 1-5 hot side, 6-10 SELV side: reinforced insulation")
P("U2", "Regulator_Switching:VIPer26LN", "VIPer26LN", "Package_DIP:DIP-8-N6_W7.62mm",
  {1: "PGND", 2: "VIP_VDD", 3: "VIP_LIM", 4: "VIP_FB", 5: "VIP_COMP", 7: "VIP_D", 8: "VIP_D"},
  names={1: "GND", 2: "VDD", 3: "LIM", 4: "FB", 5: "COMP", 7: "DRAIN", 8: "DRAIN"}, mpn="ST VIPer26LN (60 kHz)")
D("D14", "UF4007", "P6K_A", "VIP_D", DO41)       # zener clamp: UF4007 + P6KE200A
P("D15", "Device:D_TVS", "P6KE200CA", DO41, {1: "AUX_HV", 2: "P6K_A"}, mpn="P6KE200CA (bidirectional, 200 V clamp)")
D("D16", "ES1D", "+15V", "T_H15", SMA)
CP("C15", "220uF 35V", "+15V", "PGND", "Capacitor_THT:CP_Radial_D8.0mm_P3.50mm")
D("D17", "1N4148W", "VIP_VDD", "+15V", SOD123)
CP("C16", "10uF 35V", "VIP_VDD", "PGND", "Capacitor_THT:CP_Radial_D8.0mm_P3.50mm")
R("R36", "36k 1%", "+15V", "VIP_FB")            # 3.3 V ref x (1 + 36/10) = 15.2 V
R("R37", "10k 1%", "VIP_FB", "PGND")
R("R38", "2.2k", "VIP_COMP", "VIP_CZ")
C("C17", "100nF", "VIP_CZ", "PGND")
C("C18", "2.2nF", "VIP_COMP", "PGND")
R("R39", "DNP", "VIP_LIM", "PGND", note="optional: lowers the current limit")
P("D18", "Device:D_TVS", "SMBJ18CA", SMB, {1: "+15V", 2: "PGND"}, mpn="SMBJ18CA")
# hot 3.3 V (MCU, isolator side 1)
P("U3", "Regulator_Switching:AP63203WU", "AP63203WU", "Package_TO_SOT_SMD:TSOT-23-6",
  {1: "+3V3", 2: "+15V", 3: "+15V", 4: "GND", 5: "BUCK_SW", 6: "BUCK_BST"},
  names={1: "FB", 2: "EN", 3: "IN", 4: "GND", 5: "SW", 6: "BST"}, mpn="Diodes AP63203WU (fixed 3.3 V)")
C("C19", "10uF 25V", "+15V", "GND", C1206)
C("C20", "100nF", "BUCK_BST", "BUCK_SW")
P("L3", "Device:L", "6.8uH 1A", "Inductor_SMD:L_Bourns_SRN6045TA", {1: "BUCK_SW", 2: "+3V3"}, mpn="Bourns SRN6045TA-6R8M")
C("C21", "22uF 10V", "+3V3", "GND", C1206)
P("FB1", "Device:L", "600R@100MHz", "Inductor_SMD:L_0603_1608Metric", {1: "+3V3", 2: "+3V3A"}, mpn="ferrite bead")
C("C22", "1uF", "+3V3A", "GND")
# SELV: 5 V winding -> 3.3 V (PHY MCU, RS-485, isolator side 2); 31 V winding -> panel bus
D("D19", "SS34", "V5S", "T_S5", SMA, sym="Device:D_Schottky")
CP("C23", "220uF 16V", "V5S", "GND_S", "Capacitor_THT:CP_Radial_D8.0mm_P3.50mm")
P("U4", "Regulator_Linear:AP2112K-3.3", "AP2112K-3.3", "Package_TO_SOT_SMD:SOT-23-5",
  {1: "V5S", 2: "GND_S", 3: "V5S", 5: "+3V3S"}, names={1: "VIN", 2: "GND", 3: "EN", 5: "VOUT"},
  mpn="Diodes AP2112K-3.3TRG1", note="pin 4 NC")
C("C24", "1uF", "+3V3S", "GND_S")
D("D20", "ES1D", "V31S", "T_S31", SMA)
CP("C25", "470uF 50V", "V31S", "GND_S", "Capacitor_THT:CP_Radial_D10.0mm_P5.00mm")

# ========================================================================
sheet("4 Inverter (discrete: 3 x IRS2186 + 6 x TO-247 MOSFET), 3-shunt sensing")
# ========================================================================
# Repairable power stage: socketed DIP-8 drivers, MOSFETs screwed to the heatsink (insulating pads).
# IRS2186: no internal dead time / interlock -> dead time from TIM1 (>= 1 us), inputs 3.3 V compatible
# (VIH 2.5 V), both inputs active HIGH (firmware IPM_LIN_ACTIVE_LOW = 0).
DRV = {"U": "U5", "V": "U12", "W": "U13"}
FET = "Package_TO_SOT_THT:TO-247-3_Vertical"
for ph, k in (("U", 0), ("V", 1), ("W", 2)):
    P(DRV[ph], "Driver_FET:IRS2186", "IRS2186PBF", "Package_DIP:DIP-8_W7.62mm_Socket",
      {1: f"HIN_{ph}", 2: f"LIN_{ph}", 3: "PGND", 4: f"LO_{ph}", 5: "+15V", 6: f"OUT{ph}", 7: f"HO_{ph}", 8: f"VBOOT{ph}"},
      names={1: "HIN", 2: "LIN", 3: "COM", 4: "LO", 5: "VCC", 6: "VS", 7: "HO", 8: "VB"},
      mpn="Infineon IRS2186PBF (DIP-8 in socket)", note="COM to the shunt star (PGND), 4 A / 4 A gate drive")
    qh, ql = f"Q{9+2*k}", f"Q{10+2*k}"
    P(qh, "Device:Q_NMOS_GDS", "600V 18A", FET, {1: f"GH_{ph}", 2: "VBUS", 3: f"OUT{ph}"},
      names={1: "G", 2: "D", 3: "S"}, mpn="ST STW24N60DM2 (fast body diode) / alt. STGW20H60DF IGBT",
      note="high side, on heatsink with insulating pad")
    P(ql, "Device:Q_NMOS_GDS", "600V 18A", FET, {1: f"GL_{ph}", 2: f"OUT{ph}", 3: f"SH_{ph}"},
      names={1: "G", 2: "D", 3: "S"}, mpn="ST STW24N60DM2 (fast body diode) / alt. STGW20H60DF IGBT",
      note="low side, source to its own shunt")
    # gate drive: 10R on, G-S 10k; bootstrap 10R + UF4007 + 4.7 uF/100 nF
    R(f"R{107+4*k}", "10R", f"HO_{ph}", f"GH_{ph}", R1206, "gate resistor (tune for dv/dt <= 10 V/ns)")
    R(f"R{108+4*k}", "10k", f"GH_{ph}", f"OUT{ph}", R0603)
    R(f"R{109+4*k}", "10R", f"LO_{ph}", f"GL_{ph}", R1206, "gate resistor")
    R(f"R{110+4*k}", "10k", f"GL_{ph}", f"SH_{ph}", R0603)
    R(f"R{119+k}", "10R", "+15V", f"BST_{ph}", R1206, "bootstrap charge current limit")
    D(f"D{26+k}", "UF4007", f"VBOOT{ph}", f"BST_{ph}", DO41, mpn="UF4007 (1000 V, 75 ns)")
    C(f"C{28+2*k}", "4.7uF 25V X7R", f"VBOOT{ph}", f"OUT{ph}", C1206, "bootstrap")
    C(f"C{29+2*k}", "100nF", f"VBOOT{ph}", f"OUT{ph}")
    C(f"C{63+2*k}", "10uF 25V", "+15V", "PGND", C1206, "driver VCC, at the socket")
    C(f"C{64+2*k}", "100nF", "+15V", "PGND")
C("C26", "10uF 25V", "+15V", "GND", C1206)
C("C27", "100nF", "+15V", "GND")
P("C34", "Device:C", "1uF 630V", "Capacitor_THT:C_Rect_L26.5mm_W8.5mm_P22.50mm_MKS4", {1: "VBUS", 2: "PGND"},
  note="bus film cap, across the three legs")
for k, ref in enumerate(("C35", "C40", "C36")):   # one snubber per leg, between high-side drain and shunt ground
    P(ref, "Device:C", "100nF 630V", "Capacitor_THT:C_Rect_L18.0mm_W6.0mm_P15.00mm_FKS3_FKP3", {1: "VBUS", 2: "PGND"},
      note=f"leg {'UVW'[k]} decoupling, < 15 mm from the MOSFETs")
# gate inputs: series 100R near MCU, pull-downs near the drivers (both inputs active HIGH)
for ph, k in (("U", 0), ("V", 1), ("W", 2)):
    R(f"R{40+4*k}", "100R", f"PWM_{ph}H", f"HIN_{ph}")
    R(f"R{41+4*k}", "10k", f"HIN_{ph}", "GND")
    R(f"R{42+4*k}", "100R", f"PWM_{ph}L", f"LIN_{ph}")
    R(f"R{43+4*k}", "10k", f"LIN_{ph}", "GND")
# heatsink thermal switch (NC bimetal 90 C) -> TIM1_BKIN, high = trip (open switch or unplugged = safe)
R("R52", "10k", "+3V3", "HS_TSW")
C("C69", "10nF", "HS_TSW", "GND")
R("R53", "1k", "HS_TSW", "MCU_BKIN")
P("J10", "Connector_Generic:Conn_01x02", "TSW 90C NC", "Connector_PinHeader_2.54mm:PinHeader_1x02_P2.54mm_Vertical",
  {1: "HS_TSW", 2: "GND"}, mpn="KSD301 90 C normally closed, on heatsink")
# over-current: OPAMP outputs -> COMP1/2/4 -> TIM1_BKIN2 (internal, ~45 A peak, no external parts)
# shunts in the low-side sources + level shift (offset 0.3 V, gain 0.909) to the OPAMP/COMP inputs
for ph, k in (("U", 0), ("V", 1), ("W", 2)):
    R(f"R{54+k}", "25m 2W", f"SH_{ph}", "PGND", R2512, "kelvin to PGND star")
    R(f"R{57+k}", "1k 0.1%", f"SH_{ph}", f"ISENSE_{ph}")
    R(f"R{60+k}", "10k 0.1%", f"ISENSE_{ph}", "+3V3A")
    C(f"C{37+k}", "470pF C0G", f"ISENSE_{ph}", "GND")
P("NT1", "Device:NetTie_2", "STAR", "NetTie:NetTie-2_SMD_Pad0.5mm", {1: "PGND", 2: "GND"},
  note="single connection of signal ground to power ground, at the shunt star point")
P("J2", "Connector:Screw_Terminal_01x04", "MOTOR U/V/W/PE",
  "TerminalBlock_Phoenix:TerminalBlock_Phoenix_MKDS-1,5-4-5.08_1x04_P5.08mm_Horizontal",
  {1: "OUTU", 2: "OUTV", 3: "OUTW", 4: "PE"})
# DC bus voltage sense 1:150 (445 V -> 2.97 V)
for k in range(3):
    R(f"R{66+k}", "330k 1%", "VBUS" if k == 0 else f"VBD{k}", f"VBD{k+1}", R1206)
R("R69", "6.65k 0.1%", "VBD3", "GND")
C("C41", "1nF", "VBD3", "GND")
R("R70", "1k", "VBD3", "VBUS_SENSE")
# heatsink NTC (screw-lug, on the heatsink between the MOSFETs)
P("J3", "Connector_Generic:Conn_01x02", "NTC 10k B3435", "Connector_PinHeader_2.54mm:PinHeader_1x02_P2.54mm_Vertical",
  {1: "NTC_HS", 2: "GND"}, mpn="screw-lug NTC 10k B3435 on heatsink")
R("R71", "10k 1%", "+3V3A", "NTC_HS")
C("C42", "100nF", "NTC_HS", "GND")

# ========================================================================
sheet("5 MCU STM32G431CBT6 (hot side)")
# ========================================================================
MCU = {
    1: ("VBAT", "+3V3"), 2: ("PC13", "NC_PC13"), 3: ("PC14", "NC_PC14"), 4: ("PC15", "NC_PC15"),
    5: ("PF0", "NC_PF0"), 6: ("PF1", "NC_PF1"), 7: ("PG10", "NRST"),
    8: ("PA0", "VBUS_SENSE"), 9: ("PA1", "ISENSE_U"), 10: ("PA2", "VIN_L"), 11: ("PA3", "VIN_N"),
    12: ("PA4", "TP_DAC"), 13: ("PA5", "VBM_SENSE"), 14: ("PA6", "NTC_HS"), 15: ("PA7", "ISENSE_V"),
    16: ("PB0", "ISENSE_W"), 17: ("PB1", "NC_PB1"), 18: ("PB2", "NC_PB2"), 19: ("VSSA", "GND"),
    20: ("VREF+", "+3V3A"), 21: ("VDDA", "+3V3A"), 22: ("PB10", "K3_ON"), 23: ("VSS", "GND"), 24: ("VDD", "+3V3"),
    25: ("PB11", "K4_ON"), 26: ("PB12", "MCU_BKIN"), 27: ("PB13", "PWM_UL"), 28: ("PB14", "PWM_VL"),
    29: ("PB15", "PWM_WL"), 30: ("PA8", "PWM_UH"), 31: ("PA9", "PWM_VH"), 32: ("PA10", "PWM_WH"),
    33: ("PA11", "PFC_OFF"), 34: ("PA12", "LED_STAT"), 35: ("VSS", "GND"), 36: ("VDD", "+3V3"),
    37: ("PA13", "SWDIO"), 38: ("PA14", "SWCLK"), 39: ("PA15", "NC_PA15"), 40: ("PB3", "SWO"),
    41: ("PB4", "K1_ON"), 42: ("PB5", "K2_ON"), 43: ("PB6", "UART_H_TX"), 44: ("PB7", "UART_H_RX"),
    45: ("PB8", "BOOT0"), 46: ("PB9", "NC_PB9"), 47: ("VSS", "GND"), 48: ("VDD", "+3V3"),
}
P("U6", "MCU_ST_STM32G4:STM32G431CBTx", "STM32G431CBT6", "Package_QFP:LQFP-48_7x7mm_P0.5mm",
  {k: v[1] for k, v in MCU.items()}, names={k: v[0] for k, v in MCU.items()}, mpn="ST STM32G431CBT6")
# required alternate functions (validated against ST open pin data)
MCU_FUNCS = {
    "PA0": "ADC1_IN1", "PA5": "ADC2_IN13", "PA1": "OPAMP1_VINP", "PA2": "ADC1_IN3", "PA3": "ADC1_IN4", "PA6": "ADC2_IN3",
    "PA7": "OPAMP2_VINP", "PB0": "OPAMP3_VINP", "PA4": "DAC1_OUT1",
    "PA8": "TIM1_CH1", "PA9": "TIM1_CH2", "PA10": "TIM1_CH3", "PB13": "TIM1_CH1N", "PB14": "TIM1_CH2N",
    "PB15": "TIM1_CH3N", "PB12": "TIM1_BKIN", "PB6": "USART1_TX", "PB7": "USART1_RX",
    "PA13": "SYS_JTMS-SWDIO", "PA14": "SYS_JTCK-SWCLK", "PB3": "SYS_JTDO-SWO",
}
MCU_FUNCS_EXTRA = {"PA1": "COMP1_INP", "PA7": "COMP2_INP", "PB0": "COMP4_INP"}   # per-phase over-current
for k in (24, 36, 48):
    C(f"C{43+(k-24)//12}", "100nF", "+3V3", "GND")
C("C46", "4.7uF", "+3V3", "GND", C0805)
C("C47", "1uF", "+3V3A", "GND")
C("C48", "100nF", "+3V3A", "GND")
C("C49", "100nF", "NRST", "GND")
R("R72", "10k", "BOOT0", "GND")
R("R73", "1k", "LED_STAT", "LED2_A")
P("LED2", "Device:LED", "green", LED0805, {1: "GND", 2: "LED2_A"})
P("TP1", "Connector:TestPoint", "DAC", "TestPoint:TestPoint_Pad_D1.5mm", {1: "TP_DAC"})
P("J4", "Connector_Generic:Conn_02x05_Odd_Even", "SWD (HOT!)", "Connector_PinHeader_1.27mm:PinHeader_2x05_P1.27mm_Vertical",
  {1: "+3V3", 2: "SWDIO", 3: "GND", 4: "SWCLK", 5: "GND", 6: "SWO", 7: "NC_J4_7", 8: "NC_J4_8", 9: "GND", 10: "NRST"},
  note="mains referenced: isolated debugger or isolation transformer only")
# PFC enable: Q2 on sheet 2 (PFC_OFF high -> FB/SD low -> PFC off)

# ========================================================================
sheet("6 Relay drivers, pump and drain outputs (220 VAC)")
# ========================================================================
coil = {"K1": ("68R 0.5W", R1206), "K2": ("220R", R0603), "K3": ("220R", R0603), "K4": ("220R", R0603)}
for i, k in enumerate(("K1", "K2", "K3", "K4")):
    q = f"Q{4+i}"
    R(f"R{74+3*i}", coil[k][0], "+15V", f"{k}_A1", coil[k][1], "drops 15 V to the 12 V coil (verify coil R)")
    NMOS_SMD(q, "2N7002", f"{k}_G", "GND", f"{k}_DRV")
    R(f"R{75+3*i}", "100R", f"{k}_ON", f"{k}_G")
    R(f"R{76+3*i}", "100k", f"{k}_G", "GND")
    D(f"D{21+i}", "1N4148W", f"{k}_A1", f"{k}_DRV", SOD123)
P("F2", "Device:Fuse", "2A T", "Fuse:Fuseholder_Cylinder-5x20mm_Bulgin_FX0457_Horizontal_Closed",
  {1: "L_SW", 2: "L_LOAD"}, mpn="5x20 2 A time-lag", note="pump + drain branch")
P("K3", "Relay:FINDER-34.51", "Finder 34.51.7.012", "Relay_THT:Relay_SPDT_Finder_34.51_Vertical",
  {"A1": "K3_A1", "A2": "K3_DRV", "11": "L_LOAD", "14": "PUMP_L", "12": "NC_K3"}, mpn="Finder 34.51.7.012.0010",
  note="water pump 220 VAC")
P("K4", "Relay:FINDER-34.51", "Finder 34.51.7.012", "Relay_THT:Relay_SPDT_Finder_34.51_Vertical",
  {"A1": "K4_A1", "A2": "K4_DRV", "11": "L_LOAD", "14": "DRAIN_L", "12": "NC_K4"}, mpn="Finder 34.51.7.012.0010",
  note="drain valve 220 VAC")
P("J5", "Connector:Screw_Terminal_01x04", "PUMP L/N DRAIN L/N",
  "TerminalBlock_Phoenix:TerminalBlock_Phoenix_MKDS-1,5-4-5.08_1x04_P5.08mm_Horizontal",
  {1: "PUMP_L", 2: "AC_NF", 3: "DRAIN_L", 4: "AC_NF"})
for i, (n, l) in enumerate((("PUMP", "PUMP_L"), ("DRAIN", "DRAIN_L"))):     # snubbers across the loads (DNP)
    P(f"C{50+i}", "Device:C", "47nF X2", "Capacitor_THT:C_Rect_L18.0mm_W6.0mm_P15.00mm_FKS3_FKP3",
      {1: l, 2: f"{n}_SN"}, dnp=True, note="optional RC snubber")
    R(f"R{86+i}", "100R 1W", f"{n}_SN", "AC_NF", R2512)

# ========================================================================
sheet("7 Isolated side: isolator, PHY MCU, 2-wire panel bus, RS-485 to battery module")
# ========================================================================
P("U7", "Isolator:ISO7762DW", "ISO7762DW", "Package_SO:SOIC-16W_7.5x10.3mm_P1.27mm",
  {1: "+3V3", 2: "UART_H_TX", 3: "GND", 4: "GND", 5: "GND", 6: "UART_H_RX", 7: "NC_ISO_OUTF", 8: "GND",
   9: "GND_S", 10: "GND_S", 11: "UART_S_TX", 12: "NC_ISO_OUTD", 13: "NC_ISO_OUTC", 14: "NC_ISO_OUTB",
   15: "UART_S_RX", 16: "+3V3S"},
  names={1: "Vcc1", 2: "INA", 6: "OUTE", 8: "GND1", 9: "GND2", 11: "INE", 15: "OUTA", 16: "Vcc2"},
  mpn="TI ISO7762DWR (reinforced 5 kVrms, SOIC-16 wide)",
  note="ch A hot->SELV, ch E SELV->hot; unused inputs grounded; slot under the package, 8 mm creepage")
C("C52", "100nF", "+3V3", "GND")
C("C53", "100nF", "+3V3S", "GND_S")
PHY = {1: ("PB7", "UART_S_RX"), 2: ("PC14", "NC_S14"), 3: ("PC15", "NC_S15"), 4: ("VDD", "+3V3S"), 5: ("VSS", "GND_S"),
       6: ("PF2", "NRST_S"), 7: ("PA0", "LED_S"), 8: ("PA1", "RS485_DE"), 9: ("PA2", "RS485_DI"), 10: ("PA3", "RS485_RO"),
       11: ("PA4", "PB_TX"), 12: ("PA5", "PB_ISNS"), 13: ("PA6", "PB_VSNS"), 14: ("PA7", "NC_S7"), 15: ("PA8", "NC_S8"),
       16: ("PA9", "NC_S16"), 17: ("PA10", "NC_S17"), 18: ("PA13", "SWDIO_S"), 19: ("PA14", "SWCLK_S"), 20: ("PB6", "UART_S_TX")}
P("U8", "MCU_ST_STM32C0:STM32C011F6Px", "STM32C011F6P6", "Package_SO:TSSOP-20_4.4x6.5mm_P0.65mm",
  {k: v[1] for k, v in PHY.items()}, names={1: "PB7", 4: "VDD", 5: "VSS", 6: "PF2", 7: "PA0", 8: "PA1", 9: "PA2",
                                             10: "PA3", 11: "PA4", 12: "PA5", 13: "PA6", 18: "PA13", 19: "PA14", 20: "PB6"},
  mpn="ST STM32C011F6P6")
PHY_FUNCS = {"PB7": "USART1_RX", "PB6": "USART1_TX", "PA2": "USART2_TX", "PA3": "USART2_RX", "PA1": "USART2_DE",
             "PA5": "ADC1_IN5", "PA6": "ADC1_IN6"}
C("C54", "100nF", "+3V3S", "GND_S")
C("C55", "1uF", "+3V3S", "GND_S")
C("C56", "100nF", "NRST_S", "GND_S")
R("R88", "1k", "LED_S", "LED3_A")
P("LED3", "Device:LED", "yellow", LED0805, {1: "GND_S", 2: "LED3_A"})
P("J6", "Connector_Generic:Conn_01x05", "SWD SELV", "Connector_PinHeader_2.54mm:PinHeader_1x05_P2.54mm_Vertical",
  {1: "+3V3S", 2: "SWDIO_S", 3: "SWCLK_S", 4: "NRST_S", 5: "GND_S"})
# panel bus: current limiter (LM317 as 150 mA CC) -> regulator 24 V (mark) / 16 V (space)
P("U9", "Regulator_Linear:LM317_TO-220", "LM317 CC", "Package_TO_SOT_THT:TO-220-3_Vertical",
  {1: "PB_CC", 2: "PB_CCO", 3: "V31S"}, names={1: "ADJ", 2: "VO", 3: "VI"}, mpn="LM317T (small heatsink)")
R("R89", "8.2R 0.5W", "PB_CCO", "PB_CC", R1206, "1.25 V / 8.2 R = 150 mA short-circuit limit")
P("U10", "Regulator_Linear:LM317_TO-220", "LM317 REG", "Package_TO_SOT_THT:TO-220-3_Vertical",
  {1: "PB_ADJ", 2: "PB_REG", 3: "PB_CC"}, names={1: "ADJ", 2: "VO", 3: "VI"}, mpn="LM317T")
R("R90", "240R 1%", "PB_REG", "PB_ADJ")
R("R91", "4.3k 1%", "PB_ADJ", "GND_S", note="24 V mark")
R("R92", "8.2k 1%", "PB_ADJ", "PB_TXD", note="switched in parallel -> 16 V space")
NMOS_SMD("Q8", "2N7002", "PB_TXG", "GND_S", "PB_TXD")
R("R93", "100R", "PB_TX", "PB_TXG")
R("R94", "100k", "PB_TXG", "GND_S")
C("C57", "1uF 50V", "V31S", "GND_S", C1206)
C("C58", "10uF 35V", "PB_REG", "GND_S", C1206)
P("F3", "Device:Polyfuse", "0.1A hold", "Fuse:Fuse_1812_4532Metric", {1: "PB_REG", 2: "PANEL_P"}, mpn="Bourns MF-MSMF010")
P("D25", "Device:D_TVS", "SMBJ33CA", SMB, {1: "PANEL_P", 2: "PANEL_N"}, mpn="SMBJ33CA")
R("R95", "4.7R 1%", "PANEL_N", "GND_S", R1206, "return current sense")
R("R96", "1k", "PANEL_N", "PB_ISNS")
C("C59", "10nF", "PB_ISNS", "GND_S")
R("R97", "100k 1%", "PANEL_P", "PB_VSNS")
R("R98", "10k 1%", "PB_VSNS", "GND_S")
P("J7", "Connector:Screw_Terminal_01x02", "PANEL 2-WIRE", "TerminalBlock_Phoenix:TerminalBlock_Phoenix_MKDS-1,5-2-5.08_1x02_P5.08mm_Horizontal",
  {1: "PANEL_P", 2: "PANEL_N"}, note="SELV: 24 V mark / 16 V space; panel is polarity-free")
# RS-485 to the battery module (SELV)
P("U11", "Interface_UART:SP3485EN", "SP3485EN", "Package_SO:SOIC-8_3.9x4.9mm_P1.27mm",
  {1: "RS485_RO", 2: "RS485_DE", 3: "RS485_DE", 4: "RS485_DI", 5: "GND_S", 6: "RS485_A", 7: "RS485_B", 8: "+3V3S"},
  names={1: "RO", 2: "~{RE}", 3: "DE", 4: "DI", 5: "GND", 6: "A", 7: "B", 8: "VCC"},
  mpn="MaxLinear SP3485EN (3.3 V half duplex)", dnp=True, note="OPTION battery module")
P("C60", "Device:C", "100nF", C0603, {1: "+3V3S", 2: "GND_S"}, dnp=True, note="OPTION battery module")
P("R99", "Device:R", "120R", R0603, {1: "RS485_A", 2: "RS485_T"}, dnp=True, note="OPTION battery module: termination")
P("JP1", "Jumper:SolderJumper_2_Open", "TERM", "Jumper:SolderJumper-2_P1.3mm_Open_Pad1.0x1.5mm",
  {1: "RS485_T", 2: "RS485_B"}, dnp=True, note="OPTION battery module")
P("J8", "Connector:Screw_Terminal_01x03", "BATT MODULE RS-485",
  "TerminalBlock_Phoenix:TerminalBlock_Phoenix_MKDS-1,5-3-5.08_1x03_P5.08mm_Horizontal",
  {1: "RS485_A", 2: "RS485_B", 3: "GND_S"}, dnp=True, note="OPTION battery module")

# ========================================================================
sheet("8 OPTION battery module (DNP in the mains-only version), mechanics, test points")
# ========================================================================
P("F4", "Device:Fuse", "5A 500VDC", "Fuse:Fuseholder_Cylinder-5x20mm_Bulgin_FX0457_Horizontal_Closed",
  {1: "VBUS", 2: "VBUS_EXT"}, mpn="5x20 ceramic, DC rated >= 500 V (e.g. Littelfuse 505 series)", dnp=True,
  note="OPTION battery module")
P("J9", "Connector:Screw_Terminal_01x03", "BATT MODULE DC BUS",
  "TerminalBlock_Phoenix:TerminalBlock_Phoenix_MKDS-1,5-3-5.08_1x03_P5.08mm_Horizontal",
  {1: "VBUS_EXT", 2: "NC_J9", 3: "PGND"}, dnp=True,
  note="OPTION battery module; middle position empty for creepage; module does its own pre-charge")

# --- mechanics and production aids (all versions) ---------------------------
for i, (x) in enumerate(range(4)):
    P(f"H{i+1}", "Mechanical:MountingHole", "M3", "MountingHole:MountingHole_3.2mm_M3", {},
      note="non-plated: no creepage path from mounting screws")
for i in range(3):
    P(f"FID{i+1}", "Mechanical:Fiducial", "FID", "Fiducial:Fiducial_1mm_Mask2mm", {})
for i, n in enumerate(["+15V", "+3V3", "GND", "PFC_DRV", "ISENSE_U", "UART_H_TX", "+3V3S", "GND_S", "PANEL_P"]):
    P(f"TP{i+2}", "Connector:TestPoint", n, "TestPoint:TestPoint_Pad_D1.5mm", {1: n})
# supply flags (schematic ERC only, no footprint)
