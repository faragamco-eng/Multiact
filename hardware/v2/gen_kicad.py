#!/usr/bin/env python3
"""
gen_kicad.py - generate a complete KiCad 7 project from schematic.py:
  out/kicad/bldc_cooler_v2.kicad_pro       project (net classes: HV clearance)
  out/kicad/bldc_cooler_v2.kicad_sch       root sheet (hierarchy)
  out/kicad/s<N>_*.kicad_sch               one sheet per functional block
  out/kicad/bldc_cooler_v2.kicad_pcb       board outline, creepage slot, zones,
                                           all footprints placed, nets assigned
  out/kicad/lib/                           project-local symbols/footprints
Altium Designer: File > Import Wizard > KiCad design files.

    python3 gen_kicad.py --symbols <kicad-symbols 7.0.11> --footprints <kicad-footprints 7.0.11>
"""
import argparse, collections, math, os, shutil, sys, uuid
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE); sys.path.insert(0, os.path.join(HERE, "tools"))
import schematic as S
from sexpr import Sym, dump, find, first, q
from kicadlib import SymbolLib, FootprintLib

ap = argparse.ArgumentParser()
ap.add_argument("--symbols", required=True)
ap.add_argument("--footprints", required=True)
ap.add_argument("--out", default=os.path.join(HERE, "out", "kicad"))
a = ap.parse_args()
OUT = a.out
os.makedirs(OUT, exist_ok=True)
LOCAL = os.path.join(HERE, "lib")
SL, FL = SymbolLib(a.symbols, LOCAL), FootprintLib(a.footprints, LOCAL)
PROJECT = "bldc_cooler_v2"
NS = uuid.UUID("6f1d2a3b-0000-4000-8000-00000000c001")
def U(*k): return str(uuid.uuid5(NS, "/".join(map(str, k))))
def g(v): return round(round(v / 1.27) * 1.27, 4)          # 50 mil grid

TITLE = [Sym("title_block"), [Sym("title"), "BLDC cooler motor drive v2 (220 VAC, PFC, sensorless FOC)"],
         [Sym("date"), "2026-11-06"], [Sym("rev"), "2.0"], [Sym("company"), "Multiact"],
         [Sym("comment"), 1, "Generated from hardware/v2/schematic.py - validated against KiCad 7.0.11 libs and ST pin data"],
         [Sym("comment"), 2, "DANGER: mains-referenced except the SELV area (panel bus, RS-485)"]]

# ======================================================================
# schematic
# ======================================================================
sheets = collections.OrderedDict()
for p in S.PARTS:
    sheets.setdefault(p["sheet"], []).append(p)

pins_cache = {}
def sym_pins(symname):
    if symname not in pins_cache:
        pins_cache[symname] = SL.pins(*symname.split(":"))
    return pins_cache[symname]

def font(h=1.27): return [Sym("effects"), [Sym("font"), [Sym("size"), h, h]]]

def label_len(net): return 3.0 + 1.05 * len(net)

ROOT = U("root")
nets_all = collections.defaultdict(list)
for p in S.PARTS:
    for pin, net in p["pins"].items():
        nets_all[net].append((p, pin))

# nets that need a PWR_FLAG (power_in pins, no power_out pin)
pin_type = {}
for p in S.PARTS:
    for x in sym_pins(p["sym"]):
        pin_type[(p["ref"], x["number"])] = x["type"]
need_flag = []
for net, nodes in nets_all.items():
    types = {pin_type.get((p["ref"], pin)) for p, pin in nodes}
    if "power_in" in types and "power_out" not in types:
        need_flag.append(net)

def sheet_file(i, name):
    slug = "".join(c if c.isalnum() else "_" for c in name.lower())[:30].strip("_")
    return f"s{i}_{slug}.kicad_sch"

pwr_flag_sym = SL.flat("power", "PWR_FLAG")
flags_by_sheet = collections.defaultdict(list)
for net in need_flag:          # put the flag on the sheet where the net first appears
    first_sheet = nets_all[net][0][0]["sheet"]
    flags_by_sheet[first_sheet].append(net)

sheet_uuids = {}
for si, (sname, parts) in enumerate(sheets.items(), 1):
    suid = U("sheet", si)
    sheet_uuids[sname] = (suid, sheet_file(si, sname), si + 1)
    items, lib_syms = [], {}
    # layout: rows of symbols
    X0, Y0, WMAX = 25.4, 30.48, 780.0
    x, y, rowh = X0, Y0, 0.0
    placed = []
    for p in parts:
        lib, name = p["sym"].split(":")
        if p["sym"] not in lib_syms:
            lib_syms[p["sym"]] = SL.flat(lib, name)
        pins = sym_pins(p["sym"])
        units = SL.units(lib, name)
        for unit in units:
            up = [x_ for x_ in pins if x_["unit"] in (0, unit)]
            if not up and unit != units[0]:
                continue
            xs = [pp["x"] for pp in up] or [0]; ys = [-pp["y"] for pp in up] or [0]
            # label room on each side
            left = max([label_len(p["pins"].get(pp["number"], "")) + 2.54 for pp in up if math.cos(math.radians(pp["angle"])) > 0.5] or [0])
            right = max([label_len(p["pins"].get(pp["number"], "")) + 2.54 for pp in up if math.cos(math.radians(pp["angle"])) < -0.5] or [0])
            up_ = max([4.0 for pp in up if math.sin(math.radians(pp["angle"])) < -0.5] or [0])
            down = max([4.0 for pp in up if math.sin(math.radians(pp["angle"])) > 0.5] or [0])
            vlab = max([label_len(p["pins"].get(pp["number"], "")) for pp in up if abs(math.sin(math.radians(pp["angle"]))) > 0.5] or [0])
            w = (max(xs) - min(xs)) + left + right + 12 + (vlab if vlab else 0) * 0.2
            h = (max(ys) - min(ys)) + 10 + (vlab if vlab else 0) + up_ + down
            if x + w > WMAX:
                x, y, rowh = X0, y + rowh + 7.62, 0.0
            sx = g(x + left - min(xs) + 2.54)
            sy = g(y + 5.08 + (vlab if any(math.sin(math.radians(pp["angle"])) < -0.5 for pp in up) else 0) - min(ys))
            x += w + 7.62
            rowh = max(rowh, h)
            ref = p["ref"]
            vertical_only = all(abs(math.sin(math.radians(pp["angle"]))) > 0.5 for pp in up) and len(up) <= 3
            if vertical_only:      # 2-pin parts drawn vertically: texts to the right of the body
                rpos = [Sym("at"), g(sx + 2.54), g(sy - 1.27), 0]; vpos = [Sym("at"), g(sx + 2.54), g(sy + 1.27), 0]
                tfont = [Sym("effects"), [Sym("font"), [Sym("size"), 1.27, 1.27]], [Sym("justify"), Sym("left")]]
            else:
                rpos = [Sym("at"), sx, g(sy + min(ys) - 2.54), 0]; vpos = [Sym("at"), sx, g(sy + max(ys) + 2.54), 0]
                tfont = font()
            props = [
                [Sym("property"), "Reference", ref, rpos, tfont],
                [Sym("property"), "Value", p["value"], vpos, tfont],
                [Sym("property"), "Footprint", p["fp"], [Sym("at"), sx, sy, 0], font() + [Sym("hide")]],
                [Sym("property"), "Datasheet", "", [Sym("at"), sx, sy, 0], font() + [Sym("hide")]],
                [Sym("property"), "MPN", p["mpn"], [Sym("at"), sx, sy, 0], font() + [Sym("hide")]],
            ]
            el = [Sym("symbol"), [Sym("lib_id"), p["sym"]], [Sym("at"), sx, sy, 0], [Sym("unit"), unit],
                  [Sym("in_bom"), Sym("yes")], [Sym("on_board"), Sym("yes")], [Sym("dnp"), Sym("yes" if p["dnp"] else "no")],
                  [Sym("uuid"), U("sym", ref, unit)]] + props
            for pp in up:
                el.append([Sym("pin"), pp["number"], [Sym("uuid"), U("pin", ref, pp["number"], unit)]])
            el.append([Sym("instances"), [Sym("project"), PROJECT,
                       [Sym("path"), f"/{ROOT}/{suid}", [Sym("reference"), ref], [Sym("unit"), unit]]]])
            items.append(el)
            # stubs + labels / no-connects
            done = set()
            for pp in up:
                if pp["number"] in done and pp["type"] == "power_in":
                    pass
                done.add(pp["number"])
                px, py = g(sx + pp["x"]), g(sy - pp["y"])
                net = p["pins"].get(pp["number"])
                if net is None or net.startswith("NC_"):
                    items.append([Sym("no_connect"), [Sym("at"), px, py], [Sym("uuid"), U("nc", ref, pp["number"], unit)]])
                    continue
                ang = pp["angle"]
                dx, dy = -math.cos(math.radians(ang)), math.sin(math.radians(ang))
                ex, ey = g(px + 2.54 * dx), g(py + 2.54 * dy)
                items.append([Sym("wire"), [Sym("pts"), [Sym("xy"), px, py], [Sym("xy"), ex, ey]],
                              [Sym("stroke"), [Sym("width"), 0], [Sym("type"), Sym("default")]],
                              [Sym("uuid"), U("w", ref, pp["number"], unit)]])
                if abs(dx) > 0.5:
                    la, just = (0, "left") if dx > 0 else (180, "right")
                else:
                    la, just = (90, "left") if dy < 0 else (270, "right")
                items.append([Sym("global_label"), net, [Sym("shape"), Sym("passive")], [Sym("at"), ex, ey, la],
                              [Sym("fields_autoplaced")],
                              [Sym("effects"), [Sym("font"), [Sym("size"), 1.27, 1.27]], [Sym("justify"), Sym(just)]],
                              [Sym("uuid"), U("gl", ref, pp["number"], unit)],
                              [Sym("property"), "Intersheetrefs", "${INTERSHEET_REFS}", [Sym("at"), ex, ey, 0],
                               [Sym("effects"), [Sym("font"), [Sym("size"), 1.27, 1.27]], Sym("hide")]]])
    # power flags
    if flags_by_sheet.get(sname):
        lib_syms["power:PWR_FLAG"] = pwr_flag_sym
        y += rowh + 15
        fx = X0
        for k, net in enumerate(flags_by_sheet[sname]):
            fx_ = g(fx + 2.54); fy = g(y + 6)
            refn = f"#FLG0{si}{k:02d}"
            items.append([Sym("symbol"), [Sym("lib_id"), "power:PWR_FLAG"], [Sym("at"), fx_, fy, 0], [Sym("unit"), 1],
                          [Sym("in_bom"), Sym("yes")], [Sym("on_board"), Sym("yes")], [Sym("dnp"), Sym("no")],
                          [Sym("uuid"), U("flag", net)],
                          [Sym("property"), "Reference", refn, [Sym("at"), fx_, g(fy - 6), 0], font() + [Sym("hide")]],
                          [Sym("property"), "Value", "PWR_FLAG", [Sym("at"), fx_, g(fy - 3.8), 0], font()],
                          [Sym("property"), "Footprint", "", [Sym("at"), fx_, fy, 0], font() + [Sym("hide")]],
                          [Sym("property"), "Datasheet", "", [Sym("at"), fx_, fy, 0], font() + [Sym("hide")]],
                          [Sym("pin"), "1", [Sym("uuid"), U("flagpin", net)]],
                          [Sym("instances"), [Sym("project"), PROJECT, [Sym("path"), f"/{ROOT}/{suid}",
                           [Sym("reference"), refn], [Sym("unit"), 1]]]]])
            items.append([Sym("wire"), [Sym("pts"), [Sym("xy"), fx_, fy], [Sym("xy"), fx_, g(fy + 2.54)]],
                          [Sym("stroke"), [Sym("width"), 0], [Sym("type"), Sym("default")]], [Sym("uuid"), U("fw", net)]])
            items.append([Sym("global_label"), net, [Sym("shape"), Sym("passive")], [Sym("at"), fx_, g(fy + 2.54), 270],
                          [Sym("fields_autoplaced")], [Sym("effects"), [Sym("font"), [Sym("size"), 1.27, 1.27]], [Sym("justify"), Sym("right")]],
                          [Sym("uuid"), U("fgl", net)],
                          [Sym("property"), "Intersheetrefs", "${INTERSHEET_REFS}", [Sym("at"), fx_, fy, 0],
                           [Sym("effects"), [Sym("font"), [Sym("size"), 1.27, 1.27]], Sym("hide")]]])
            fx += 12.7
    paper = "A1" if y < 560 else "A0"
    doc = [Sym("kicad_sch"), [Sym("version"), 20230121], [Sym("generator"), Sym("eeschema")],
           [Sym("uuid"), suid], [Sym("paper"), paper],
           TITLE[:1] + [[Sym("title"), f"{sname}"]] + TITLE[2:],
           [Sym("lib_symbols")] + list(lib_syms.values())] + items
    open(os.path.join(OUT, sheet_file(si, sname)), "w").write(dump(doc) + "\n")

# root sheet with sheet symbols
root_items = []
for k, (sname, (suid, fname, page)) in enumerate(sheet_uuids.items()):
    x, y = 25.4 + (k % 3) * 95.0, 40.0 + (k // 3) * 35.0
    root_items.append([Sym("sheet"), [Sym("at"), g(x), g(y)], [Sym("size"), 76.2, 20.32], [Sym("fields_autoplaced")],
                       [Sym("stroke"), [Sym("width"), 0.1524], [Sym("type"), Sym("solid")]],
                       [Sym("fill"), [Sym("color"), 0, 0, 0, 0.0]], [Sym("uuid"), suid],
                       [Sym("property"), "Sheetname", sname, [Sym("at"), g(x), g(y) - 0.7, 0],
                        [Sym("effects"), [Sym("font"), [Sym("size"), 1.27, 1.27]], [Sym("justify"), Sym("left"), Sym("bottom")]]],
                       [Sym("property"), "Sheetfile", fname, [Sym("at"), g(x), g(y) + 20.9, 0],
                        [Sym("effects"), [Sym("font"), [Sym("size"), 1.27, 1.27]], [Sym("justify"), Sym("left"), Sym("top")]]],
                       [Sym("instances"), [Sym("project"), PROJECT, [Sym("path"), f"/{ROOT}", [Sym("page"), str(page)]]]]])
root = [Sym("kicad_sch"), [Sym("version"), 20230121], [Sym("generator"), Sym("eeschema")], [Sym("uuid"), ROOT],
        [Sym("paper"), "A3"], TITLE, [Sym("lib_symbols")]] + root_items + \
       [[Sym("sheet_instances"), [Sym("path"), "/", [Sym("page"), "1"]]]]
open(os.path.join(OUT, f"{PROJECT}.kicad_sch"), "w").write(dump(root) + "\n")

# ======================================================================
# PCB
# ======================================================================
BX0, BY0, BW, BH = 20.0, 20.0, 250.0, 180.0
BX1, BY1 = BX0 + BW, BY0 + BH
SLOT_X = 216.0          # vertical creepage slot (hot | SELV), runs under T1 and U7
SLOT_Y = 84.0           # horizontal part of the SELV boundary
netcode = {"": 0}
for net in sorted(nets_all):
    netcode[net] = len(netcode)
parts_by_ref = {p["ref"]: p for p in S.PARTS}

HS_Y = BY1 - 7.0        # heatsink row (vertical TO-247/TO-220/GBU, tabs towards the bottom edge)
FIXED = {
    "J1": (27, 40, 270), "F1": (45, 28, 0), "L1": (40, 62, 0),
    "K1": (34, 128, 0), "K2": (74, 128, 0),
    "BR1": (40, HS_Y, 0), "Q3": (66, HS_Y, 0), "Q1": (90, HS_Y, 0), "D6": (112, HS_Y, 0),
    "L2": (130, 48, 0), "C12": (110, 140, 0), "C13": (147, 140, 0), "C61": (184, 140, 0),
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

def courtyard(fp, rot):
    x0, y0, x1, y1 = FL.bbox(fp)
    pts = [(x0, y0), (x1, y0), (x1, y1), (x0, y1)]
    c, s_ = math.cos(math.radians(rot)), math.sin(math.radians(rot))
    rp = [(px * c + py * s_, -px * s_ + py * c) for px, py in pts]      # KiCad: +rot is CCW, y down
    return (min(p[0] for p in rp), min(p[1] for p in rp), max(p[0] for p in rp), max(p[1] for p in rp))

import fnmatch
HV_NETS = None
HV_PATS = ["AC_*", "L_SW", "L_LOAD", "PRE_L", "PUMP_L", "DRAIN_L", "PUMP_SN", "DRAIN_SN", "RECT_P", "SW_PFC",
           "VBUS", "VBUS_EXT", "AUX_RAW", "AUX_HV", "MOT_*", "OUTU", "OUTV", "OUTW", "VBOOT?", "VIP_D", "P6K_A",
           "PRE_G", "XDIS", "SURGE_PE", "VIN_?_A", "VIN_?_B", "BLD1", "BLD2", "BLD3", "VBD1", "VBD2",
           "PFC_FBD?", "PFC_IND?", "PREG?", "PREZ?", "VB_M?", "VBM1D", "HO_?", "GH_?"]
HV_NETS = sorted(n for n in nets_all if any(fnmatch.fnmatch(n, pt) for pt in HV_PATS))
def is_hv(p):
    return any(n in HV_NETS for n in p["pins"].values())
place, overflow = {}, []
occupied = []
for ref, (x, y, r) in FIXED.items():
    if ref in parts_by_ref:
        place[ref] = (x, y, r)
        cx0, cy0, cx1, cy1 = courtyard(parts_by_ref[ref]["fp"], r)
        occupied.append((x + cx0, y + cy0, x + cx1, y + cy1))

def free(rect, m=0.6):
    for o in occupied:
        if not (rect[2] + m <= o[0] or rect[0] - m >= o[2] or rect[3] + m <= o[1] or rect[1] - m >= o[3]):
            return False
    return True

by_region = collections.defaultdict(list)
for p in S.PARTS:
    if p["ref"] not in place:
        by_region[region_of(p)].append(p)
for reg, plist in by_region.items():
    rx0, ry0, rx1, ry1 = REGIONS[reg]
    plist.sort(key=lambda p: -(lambda b: (b[2] - b[0]) * (b[3] - b[1]))(courtyard(p["fp"], 0)))
    for p in plist:
        cx0, cy0, cx1, cy1 = courtyard(p["fp"], 0)
        done = False
        yy = ry0
        while yy + (cy1 - cy0) <= ry1 and not done:
            xx = rx0
            while xx + (cx1 - cx0) <= rx1:
                rect = (xx, yy, xx + cx1 - cx0, yy + cy1 - cy0)
                m = 1.8 if is_hv(p) else 0.6
                if free(rect, m):
                    place[p["ref"]] = (round(xx - cx0, 3), round(yy - cy0, 3), 0)
                    occupied.append((rect[0] - (m - 0.6) / 2, rect[1] - (m - 0.6) / 2, rect[2] + (m - 0.6) / 2, rect[3] + (m - 0.6) / 2))
                    done = True
                    break
                xx += 0.5
            yy += 0.5
        if not done:
            overflow.append((p["ref"], reg))

# build footprints
def rotate_angles(e, rot):
    for x in e:
        if isinstance(x, list) and x and x[0] in ("pad", "fp_text"):
            at = first(x, "at")
            if at is not None:
                ang = float(at[3]) if len(at) > 3 and not isinstance(at[3], Sym) else 0.0
                rest = at[4:] if len(at) > 4 else ([at[3]] if len(at) > 3 and isinstance(at[3], Sym) else [])
                at[:] = [at[0], at[1], at[2], (ang + rot) % 360] + rest

fps = []
for p in S.PARTS:
    if p["ref"] not in place:
        continue
    x, y, r = place[p["ref"]]
    f = FL.get(p["fp"])
    out = [Sym("footprint"), p["fp"]]
    for e in f[2:]:
        if isinstance(e, list) and e and e[0] in ("version", "generator"):
            continue
        out.append(e)
    # insert position after layer
    li = next(i for i, e in enumerate(out) if isinstance(e, list) and e and e[0] == "layer")
    out.insert(li + 1, [Sym("tstamp"), U("fp", p["ref"])])
    out.insert(li + 2, [Sym("at"), x, y, r] if r else [Sym("at"), x, y])
    for e in out:
        if isinstance(e, list) and e and e[0] == "fp_text":
            if e[1] == "reference": e[2] = p["ref"]
            if e[1] == "value": e[2] = p["value"]
        if isinstance(e, list) and e and e[0] == "pad":
            num = str(e[1])
            net = p["pins"].get(num)
            if net and not net.startswith("NC_"):
                e.append([Sym("net"), netcode[net], net])
    if p["dnp"]:
        out.append([Sym("attr"), Sym("through_hole") if "THT" in p["fp"] or "Terminal" in p["fp"] else Sym("smd"),
                    Sym("exclude_from_bom")])
    if r: rotate_angles(out, r)
    # unique ids for every item inside the footprint (library copies share them -> KIID collisions)
    cnt = [0]
    def reid(e):
        for x in e:
            if isinstance(x, list) and x:
                if x[0] in ("tstamp", "uuid") and len(x) == 2 and x is not out[li + 1]:
                    cnt[0] += 1
                    x[1] = U("fpitem", p["ref"], cnt[0])
                else:
                    reid(x)
    reid(out)
    fps.append(out)

# board outline, slot, silkscreen zone marks
def line(x0, y0, x1, y1, layer, w=0.1):
    return [Sym("gr_line"), [Sym("start"), x0, y0], [Sym("end"), x1, y1],
            [Sym("stroke"), [Sym("width"), w], [Sym("type"), Sym("default")]], [Sym("layer"), layer],
            [Sym("tstamp"), U("gl", x0, y0, x1, y1, layer)]]
def text(t, x, y, layer, h=2.0):
    return [Sym("gr_text"), t, [Sym("at"), x, y], [Sym("layer"), layer], [Sym("tstamp"), U("tx", t, x, y)],
            [Sym("effects"), [Sym("font"), [Sym("size"), h, h], [Sym("thickness"), 0.3]]]]
gr = [line(BX0, BY0, BX1, BY0, "Edge.Cuts"), line(BX1, BY0, BX1, BY1, "Edge.Cuts"),
      line(BX1, BY1, BX0, BY1, "Edge.Cuts"), line(BX0, BY1, BX0, BY0, "Edge.Cuts")]
# 2 mm milled slot along the hot/SELV boundary (gaps where T1 and U7 bridge it are left as solid board
# under the parts' own insulation; slot ends 10 mm from the components)
SW = 1.0
def slot(x0, y0, x1, y1):
    return [line(x0, y0, x1, y0, "Edge.Cuts"), line(x1, y0, x1, y1, "Edge.Cuts"),
            line(x1, y1, x0, y1, "Edge.Cuts"), line(x0, y1, x0, y0, "Edge.Cuts")]
# one L-shaped milled slot (vertical under T1/U7, horizontal to near the right edge); bridges at both ends
gr.append([Sym("gr_poly"), [Sym("pts")] + [[Sym("xy"), x_, y_] for x_, y_ in (
    (SLOT_X - SW, BY0 + 6), (SLOT_X + SW, BY0 + 6), (SLOT_X + SW, SLOT_Y - SW), (BX1 - 8, SLOT_Y - SW),
    (BX1 - 8, SLOT_Y + SW), (SLOT_X - SW, SLOT_Y + SW))],
    [Sym("stroke"), [Sym("width"), 0.1], [Sym("type"), Sym("solid")]], [Sym("fill"), Sym("none")],
    [Sym("layer"), "Edge.Cuts"], [Sym("tstamp"), U("slot")]])
gr += [line(SLOT_X - 4, BY0, SLOT_X - 4, SLOT_Y + 4, "Dwgs.User", 0.15), line(SLOT_X - 4, SLOT_Y + 4, BX1, SLOT_Y + 4, "Dwgs.User", 0.15),
       text("8 mm creepage: no copper within 4 mm of the slot (except T1 / U7 pads)", 150, SLOT_Y + 8, "Dwgs.User", 1.2)]
gr += [text("HOT - MAINS POTENTIAL", 80, 22.5, "F.SilkS", 2.0), text("SELV", 245, 22.5, "F.SilkS", 2.5),
       text("HEATSINK EDGE", 100, BY1 - 1.5, "F.SilkS", 1.5),
       text("BATTERY OPTION (DNP)", 112, 106, "F.SilkS", 1.2),
       text("Placement generated from schematic.py - routing to be done manually", 140, BY1 + 6, "Cmts.User", 1.5)]

LAYERS = [Sym("layers")] + [[i, n, Sym(t)] + ([u] if u else []) for i, n, t, u in [
    (0, "F.Cu", "signal", None), (31, "B.Cu", "signal", None), (32, "B.Adhes", "user", "B.Adhesive"),
    (33, "F.Adhes", "user", "F.Adhesive"), (34, "B.Paste", "user", None), (35, "F.Paste", "user", None),
    (36, "B.SilkS", "user", "B.Silkscreen"), (37, "F.SilkS", "user", "F.Silkscreen"), (38, "B.Mask", "user", None),
    (39, "F.Mask", "user", None), (40, "Dwgs.User", "user", "User.Drawings"), (41, "Cmts.User", "user", "User.Comments"),
    (42, "Eco1.User", "user", "User.Eco1"), (43, "Eco2.User", "user", "User.Eco2"), (44, "Edge.Cuts", "user", None),
    (45, "Margin", "user", None), (46, "B.CrtYd", "user", "B.Courtyard"), (47, "F.CrtYd", "user", "F.Courtyard"),
    (48, "B.Fab", "user", None), (49, "F.Fab", "user", None)]]
pcb = [Sym("kicad_pcb"), [Sym("version"), 20221018], [Sym("generator"), Sym("pcbnew")],
       [Sym("general"), [Sym("thickness"), 1.6]], [Sym("paper"), "A3"],
       [Sym("title_block"), [Sym("title"), "BLDC cooler drive v2 - placement"], [Sym("date"), "2026-11-06"], [Sym("rev"), "2.0"]],
       LAYERS, [Sym("setup"), [Sym("pad_to_mask_clearance"), 0]]]
pcb += [[Sym("net"), c, n] for n, c in sorted(netcode.items(), key=lambda kv: kv[1])]
pcb += fps + gr
open(os.path.join(OUT, f"{PROJECT}.kicad_pcb"), "w").write(dump(pcb) + "\n")

# ======================================================================
# project file with net classes (clearance) and library tables
# ======================================================================
import json
proj = {
    "board": {"design_settings": {"rules": {"min_clearance": 0.2, "min_track_width": 0.2}}},
    "meta": {"filename": f"{PROJECT}.kicad_pro", "version": 1},
    "net_settings": {
        "classes": [
            {"name": "Default", "clearance": 0.2, "track_width": 0.25, "via_diameter": 0.8, "via_drill": 0.4,
             "diff_pair_gap": 0.25, "diff_pair_width": 0.2, "microvia_diameter": 0.3, "microvia_drill": 0.1,
             "wire_width": 6, "bus_width": 12, "line_style": 0, "schematic_color": "rgba(0, 0, 0, 0.000)",
             "pcb_color": "rgba(0, 0, 0, 0.000)"},
            {"name": "HV_mains", "clearance": 1.5, "track_width": 1.5, "via_diameter": 1.6, "via_drill": 0.8,
             "diff_pair_gap": 0.25, "diff_pair_width": 0.2, "microvia_diameter": 0.3, "microvia_drill": 0.1,
             "wire_width": 6, "bus_width": 12, "line_style": 0, "schematic_color": "rgba(194, 0, 0, 1.000)",
             "pcb_color": "rgba(194, 0, 0, 1.000)"},
            {"name": "Power_GND", "clearance": 0.4, "track_width": 2.0, "via_diameter": 1.2, "via_drill": 0.6,
             "diff_pair_gap": 0.25, "diff_pair_width": 0.2, "microvia_diameter": 0.3, "microvia_drill": 0.1,
             "wire_width": 6, "bus_width": 12, "line_style": 0, "schematic_color": "rgba(0, 0, 0, 0.000)",
             "pcb_color": "rgba(0, 0, 0, 0.000)"},
        ],
        "meta": {"version": 3},
        "netclass_patterns": [{"netclass": "HV_mains", "pattern": n} for n in HV_NETS] +
                             [{"netclass": "Power_GND", "pattern": pt} for pt in ("PGND", "SH_*")],
    },
    "pcbnew": {"last_paths": {}, "page_layout_descr_file": ""},
    "schematic": {"meta": {"version": 1}},
    "sheets": [[ROOT, "Root"]] + [[u, n] for n, (u, f, pg) in sheet_uuids.items()],
}
json.dump(proj, open(os.path.join(OUT, f"{PROJECT}.kicad_pro"), "w"), indent=2)
# custom DRC rules: package-internal pin spacing is set by the manufacturer; creepage to SELV is checked
# against the slot (see Dwgs.User note); everything else uses the net classes above.
DRU = ['(version 1)']
for ref, mn in (("U5", 0.5), ("U12", 0.5), ("U13", 0.5), *((f"Q{9+i}", 1.0) for i in range(6)), ("U2", 0.6), ("T1", 1.0), ("Q1", 1.0), ("Q3", 1.0), ("D6", 1.0), ("BR1", 1.0),
                ("U1", 0.25), ("U6", 0.15), ("U7", 0.2), ("K1", 1.0), ("K2", 1.0), ("K3", 1.0), ("K4", 1.0),
                ("D4", 0.2), ("D5", 0.2), ("RV1", 1.0), ("CX1", 1.0), ("L1", 1.0),
                # only volts across these parts although both nets float at bus potential
                ("R107", 0.2), ("R108", 0.2), ("R111", 0.2), ("R112", 0.2), ("R115", 0.2), ("R116", 0.2),
                ("C28", 0.2), ("C29", 0.2), ("C30", 0.2), ("C31", 0.2), ("C32", 0.2), ("C33", 0.2), ("LED1", 0.2)):
    DRU.append(f'(rule "package pitch {ref}" (condition "A.insideCourtyard(\'{ref}\') && B.insideCourtyard(\'{ref}\')")'
               f' (constraint clearance (min {mn}mm)))')
DRU.append('(rule "SELV to hot" (condition "A.NetClass == \'HV_mains\' && B.Net == \'GND_S\'") (constraint clearance (min 8mm)))')
open(os.path.join(OUT, f"{PROJECT}.kicad_dru"), "w").write("\n".join(DRU) + "\n")
shutil.copytree(LOCAL, os.path.join(OUT, "lib"), dirs_exist_ok=True)
open(os.path.join(OUT, "sym-lib-table"), "w").write(
    '(sym_lib_table\n  (lib (name "Custom")(type "KiCad")(uri "${KIPRJMOD}/lib/Custom.kicad_sym")(options "")(descr "project"))\n)\n')
open(os.path.join(OUT, "fp-lib-table"), "w").write(
    '(fp_lib_table\n  (lib (name "Custom")(type "KiCad")(uri "${KIPRJMOD}/lib/Custom.pretty")(options "")(descr "project"))\n)\n')

print(f"schematic sheets {len(sheet_uuids)}, power flags {len(need_flag)}, footprints placed {len(fps)}/{len(S.PARTS)}")
if overflow:
    print("NOT PLACED (region full):", overflow)
