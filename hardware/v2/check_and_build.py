#!/usr/bin/env python3
"""
check_and_build.py - validate schematic.py against the official KiCad 7.0.11
libraries + ST pin data, and write netlist.md, bom.csv, a KiCad netlist and an
Altium/Protel netlist.

    python3 check_and_build.py --symbols kicad-symbols(7.0.11) --footprints kicad-footprints(7.0.11) \
                               --pindata STM32_open_pin_data
See README.md for the clone commands.
"""
import argparse, collections, csv, os, re, sys
import xml.etree.ElementTree as ET

HERE = os.path.dirname(os.path.abspath(__file__))
ap = argparse.ArgumentParser()
ap.add_argument("--symbols", required=True)
ap.add_argument("--footprints", required=True)
ap.add_argument("--pindata", help="ST open pin data (only needed when the design lists MCU_CHECKS)")
ap.add_argument("--design", default=HERE, help="folder with schematic.py, layout.py and lib/")
ap.add_argument("--out")
a = ap.parse_args()
DESIGN = os.path.abspath(a.design)
sys.path.insert(0, os.path.join(HERE, "tools")); sys.path.insert(0, DESIGN)
import schematic as S
import layout as LY
from kicadlib import SymbolLib, FootprintLib
a.out = a.out or os.path.join(DESIGN, "out")
os.makedirs(a.out, exist_ok=True)
LOCAL = os.path.join(DESIGN, "lib")
SL, FL = SymbolLib(a.symbols, LOCAL), FootprintLib(a.footprints, LOCAL if os.path.isdir(LOCAL) else None)
PROJECT = LY.PROJECT

errors, warnings = [], []
err, warn = errors.append, warnings.append

refs = collections.Counter(p["ref"] for p in S.PARTS)
for r, n in refs.items():
    if n > 1: err(f"duplicate reference {r}")

nets = collections.defaultdict(list)
for p in S.PARTS:
    for pin, net in p["pins"].items():
        nets[net].append((p["ref"], pin))

for p in S.PARTS:
    lib, name = p["sym"].split(":")
    if lib == "Custom":
        warn(f"{p['ref']}: project-local symbol {p['sym']} (lib/)")
    if True:
        pins = SL.pins(lib, name)
        if pins is None:
            err(f"{p['ref']}: symbol {p['sym']} not found in KiCad library"); continue
        sp = {}
        for x in pins: sp.setdefault(x["number"], x)
        for pin in p["pins"]:
            if pin not in sp:
                err(f"{p['ref']} ({p['sym']}): pin {pin} does not exist on the symbol (has {sorted(sp)})")
        for pin, exp in p["names"].items():
            if pin in sp and sp[pin]["name"].lower() != exp.lower():
                err(f"{p['ref']} ({p['sym']}): pin {pin} is '{sp[pin]['name']}', expected '{exp}'")
        for pin, x in sp.items():
            if pin not in p["pins"] and x["type"] != "no_connect":
                warn(f"{p['ref']} ({p['sym']}): pin {pin} '{x['name']}' left unconnected")
    if not FL.exists(p["fp"]):
        err(f"{p['ref']}: footprint {p['fp']} not found in KiCad library")
    else:
        pd = FL.pads(p["fp"])
        for pin in p["pins"]:
            if pin not in pd:
                err(f"{p['ref']}: pin {pin} has no pad in footprint {p['fp']} (pads {sorted(pd)})")
        extra = sorted(x for x in pd - set(p["pins"]) if x)
        if extra and not p["ref"].startswith(("J", "TP", "H", "FID")):
            warn(f"{p['ref']}: footprint pads without net: {extra}")

def st_pins(xmlname):
    t = ET.parse(os.path.join(a.pindata, "mcu", xmlname)).getroot()
    tag = lambda e: e.tag.split('}')[1]
    return {p.get("Position"): (p.get("Name"), {s.get("Name") for s in p if tag(s) == "Signal"})
            for p in t if tag(p) == "Pin"}

for ref, xml, funcs in getattr(S, "MCU_CHECKS", []):
    stp = st_pins(xml)
    part = next(p for p in S.PARTS if p["ref"] == ref)
    sp = {x["number"]: x for x in SL.pins(*part["sym"].split(":"))}
    name_at = {}
    for pos, (stname, sigs) in stp.items():
        base = stname.split("-")[0].split(" ")[0]
        symname = sp.get(pos, {"name": "?"})["name"]
        name_at[base] = pos
        alts = set(re.findall(r"P[A-Z]\d+", stname)) | {base}
        if not (set(symname.split("/")) & alts or symname.startswith(base)):
            if not (symname in ("VDD", "VSS", "VDDA", "VSSA", "VBAT", "VREF+") or base in ("VDD/VDDA", "VSS/VSSA")):
                err(f"{ref}: KiCad pin {pos} '{symname}' vs ST position {pos} '{stname}'")
    for fmap in funcs:
        for pin, fn in fmap.items():
            pos = name_at.get(pin)
            if pos is None: err(f"{ref}: {pin} not in package"); continue
            if fn not in stp[pos][1]:
                err(f"{ref}: {pin} (pos {pos}) has no function {fn}; available: {sorted(stp[pos][1])}")
            netname = part["pins"].get(pos)
            if netname is None or netname.startswith("NC_"):
                err(f"{ref}: {pin} must carry {fn} but is not connected")

for net, nodes in nets.items():
    if len(nodes) < 2 and not net.startswith("NC_"):
        err(f"net {net} has a single connection {nodes}")
    if net.startswith("NC_") and len(nodes) > 1:
        err(f"net {net} is marked NC but has {len(nodes)} connections")
for net in nets:
    for pw in getattr(S, "POWER_NETS", ["GND"]):
        if net != pw and net.replace(" ", "").upper() == pw.upper():
            err(f"net {net} looks like a typo of {pw}")
ISO = getattr(S, "ISOLATION", None)      # (selv_net(name) -> bool, refs allowed to bridge)
if ISO:
    SELV = {n for n in nets if ISO[0](n)}
    for p in S.PARTS:
        ns = {n for n in p["pins"].values() if not n.startswith("NC_")}
        s_side, h_side = ns & SELV, ns - SELV
        if s_side and h_side and p["ref"] not in ISO[1]:
            err(f"ISOLATION: {p['ref']} bridges SELV {sorted(s_side)} and hot {sorted(h_side)}")

with open(os.path.join(a.out, "check_report.txt"), "w") as f:
    f.write(f"parts {len(S.PARTS)}, nets {len(nets)}\nERRORS {len(errors)}\n")
    f.writelines(f"  E {e}\n" for e in errors)
    f.write(f"WARNINGS {len(warnings)}\n")
    f.writelines(f"  W {w}\n" for w in warnings)

def q(s): return '"' + str(s).replace('"', "'") + '"'
with open(os.path.join(a.out, f"{PROJECT}.net"), "w") as f:
    f.write('(export (version "E")\n  (design (source "schematic.py") (tool "check_and_build.py"))\n  (components\n')
    for p in S.PARTS:
        lib, name = p["sym"].split(":")
        dnp = ' (field (name "DNP") "1")' if p["dnp"] else ""
        f.write(f'    (comp (ref {q(p["ref"])}) (value {q(p["value"])}) (footprint {q(p["fp"])})\n'
                f'      (fields (field (name "MPN") {q(p["mpn"])}) (field (name "Sheet") {q(p["sheet"])}){dnp})\n'
                f'      (libsource (lib {q(lib)}) (part {q(name)}) (description {q(p["note"])})))\n')
    f.write('  )\n  (nets\n')
    for i, (net, nodes) in enumerate(sorted(nets.items()), 1):
        f.write(f'    (net (code "{i}") (name {q(net)})\n')
        for r, pin in nodes:
            f.write(f'      (node (ref {q(r)}) (pin {q(pin)}))\n')
        f.write('    )\n')
    f.write('  )\n)\n')

# Protel / Altium netlist (Altium: PCB > Design > Import Changes / netlist import)
with open(os.path.join(a.out, f"{PROJECT}_altium_protel.net"), "w") as f:
    for p in S.PARTS:
        f.write(f"[\n{p['ref']}\n{p['fp'].split(':')[1]}\n{p['value']}\n\n\n\n]\n")
    for net, nodes in sorted(nets.items()):
        if net.startswith("NC_"): continue
        f.write(f"(\n{net}\n" + "".join(f"{r}-{pin}\n" for r, pin in nodes) + ")\n")

groups = collections.OrderedDict()
for p in S.PARTS:
    groups.setdefault((p["value"], p["fp"], p["mpn"], p["dnp"]), []).append(p["ref"])
with open(os.path.join(a.out, "bom.csv"), "w", newline="") as f:
    w = csv.writer(f)
    w.writerow(["Qty", "References", "Value", "MPN / description", "Footprint", "Populate"])
    for (val, fp, mpn, dnp), rs in groups.items():
        w.writerow([len(rs), " ".join(rs), val, mpn, fp, "OPTION/DNP" if dnp else "yes"])

with open(os.path.join(a.out, "netlist.md"), "w") as f:
    f.write(f"# {LY.TITLE} — netlist (generated from schematic.py, do not edit)\n\n")
    f.write(f"Parts: {len(S.PARTS)} · nets: {len(nets)} · check errors: {len(errors)}\n\n")
    cur = None
    for p in S.PARTS:
        if p["sheet"] != cur:
            cur = p["sheet"]
            f.write(f"\n## Sheet {cur}\n\n| Ref | Part | Value | Pins → nets | Note |\n|---|---|---|---|---|\n")
        pins = ", ".join(f"{k}{('/' + p['names'][k]) if k in p['names'] else ''}→`{v}`" for k, v in p["pins"].items())
        f.write(f"| {p['ref']}{' (DNP)' if p['dnp'] else ''} | {p['mpn'] or p['sym']} | {p['value']} | {pins} | {p['note']} |\n")
    f.write("\n## Net index\n\n| Net | Nodes |\n|---|---|\n")
    for net, nodes in sorted(nets.items()):
        f.write(f"| `{net}` | {' '.join(f'{r}.{pin}' for r, pin in nodes)} |\n")

print(f"parts {len(S.PARTS)}  nets {len(nets)}  errors {len(errors)}  warnings {len(warnings)}")
for e in errors: print("  E", e)
for w in warnings: print("  W", w)
sys.exit(1 if errors else 0)
