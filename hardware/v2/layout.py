"""layout.py - board-specific settings of the v2 drive for gen_kicad.py
(placement, regions, board outline, creepage slot, net classes, DRC rules)."""

PROJECT = "bldc_cooler_v2"
TITLE = "BLDC cooler motor drive v2 (220 VAC, PFC, sensorless FOC)"
COMMENTS = ["Generated from hardware/v2/schematic.py - validated against KiCad 7.0.11 libs and ST pin data",
            "DANGER: mains-referenced except the SELV area (panel bus, RS-485)"]
REV, DATE = "2.0", "2026-11-06"
PCB_TITLE = "BLDC cooler drive v2 - placement"
BOARD = (20.0, 20.0, 250.0, 180.0)          # x0, y0, width, height [mm]
SLOT_X = 216.0                              # vertical creepage slot (hot | SELV), runs under T1 and U7
SLOT_Y = 84.0                               # horizontal part of the SELV boundary


def fixed(BX0, BY0, BX1, BY1):
    HS_Y = BY1 - 7.0        # heatsink row (vertical TO-247/TO-220/GBU, tabs towards the bottom edge)
    return {
        "J1": (27, 40, 270), "F1": (45, 28, 0), "L1": (40, 62, 0),
        "K1": (34, 128, 0), "K2": (74, 128, 0),
        "BR1": (40, HS_Y, 0), "Q3": (66, HS_Y, 0), "Q1": (90, HS_Y, 0), "D6": (112, HS_Y, 0),
        "L2": (130, 48, 0), "C12": (118, 140, 0), "C13": (160, 140, 0),
        # discrete inverter: 6 x TO-247 on the heatsink, each driver socket right above its leg
        **{f"Q{9+i}": (128 + 17.5 * i, HS_Y, 0) for i in range(6)},
        "U5": (136, 166, 0), "U12": (168, 166, 0), "U13": (203, 166, 0),
        "J9": (104, 112, 0), "F4": (122, 112, 0),
        "J2": (238, 180, 0),
        "T1": (SLOT_X - 7.6, 40, 0), "U7": (SLOT_X, 68, 0),
        "U9": (232, 26, 0), "U10": (248, 26, 0), "J7": (263, 40, 90), "J8": (263, 64, 90),
        "J5": (263, 112, 90), "K3": (226, 112, 0), "K4": (226, 128, 0), "F2": (224, 96, 0),
        "H1": (BX0 + 5, BY0 + 5, 0), "H2": (BX1 - 5, BY0 + 5, 0), "H3": (BX0 + 5, BY1 - 5, 0), "H4": (BX1 - 5, BY1 - 5, 0),
        "FID1": (BX0 + 12, BY0 + 4, 0), "FID2": (BX1 - 14, BY1 - 4, 0), "FID3": (BX0 + 12, BY1 - 4, 0),
    }


REGIONS = {
    "ac":   (24, 22, 96, 150),
    "pfc":  (96, 76, 166, 124),
    "aux":  (166, 22, SLOT_X - 11, 84),
    "inv":  (124, 159, 234, 190),
    "mcu":  (SLOT_X + 2, 88, 254, 158),
    "selv": (SLOT_X + 4, 36, 256, 80),
}


def region_of(p):
    s = p["sheet"][0]
    ref = p["ref"]
    if s == "1": return "ac"
    if s == "2": return "pfc"
    if s == "3":
        return "selv" if ref in ("D19", "C23", "U4", "C24", "D20", "C25") else "aux"
    if s == "4": return "inv"
    if s in ("5", "6"): return "mcu"
    if s == "7": return "mcu" if ref in ("C52",) else "selv"
    if s == "8":
        if ref.startswith("TP"):
            n = p["pins"]["1"]
            return "selv" if n in ("+3V3S", "GND_S", "PANEL_P") else "mcu"
        return "pfc"
    return "mcu"


HV_CLASS = "HV_mains"
HV_CLEARANCE = 1.5
HV_PATS = ["AC_*", "L_SW", "L_LOAD", "PRE_L", "PUMP_L", "DRAIN_L", "PUMP_SN", "DRAIN_SN", "RECT_P", "SW_PFC",
           "VBUS", "VBUS_EXT", "AUX_RAW", "AUX_HV", "MOT_*", "OUTU", "OUTV", "OUTW", "VBOOT?", "VIP_D", "P6K_A",
           "PRE_G", "XDIS", "SURGE_PE", "VIN_?_A", "VIN_?_B", "BLD1", "BLD2", "BLD3", "VBD1", "VBD2",
           "PFC_FBD?", "PFC_IND?", "PREG?", "PREZ?", "HO_?", "GH_?"]
POWER_GND_PATS = ("PGND", "SH_*")

# package-internal pin spacing is set by the manufacturer
DRU_PITCH = [("U5", 0.5), ("U12", 0.5), ("U13", 0.5), *((f"Q{9+i}", 1.0) for i in range(6)), ("U2", 0.6),
             ("T1", 1.0), ("Q1", 1.0), ("Q3", 1.0), ("D6", 1.0), ("BR1", 1.0),
             ("U1", 0.25), ("U6", 0.15), ("U7", 0.2), ("K1", 1.0), ("K2", 1.0), ("K3", 1.0), ("K4", 1.0),
             ("D4", 0.2), ("D5", 0.2), ("RV1", 1.0), ("CX1", 1.0), ("L1", 1.0),
             # only volts across these parts although both nets float at bus potential
             ("R107", 0.2), ("R108", 0.2), ("R111", 0.2), ("R112", 0.2), ("R115", 0.2), ("R116", 0.2),
             ("C28", 0.2), ("C29", 0.2), ("C30", 0.2), ("C31", 0.2), ("C32", 0.2), ("C33", 0.2), ("LED1", 0.2)]
DRU_EXTRA = ['(rule "SELV to hot" (condition "A.NetClass == \'HV_mains\' && B.Net == \'GND_S\'") (constraint clearance (min 8mm)))']


def graphics(g, BX0, BY0, BX1, BY1):
    """board graphics besides the outline. g: dict with line, text, poly helpers."""
    line, text, poly = g["line"], g["text"], g["poly"]
    SW = 1.0
    out = [poly("slot", [(SLOT_X - SW, BY0 + 6), (SLOT_X + SW, BY0 + 6), (SLOT_X + SW, SLOT_Y - SW), (BX1 - 8, SLOT_Y - SW),
                         (BX1 - 8, SLOT_Y + SW), (SLOT_X - SW, SLOT_Y + SW)], "Edge.Cuts")]
    out += [line(SLOT_X - 4, BY0, SLOT_X - 4, SLOT_Y + 4, "Dwgs.User", 0.15),
            line(SLOT_X - 4, SLOT_Y + 4, BX1, SLOT_Y + 4, "Dwgs.User", 0.15),
            text("8 mm creepage: no copper within 4 mm of the slot (except T1 / U7 pads)", 150, SLOT_Y + 8, "Dwgs.User", 1.2)]
    out += [text("HOT - MAINS POTENTIAL", 80, 22.5, "F.SilkS", 2.0), text("SELV", 245, 22.5, "F.SilkS", 2.5),
            text("HEATSINK EDGE", 100, BY1 - 1.5, "F.SilkS", 1.5),
            text("BATTERY OPTION (DNP)", 112, 106, "F.SilkS", 1.2),
            text("Placement generated from schematic.py - routing to be done manually", 140, BY1 + 6, "Cmts.User", 1.5)]
    return out
