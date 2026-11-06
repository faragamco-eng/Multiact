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
echo "all checks passed"
