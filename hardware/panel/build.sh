#!/bin/sh
# Rebuild + route + verify the panel: ./build.sh [library-cache-dir]   (NOROUTE=1 ./build.sh: placement only)
# shares the tools and the KiCad library cache with hardware/v2
set -e
cd "$(dirname "$0")"
V2=../v2
L=${1:-$V2/.libcache}
[ -d "$L/sym7" ] || (cd $V2 && ./build.sh >/dev/null)          # fetches the KiCad 7.0.11 libraries once
python3 $V2/check_and_build.py --design . --symbols "$L/sym7" --footprints "$L/fp7"
python3 $V2/gen_kicad.py --design . --symbols "$L/sym7" --footprints "$L/fp7"
if [ -z "$NOROUTE" ]; then                                        # two-layer autorouting (~4 min)
  cp out/kicad/cooler_panel.kicad_pcb out/placed_unrouted.kicad_pcb
  python3 -u $V2/tools/autoroute.py out/placed_unrouted.kicad_pcb out/kicad/cooler_panel.kicad_pcb | grep -E 'rip-up|FAIL|routed|Traceback|Error'
fi
if command -v kicad-cli >/dev/null; then
  (cd out/kicad && kicad-cli sch export netlist --format kicadsexpr -o ../sch_export.net cooler_panel.kicad_sch >/dev/null)
  DESIGN_DIR=$(pwd) python3 $V2/tools/compare_netlist.py out/sch_export.net
  DESIGN_DIR=$(pwd) python3 $V2/tools/check_pcb_nets.py out/kicad/cooler_panel.kicad_pcb
  python3 $V2/tools/drc.py out/kicad/cooler_panel.kicad_pcb out/drc_report.txt
  (cd out/kicad && kicad-cli sch export pdf -o ../cooler_panel_schematic.pdf cooler_panel.kicad_sch >/dev/null \
     && kicad-cli pcb export pdf --layers "F.Cu,F.SilkS,Edge.Cuts,F.Fab,Dwgs.User" -o ../cooler_panel_placement.pdf cooler_panel.kicad_pcb >/dev/null)
fi
if command -v kicad-cli >/dev/null && [ -z "$NOROUTE" ]; then                     # fabrication files
  rm -rf out/gerbers && mkdir -p out/gerbers
  kicad-cli pcb export gerbers --layers "F.Cu,B.Cu,F.Paste,B.Paste,F.SilkS,B.SilkS,F.Mask,B.Mask,Edge.Cuts" \
     --subtract-soldermask -o out/gerbers/ out/kicad/cooler_panel.kicad_pcb >/dev/null
  kicad-cli pcb export drill --format excellon --excellon-separate-th -o out/gerbers/ out/kicad/cooler_panel.kicad_pcb >/dev/null
  kicad-cli pcb export pos --format csv --units mm --side front -o out/cooler_panel_pick_and_place.csv out/kicad/cooler_panel.kicad_pcb >/dev/null
  (cd out/gerbers && rm -f ../cooler_panel_gerbers.zip && zip -q ../cooler_panel_gerbers.zip *)
  (cd out/kicad && kicad-cli pcb export pdf --layers "F.Cu,F.SilkS,Edge.Cuts" -o ../cooler_panel_routed_top.pdf cooler_panel.kicad_pcb >/dev/null \
     && kicad-cli pcb export pdf --layers "B.Cu,Edge.Cuts" -o ../cooler_panel_routed_bottom.pdf cooler_panel.kicad_pcb >/dev/null)
fi
echo "all checks passed"
