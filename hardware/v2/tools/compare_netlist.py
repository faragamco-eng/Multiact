#!/usr/bin/env python3
"""Compare a netlist exported by KiCad from the generated schematic with schematic.py."""
import collections, os, sys
HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, HERE); sys.path.insert(0, os.path.join(HERE, "tools"))
import schematic as S
from sexpr import parse, find, first
exp = parse(open(sys.argv[1]).read())[0]
got = {}
for n in find(first(exp, "nets"), "net"):
    name = first(n, "name")[1].lstrip("/")
    nodes = {(first(x, "ref")[1], first(x, "pin")[1]) for x in find(n, "node") if not first(x, "ref")[1].startswith("#")}
    if nodes: got[name] = nodes
want = collections.defaultdict(set)
for p in S.PARTS:
    for pin, net in p["pins"].items():
        if not net.startswith("NC_"):
            want[net].add((p["ref"], pin))
bad = 0
for net, nodes in want.items():
    if got.get(net) != nodes:
        bad += 1
        print(f"MISMATCH {net}: missing {sorted(nodes - got.get(net, set()))} extra {sorted(got.get(net, set()) - nodes)}")
for net in got:
    if net not in want and not net.startswith(("unconnected-", "Net-(")):
        bad += 1; print("EXTRA NET", net, sorted(got[net]))
comps = {first(c, "ref")[1] for c in find(first(exp, "components"), "comp")}
missing = {p["ref"] for p in S.PARTS} - comps
if missing: bad += 1; print("components missing from schematic:", sorted(missing))
print(f"schematic export vs schematic.py: {len(want)} nets compared, {len(comps)} components, {bad} differences")
sys.exit(1 if bad else 0)
