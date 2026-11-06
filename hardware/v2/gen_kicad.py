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
import numpy as np
HERE = os.path.dirname(os.path.abspath(__file__))
ap = argparse.ArgumentParser()
ap.add_argument("--symbols", required=True)
ap.add_argument("--footprints", required=True)
ap.add_argument("--design", default=HERE, help="folder with schematic.py, layout.py and lib/")
ap.add_argument("--out")
a = ap.parse_args()
DESIGN = os.path.abspath(a.design)
sys.path.insert(0, os.path.join(HERE, "tools")); sys.path.insert(0, DESIGN)
import schematic as S
import layout as L
from sexpr import Sym, dump, find, first, q
from kicadlib import SymbolLib, FootprintLib
OUT = a.out or os.path.join(DESIGN, "out", "kicad")
os.makedirs(OUT, exist_ok=True)
LOCAL = os.path.join(DESIGN, "lib")
SL, FL = SymbolLib(a.symbols, LOCAL), FootprintLib(a.footprints, LOCAL if os.path.isdir(LOCAL) else None)
PROJECT = L.PROJECT
NS = uuid.UUID("6f1d2a3b-0000-4000-8000-00000000c001")
def U(*k): return str(uuid.uuid5(NS, "/".join(map(str, k))))
def g(v): return round(round(v / 1.27) * 1.27, 4)          # 50 mil grid

TITLE = [Sym("title_block"), [Sym("title"), L.TITLE], [Sym("date"), L.DATE], [Sym("rev"), L.REV],
         [Sym("company"), "Multiact"]] + [[Sym("comment"), i + 1, c] for i, c in enumerate(L.COMMENTS)]

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
BX0, BY0, BW, BH = L.BOARD
BX1, BY1 = BX0 + BW, BY0 + BH
netcode = {"": 0}
for net in sorted(nets_all):
    netcode[net] = len(netcode)
parts_by_ref = {p["ref"]: p for p in S.PARTS}
FIXED = L.fixed(BX0, BY0, BX1, BY1)
REGIONS = L.REGIONS
region_of = L.region_of

def courtyard(fp, rot):
    x0, y0, x1, y1 = FL.bbox(fp)
    pts = [(x0, y0), (x1, y0), (x1, y1), (x0, y1)]
    c, s_ = math.cos(math.radians(rot)), math.sin(math.radians(rot))
    rp = [(px * c + py * s_, -px * s_ + py * c) for px, py in pts]      # KiCad: +rot is CCW, y down
    return (min(p[0] for p in rp), min(p[1] for p in rp), max(p[0] for p in rp), max(p[1] for p in rp))

import fnmatch
HV_PATS = L.HV_PATS
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

def pad_offsets(fp, rot):
    """pad number -> (x, y) relative to the footprint origin after rotation"""
    f = FL.get(fp)
    c, s_ = math.cos(math.radians(rot)), math.sin(math.radians(rot))
    out = {}
    for pd in find(f, "pad"):
        at = first(pd, "at")
        px, py = float(at[1]), float(at[2])
        out.setdefault(str(pd[1]), (px * c + py * s_, -px * s_ + py * c))
    return out

if getattr(L, "PLACER", "grid") == "anchor":
    # connectivity-driven placement: each part goes as close as possible to the pads it connects to
    GAP = getattr(L, "GAP", 1.0)
    GLOBAL = set(getattr(L, "GLOBAL_NETS", ("GND",)))
    NEAR = getattr(L, "NEAR", {})
    pin_xy = {}                                       # (ref, pin) -> absolute xy of placed parts
    def note_placed(ref):
        x, y, r = place[ref]
        for pin, (dx, dy) in pad_offsets(parts_by_ref[ref]["fp"], r).items():
            pin_xy[(ref, pin)] = (x + dx, y + dy)
    for ref in list(place): note_placed(ref)
    def anchors(p):
        pts = []
        for pin, net in p["pins"].items():
            if net in GLOBAL or net.startswith("NC_"): continue
            for q, qpin in nets_all[net]:
                if q["ref"] != p["ref"] and (q["ref"], qpin) in pin_xy:
                    pts.append((pin, pin_xy[(q["ref"], qpin)]))
        if p["ref"] in NEAR:
            r_, pn = NEAR[p["ref"]].split(".")
            if (r_, pn) in pin_xy:
                pts += [(pin, pin_xy[(r_, pn)]) for pin in p["pins"]]
        return pts
    todo = [p for p in S.PARTS if p["ref"] not in place]
    while todo:
        # the part with the most connections to already placed parts goes next
        todo.sort(key=lambda p: -len(anchors(p)))
        p = todo.pop(0)
        an = anchors(p)
        if not an:
            rx0, ry0, rx1, ry1 = REGIONS[region_of(p)]
            an = [(pin, ((rx0 + rx1) / 2, (ry0 + ry1) / 2)) for pin in p["pins"]] or [("", ((rx0 + rx1) / 2, (ry0 + ry1) / 2))]
        tx = sum(a_[1][0] for a_ in an) / len(an); ty = sum(a_[1][1] for a_ in an) / len(an)
        best = None
        for rot in ((0, 90) if len(p["pins"]) <= 3 else (0,)):
            cx0, cy0, cx1, cy1 = courtyard(p["fp"], rot)
            offs = pad_offsets(p["fp"], rot)
            for rad in np.arange(0.0, 40.0, 0.5):
                found = False
                n_ang = max(1, int(2 * math.pi * rad / 0.5))
                for k in range(n_ang):
                    x = round(tx + rad * math.cos(2 * math.pi * k / n_ang), 2)
                    y = round(ty + rad * math.sin(2 * math.pi * k / n_ang), 2)
                    rect = (x + cx0, y + cy0, x + cx1, y + cy1)
                    if rect[0] < BX0 + 1 or rect[1] < BY0 + 1 or rect[2] > BX1 - 1 or rect[3] > BY1 - 1: continue
                    if not free(rect, GAP): continue
                    cost = sum(math.hypot(x + offs.get(pin, (0, 0))[0] - ax, y + offs.get(pin, (0, 0))[1] - ay) for pin, (ax, ay) in an)
                    if best is None or cost < best[0]: best = (cost, x, y, rot, rect)
                    found = True
                if found: break
        if best is None:
            overflow.append((p["ref"], "board")); continue
        _, x, y, rot, rect = best
        place[p["ref"]] = (x, y, rot)
        occupied.append(rect)
        note_placed(p["ref"])
else:
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
                def num(v):
                    try: return float(v)
                    except (TypeError, ValueError): return None
                has_ang = len(at) > 3 and num(at[3]) is not None      # numbers may be parsed as symbols
                ang = num(at[3]) if has_ang else 0.0
                rest = at[4:] if has_ang else at[3:]
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
    # zones inside footprints (e.g. antenna keep-outs) are stored in BOARD coordinates in a .kicad_pcb
    c_, s__ = math.cos(math.radians(r)), math.sin(math.radians(r))
    for z in [e for e in out if isinstance(e, list) and e and e[0] == "zone"]:
        for pol in find(z, "polygon"):
            for pts in find(pol, "pts"):
                for xy_ in find(pts, "xy"):
                    px, py = float(xy_[1]), float(xy_[2])
                    xy_[1] = round(x + px * c_ + py * s__, 4)
                    xy_[2] = round(y - px * s__ + py * c_, 4)
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
def poly(key, pts, layer):
    return [Sym("gr_poly"), [Sym("pts")] + [[Sym("xy"), x_, y_] for x_, y_ in pts],
            [Sym("stroke"), [Sym("width"), 0.1], [Sym("type"), Sym("solid")]], [Sym("fill"), Sym("none")],
            [Sym("layer"), layer], [Sym("tstamp"), U(key)]]
gr += L.graphics({"line": line, "text": text, "poly": poly}, BX0, BY0, BX1, BY1)

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
       [Sym("title_block"), [Sym("title"), L.PCB_TITLE], [Sym("date"), L.DATE], [Sym("rev"), L.REV]],
       LAYERS, [Sym("setup"), [Sym("pad_to_mask_clearance"), 0]]]
pcb += [[Sym("net"), c, n] for n, c in sorted(netcode.items(), key=lambda kv: kv[1])]
pcb += fps + gr
open(os.path.join(OUT, f"{PROJECT}.kicad_pcb"), "w").write(dump(pcb) + "\n")

# ======================================================================
# project file with net classes (clearance) and library tables
# ======================================================================
import json
proj = {
    "board": {"design_settings": {"rules": {"min_clearance": 0.2, "min_track_width": 0.2,
                                            **getattr(L, "BOARD_RULES", {})}}},
    "meta": {"filename": f"{PROJECT}.kicad_pro", "version": 1},
    "net_settings": {
        "classes": [
            {"name": "Default", "clearance": 0.2, "track_width": 0.25, "via_diameter": 0.8, "via_drill": 0.4,
             "diff_pair_gap": 0.25, "diff_pair_width": 0.2, "microvia_diameter": 0.3, "microvia_drill": 0.1,
             "wire_width": 6, "bus_width": 12, "line_style": 0, "schematic_color": "rgba(0, 0, 0, 0.000)",
             "pcb_color": "rgba(0, 0, 0, 0.000)"},
            {"name": L.HV_CLASS, "clearance": L.HV_CLEARANCE, "track_width": 1.5, "via_diameter": 1.6, "via_drill": 0.8,
             "diff_pair_gap": 0.25, "diff_pair_width": 0.2, "microvia_diameter": 0.3, "microvia_drill": 0.1,
             "wire_width": 6, "bus_width": 12, "line_style": 0, "schematic_color": "rgba(194, 0, 0, 1.000)",
             "pcb_color": "rgba(194, 0, 0, 1.000)"},
            {"name": "Power_GND", "clearance": 0.4, "track_width": 2.0, "via_diameter": 1.2, "via_drill": 0.6,
             "diff_pair_gap": 0.25, "diff_pair_width": 0.2, "microvia_diameter": 0.3, "microvia_drill": 0.1,
             "wire_width": 6, "bus_width": 12, "line_style": 0, "schematic_color": "rgba(0, 0, 0, 0.000)",
             "pcb_color": "rgba(0, 0, 0, 0.000)"},
        ] + [dict({"diff_pair_gap": 0.25, "diff_pair_width": 0.2, "microvia_diameter": 0.3, "microvia_drill": 0.1,
                   "wire_width": 6, "bus_width": 12, "line_style": 0, "schematic_color": "rgba(0, 0, 0, 0.000)",
                   "pcb_color": "rgba(0, 0, 0, 0.000)"}, **{k: v for k, v in c.items() if k != "patterns"})
             for c in getattr(L, "EXTRA_CLASSES", [])],
        "meta": {"version": 3},
        "netclass_patterns": [{"netclass": L.HV_CLASS, "pattern": n} for n in HV_NETS] +
                             [{"netclass": "Power_GND", "pattern": pt} for pt in L.POWER_GND_PATS] +
                             [{"netclass": c["name"], "pattern": pt} for c in getattr(L, "EXTRA_CLASSES", []) for pt in c["patterns"]],
    },
    "pcbnew": {"last_paths": {}, "page_layout_descr_file": ""},
    "schematic": {"meta": {"version": 1}},
    "sheets": [[ROOT, "Root"]] + [[u, n] for n, (u, f, pg) in sheet_uuids.items()],
}
json.dump(proj, open(os.path.join(OUT, f"{PROJECT}.kicad_pro"), "w"), indent=2)
# custom DRC rules: package-internal pin spacing is set by the manufacturer; creepage to SELV is checked
# against the slot (see Dwgs.User note); everything else uses the net classes above.
DRU = ['(version 1)']
for ref, mn in L.DRU_PITCH:
    DRU.append(f'(rule "package pitch {ref}" (condition "A.insideCourtyard(\'{ref}\') && B.insideCourtyard(\'{ref}\')")'
               f' (constraint clearance (min {mn}mm)))')
DRU += L.DRU_EXTRA
open(os.path.join(OUT, f"{PROJECT}.kicad_dru"), "w").write("\n".join(DRU) + "\n")
if os.path.isdir(LOCAL): shutil.copytree(LOCAL, os.path.join(OUT, "lib"), dirs_exist_ok=True)
open(os.path.join(OUT, "sym-lib-table"), "w").write(
    '(sym_lib_table\n  (lib (name "Custom")(type "KiCad")(uri "${KIPRJMOD}/lib/Custom.kicad_sym")(options "")(descr "project"))\n)\n')
open(os.path.join(OUT, "fp-lib-table"), "w").write(
    '(fp_lib_table\n  (lib (name "Custom")(type "KiCad")(uri "${KIPRJMOD}/lib/Custom.pretty")(options "")(descr "project"))\n)\n')

print(f"schematic sheets {len(sheet_uuids)}, power flags {len(need_flag)}, footprints placed {len(fps)}/{len(S.PARTS)}")
if overflow:
    print("NOT PLACED (region full):", overflow)
