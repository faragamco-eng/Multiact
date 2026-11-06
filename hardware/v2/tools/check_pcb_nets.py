#!/usr/bin/env python3
"""Every pad on the generated board must carry the net defined in schematic.py."""
import os, sys, pcbnew
HERE = os.environ.get("DESIGN_DIR", os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, HERE)
import schematic as S
b = pcbnew.LoadBoard(sys.argv[1])
fps = {f.GetReference(): f for f in b.GetFootprints()}
bad = 0
for p in S.PARTS:
    f = fps.get(p["ref"])
    if f is None:
        print("missing footprint", p["ref"]); bad += 1; continue
    pads = {}
    for pad in f.Pads():
        pads.setdefault(pad.GetNumber(), set()).add(pad.GetNetname())
    for pin, net in p["pins"].items():
        want = "" if net.startswith("NC_") else net
        if pads.get(pin) != {want}:
            print(f"{p['ref']}.{pin}: board {pads.get(pin)} vs schematic {want!r}"); bad += 1
print(f"board pads vs schematic.py: {len(S.PARTS)} parts, {bad} differences")
sys.exit(1 if bad else 0)
