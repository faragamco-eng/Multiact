#!/usr/bin/env python3
"""Run KiCad DRC (pcbnew API) and summarise; exit 1 if any blocking violation."""
import collections, re, sys, pcbnew
b = pcbnew.LoadBoard(sys.argv[1])
rpt = sys.argv[2]
pcbnew.WriteDRCReport(b, rpt, pcbnew.EDA_UNITS_MILLIMETRES, True)
t = open(rpt).read()
blocks = re.findall(r'^\[(\w+)\]: (.*)\n((?:    .*\n)+)', t, re.M)
c = collections.Counter(k for k, _, _ in blocks)
INFO = {"unconnected_items", "silk_overlap", "silk_over_copper", "silk_edge_clearance", "lib_footprint_issues",
        "lib_footprint_mismatch", "text_height", "text_thickness"}
print("DRC:", dict(c))
bad = 0
for k, msg, body in blocks:
    if k in INFO: continue
    bad += 1
    if bad <= 25:
        items = [l.strip() for l in body.splitlines() if l.strip().startswith("@")]
        print(" ", k, "|", msg[:80], "|", " || ".join(items))
print("blocking violations:", bad)
sys.exit(1 if bad else 0)
