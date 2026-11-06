#!/usr/bin/env python3
"""
check_and_build.py - validate schematic.py and generate the deliverables.

    git clone --depth 1 https://gitlab.com/kicad/libraries/kicad-symbols.git
    git clone --depth 1 --filter=blob:none --no-checkout https://gitlab.com/kicad/libraries/kicad-footprints.git
    git clone --depth 1 --filter=blob:none --sparse https://github.com/STMicroelectronics/STM32_open_pin_data.git
      (sparse paths: /mcu/STM32G431C(6-8-B)Tx.xml  /mcu/STM32C011F(4-6)Px.xml)
    python3 check_and_build.py --symbols kicad-symbols --footprints kicad-footprints --pindata STM32_open_pin_data

Checks
  1. every symbol exists in the official KiCad library (extends-chains resolved)
  2. every connected pin number exists on the symbol, and has the expected name
  3. every symbol pin is connected (or deliberately left NC_* / no_connect)
  4. every footprint exists, and every connected pin number exists as a pad
  5. MCU pins: symbol pin names match ST package positions, and every
     required alternate function exists on that pin (ST open pin data)
  6. nets: no single-node nets except NC_*, no power-net typos
Outputs (out/): netlist.md, bom.csv, bldc_cooler_v2.net (KiCad), check_report.txt
"""
import argparse, collections, csv, os, re, subprocess, sys
import xml.etree.ElementTree as ET

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import schematic as S

ap = argparse.ArgumentParser()
ap.add_argument("--symbols", required=True)
ap.add_argument("--footprints", required=True)
ap.add_argument("--pindata", required=True)
ap.add_argument("--out", default=os.path.join(os.path.dirname(os.path.abspath(__file__)), "out"))
a = ap.parse_args()
os.makedirs(a.out, exist_ok=True)

errors, warnings = [], []
def err(m): errors.append(m)
def warn(m): warnings.append(m)

# ---------------------------------------------------------------- symbols
_symcache = {}
def load_symbol(lib, name):
    key = (lib, name)
    if key in _symcache: return _symcache[key]
    path = os.path.join(a.symbols, f"{lib}.kicad_symdir", f"{name}.kicad_sym")
    if not os.path.exists(path):
        _symcache[key] = None; return None
    t = open(path).read()
    ext = re.search(r'\(extends "([^"]*)"', t)
    if ext:
        base = load_symbol(lib, ext.group(1))
        res = dict(base) if base else None
    else:
        pins = {}
        for m in re.finditer(r'\(pin (\w+) \w+.*?\(name "([^"]*)".*?\(number "([^"]*)"', t, re.S):
            pins[m.group(3)] = (m.group(2), m.group(1))
        res = {"pins": pins}
    _symcache[key] = res
    return res

# ---------------------------------------------------------------- footprints
fp_list = set()
out = subprocess.run(["git", "-C", a.footprints, "ls-tree", "-r", "--name-only", "HEAD"], capture_output=True, text=True).stdout
for line in out.splitlines():
    if line.endswith(".kicad_mod"):
        lib, f = line.split("/")
        fp_list.add(lib.replace(".pretty", "") + ":" + f[:-10])
_padcache = {}
def pads(fp):
    if fp in _padcache: return _padcache[fp]
    lib, name = fp.split(":")
    r = subprocess.run(["git", "-C", a.footprints, "show", f"HEAD:{lib}.pretty/{name}.kicad_mod"], capture_output=True, text=True)
    p = set(re.findall(r'\(pad "([^"]*)"', r.stdout))
    _padcache[fp] = p
    return p

# ---------------------------------------------------------------- ST pin data
def st_pins(xmlname):
    f = os.path.join(a.pindata, "mcu", xmlname)
    t = ET.parse(f).getroot()
    tag = lambda e: e.tag.split('}')[1]
    res = {}
    for p in t:
        if tag(p) == "Pin":
            res[p.get("Position")] = (p.get("Name"), {s.get("Name") for s in p if tag(s) == "Signal"})
    return res

# ---------------------------------------------------------------- checks
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
        warn(f"{p['ref']}: custom symbol {p['sym']} (not validated against the KiCad library)")
        sym = None
    else:
        sym = load_symbol(lib, name)
        if sym is None:
            err(f"{p['ref']}: symbol {p['sym']} not found in KiCad library"); continue
    if sym:
        sp = sym["pins"]
        for pin in p["pins"]:
            if pin not in sp:
                err(f"{p['ref']} ({p['sym']}): pin {pin} does not exist on the symbol (has {sorted(sp)})")
        for pin, exp in p["names"].items():
            if pin in sp and sp[pin][0].lower() != exp.lower():
                err(f"{p['ref']} ({p['sym']}): pin {pin} is '{sp[pin][0]}', expected '{exp}'")
        for pin, (nm, typ) in sp.items():
            if pin not in p["pins"] and typ != "no_connect":
                warn(f"{p['ref']} ({p['sym']}): pin {pin} '{nm}' left unconnected")
    # footprint
    if p["fp"] not in fp_list:
        err(f"{p['ref']}: footprint {p['fp']} not found in KiCad library")
    else:
        pd = pads(p["fp"])
        for pin in p["pins"]:
            if pin not in pd:
                err(f"{p['ref']}: pin {pin} has no pad in footprint {p['fp']} (pads {sorted(pd)})")
        extra = sorted(x for x in pd - set(p["pins"]) if x)
        if extra and not p["ref"].startswith(("J", "TP")):
            warn(f"{p['ref']}: footprint pads without net: {extra}")

# MCU pin data
for ref, xml, funcs in (("U6", "STM32G431C(6-8-B)Tx.xml", [S.MCU_FUNCS, S.MCU_FUNCS_EXTRA]),
                        ("U8", "STM32C011F(4-6)Px.xml", [S.PHY_FUNCS])):
    stp = st_pins(xml)
    part = next(p for p in S.PARTS if p["ref"] == ref)
    sym = load_symbol(*part["sym"].split(":"))
    name_at = {}
    for pos, (stname, sigs) in stp.items():
        base = stname.split("-")[0].split(" ")[0]
        symname = sym["pins"].get(pos, ("?",))[0]
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

# nets
for net, nodes in nets.items():
    if len(nodes) < 2 and not net.startswith("NC_"):
        err(f"net {net} has a single connection {nodes}")
    if net.startswith("NC_") and len(nodes) > 1:
        err(f"net {net} is marked NC but has {len(nodes)} connections")
power = ["+15V", "+3V3", "+3V3A", "+3V3S", "GND", "GND_S", "PGND", "VBUS", "PE"]
for net in nets:
    for pw in power:
        if net != pw and net.replace(" ", "").upper() == pw.upper():
            err(f"net {net} looks like a typo of {pw}")
# isolation: no part may connect a SELV net and a hot net except the isolator / transformer / Y-caps
selv = {n for n in nets if n.endswith("_S") or n in ("GND_S", "+3V3S", "V5S", "V31S", "T_S5", "T_S31")
        or n.startswith(("PB_", "PANEL_", "RS485", "UART_S_", "LED3", "NC_S"))}
for p in S.PARTS:
    ns = set(p["pins"].values()) - {n for n in p["pins"].values() if n.startswith("NC_")}
    s_side, h_side = ns & selv, ns - selv
    if s_side and h_side and p["ref"] not in ("U7", "T1"):
        err(f"ISOLATION: {p['ref']} bridges SELV {sorted(s_side)} and hot {sorted(h_side)}")

# ---------------------------------------------------------------- outputs
with open(os.path.join(a.out, "check_report.txt"), "w") as f:
    f.write(f"parts {len(S.PARTS)}, nets {len(nets)}\nERRORS {len(errors)}\n")
    f.writelines(f"  E {e}\n" for e in errors)
    f.write(f"WARNINGS {len(warnings)}\n")
    f.writelines(f"  W {w}\n" for w in warnings)

# KiCad netlist (S-expression, version E) -> PCB editor: File > Import > Netlist
def q(s): return '"' + str(s).replace('"', "'") + '"'
with open(os.path.join(a.out, "bldc_cooler_v2.net"), "w") as f:
    f.write('(export (version "E")\n  (design (source "hardware/v2/schematic.py") (tool "check_and_build.py"))\n  (components\n')
    for p in S.PARTS:
        lib, name = p["sym"].split(":")
        f.write(f'    (comp (ref {q(p["ref"])}) (value {q(p["value"])}) (footprint {q(p["fp"])})\n'
                f'      (fields (field (name "MPN") {q(p["mpn"])}) (field (name "Sheet") {q(p["sheet"])})'
                f'{" (field (name \"DNP\") \"1\")" if p["dnp"] else ""})\n'
                f'      (libsource (lib {q(lib)}) (part {q(name)}) (description {q(p["note"])})))\n')
    f.write('  )\n  (nets\n')
    for i, (net, nodes) in enumerate(sorted(nets.items()), 1):
        f.write(f'    (net (code "{i}") (name {q(net)})\n')
        for r, pin in nodes:
            f.write(f'      (node (ref {q(r)}) (pin {q(pin)}))\n')
        f.write('    )\n')
    f.write('  )\n)\n')

# BOM
groups = collections.OrderedDict()
for p in S.PARTS:
    k = (p["value"], p["fp"], p["mpn"], p["dnp"])
    groups.setdefault(k, []).append(p["ref"])
with open(os.path.join(a.out, "bom.csv"), "w", newline="") as f:
    w = csv.writer(f)
    w.writerow(["Qty", "References", "Value", "MPN / description", "Footprint", "DNP"])
    for (val, fp, mpn, dnp), rs in groups.items():
        w.writerow([len(rs), " ".join(rs), val, mpn, fp, "DNP" if dnp else ""])

# human-readable netlist
with open(os.path.join(a.out, "netlist.md"), "w") as f:
    f.write("# BLDC cooler drive v2 — netlist (generated from schematic.py, do not edit)\n\n")
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
