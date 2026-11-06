"""layout.py - board settings of the cooler panel (basic version) for hardware/v2/gen_kicad.py."""

PROJECT = "cooler_panel"
TITLE = "Cooler control panel - basic (4-digit display, keys, Wi-Fi/BLE)"
COMMENTS = ["Generated from hardware/panel/schematic.py - validated against KiCad 7.0.11 libs",
            "SELV only: powered by the 2-wire panel bus of the drive (or USB for programming)"]
REV, DATE = "1.0", "2026-11-06"
PCB_TITLE = "Cooler panel - placement (front: display, LEDs, keys)"
BOARD = (20.0, 20.0, 112.0, 80.0)          # 112 x 80 mm, fits a 125 x 90 surface-mount enclosure


def fixed(BX0, BY0, BX1, BY1):
    f = {
        "U4": (52.0, 49.0, 0),             # ESP32-S3 module, antenna towards the top edge
        "DS1": (96.0, 47.0, 90),           # display, top right
        "J1": (32.0, 92.0, 0),             # bus terminal, bottom-left edge
        "J2": (60.0, 95.0, 0),             # USB-C (service), bottom edge
        "H1": (BX0 + 4, BY0 + 4, 0), "H2": (BX1 - 4, BY0 + 4, 0), "H3": (BX0 + 4, BY1 - 4, 0), "H4": (BX1 - 4, BY1 - 4, 0),
        "FID1": (BX0 + 4, BY0 + 46, 0), "FID2": (BX1 - 4, BY0 + 46, 0), "FID3": (BX1 - 4, BY1 - 14, 0),
    }
    for i in range(6):                     # status LEDs in a row under the display
        f[f"LED{i+1}"] = (83.0 + 8.0 * i, 55.0, 0)
    for i in range(5):                     # keys in a row at the bottom
        f[f"SW{i+1}"] = (73.0 + 10.0 * i, 90.0, 0)
    return f


REGIONS = {
    "bus": (22, 64, 71, 98),               # bus interface, power, USB, module support parts
    "ui":  (78, 58, 130, 86),              # display drivers, segment resistors, buzzer, sensor
}


def region_of(p):
    return "ui" if p["sheet"][0] == "3" else "bus"


HV_CLASS = "HV_unused"                     # no mains on this board
HV_CLEARANCE = 0.2
HV_PATS = []
POWER_GND_PATS = ()
DRU_PITCH = [("U4", 0.15)]
DRU_EXTRA = ['(rule "USB-C locating pegs" (condition "A.insideCourtyard(\'J2\')") (constraint hole_clearance (min 0.1mm)))']
# the ESP32-S3 module footprint has 0.2 mm thermal vias in its ground pad
BOARD_RULES = {"min_through_hole_diameter": 0.2, "min_hole_clearance": 0.2}


def graphics(g, BX0, BY0, BX1, BY1):
    line, text = g["line"], g["text"]
    return [line(28, BY0, 76, BY0 + 0.01, "Dwgs.User", 0.15),
            text("ESP32 antenna: no copper, no parts within 15 mm above the module (all layers)", 52, BY0 - 3, "Dwgs.User", 1.0),
            text("SELV - COOLER PANEL", 100, 23, "F.SilkS", 1.5),
            text("Placement generated from schematic.py - SMD parts may move to the back side when routing", 72, BY1 + 5, "Cmts.User", 1.2)]
