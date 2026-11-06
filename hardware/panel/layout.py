"""layout.py - board settings of the cooler panel (basic version) for hardware/v2/gen_kicad.py."""

PROJECT = "cooler_panel"
TITLE = "Cooler control panel - basic (4-digit display, keys, Wi-Fi/BLE)"
COMMENTS = ["Generated from hardware/panel/schematic.py - validated against KiCad 7.0.11 libs",
            "SELV only: powered by the 2-wire panel bus of the drive (or USB for programming)"]
REV, DATE = "1.0", "2026-11-06"
PCB_TITLE = "Cooler panel - placement (front: display, LEDs, keys)"
BOARD = (20.0, 20.0, 120.0, 86.0)          # 120 x 86 mm, fits a 135 x 100 surface-mount enclosure
PLACER = "anchor"                           # connectivity-driven placement (gen_kicad.py)
GAP = 1.0                                   # mm between courtyards: routing channels
GLOBAL_NETS = ("GND", "+3V3")
NEAR = {"C10": "U4.2", "C11": "U4.2", "C4": "U1.5", "C8": "L1.2", "C9": "U4.2", "C18": "U6.3",
        "C1": "BR1.1", "C6": "U3.3", "R39": "U6.1", "R40": "U6.2", "R6": "U1.4", "R13": "U4.3"}


def fixed(BX0, BY0, BX1, BY1):
    f = {
        "U4": (52.0, 49.0, 0),             # ESP32-S3 module, antenna towards the top edge
        "DS1": (106.0, 47.0, 90),          # display, top right
        "J1": (32.0, 100.0, 0),            # bus terminal, bottom-left edge
        "J2": (70.0, 102.0, 0),            # USB-C (service), bottom edge
        "BR1": (42.0, 91.0, 0), "U2": (52.0, 84.0, 0), "U3": (64.0, 84.0, 0), "L1": (73.0, 84.0, 0),
        "U1": (38.0, 75.0, 0), "Q1": (30.0, 84.0, 0), "U5": (70.0, 94.0, 0),
        "BZ1": (128.0, 70.0, 0), "U6": (136.0, 84.0, 0),
        "C18": (138.2, 84.0, 90),            # sensor decoupling right beside its VDD/GND pins
        "H1": (BX0 + 4, BY0 + 4, 0), "H2": (BX1 - 4, BY0 + 4, 0), "H3": (BX0 + 4, BY1 - 4, 0), "H4": (BX1 - 4, BY1 - 4, 0),
        "FID1": (BX0 + 4, BY0 + 46, 0), "FID2": (BX1 - 4, BY0 + 38, 0), "FID3": (BX1 - 4, BY1 - 14, 0),
    }
    for i in range(6):                     # status LEDs in a row under the display
        f[f"LED{i+1}"] = (90.0 + 8.0 * i, 57.0, 0)
    for i in range(5):                     # keys in a row at the bottom
        f[f"SW{i+1}"] = (80.0 + 10.5 * i, 97.0, 0)
    return f


REGIONS = {
    "bus": (22, 64, 80, 104),
    "ui":  (80, 60, 138, 90),
}


def region_of(p):
    return "ui" if p["sheet"][0] == "3" else "bus"


HV_CLASS = "HV_unused"                     # no mains on this board
HV_CLEARANCE = 0.2
HV_PATS = []
POWER_GND_PATS = ()
EXTRA_CLASSES = [
    {"name": "Power", "clearance": 0.2, "track_width": 0.5, "via_diameter": 0.8, "via_drill": 0.4,
     "patterns": ["BUS1", "BUS1F", "BUS2", "BUSR", "CC_OUT", "VSTORE", "+3V3", "GND", "USB_VBUS", "BUCK_SW", "TX_C", "TX_E"]},
]
DRU_PITCH = [("U4", 0.15)]
DRU_EXTRA = ['(rule "USB-C locating pegs" (condition "A.insideCourtyard(\'J2\')") (constraint hole_clearance (min 0.1mm)))',
             # GND pads are connected by tracks as well; one thermal spoke to the pour is enough
             '(rule "pour spokes" (condition "A.Type == \'Zone\'") (constraint min_resolved_spokes 1))']
# the ESP32-S3 module footprint has 0.2 mm thermal vias in its ground pad
BOARD_RULES = {"min_through_hole_diameter": 0.2, "min_hole_clearance": 0.2}


def graphics(g, BX0, BY0, BX1, BY1):
    line, text = g["line"], g["text"]
    return [line(28, BY0, 76, BY0 + 0.01, "Dwgs.User", 0.15),
            text("ESP32 antenna: no copper, no parts within 15 mm above the module (all layers)", 52, BY0 - 3, "Dwgs.User", 1.0),
            text("SELV - COOLER PANEL", 108, 24, "F.SilkS", 1.5),
            text("Placement generated from schematic.py - SMD parts may move to the back side when routing", 72, BY1 + 5, "Cmts.User", 1.2)]
