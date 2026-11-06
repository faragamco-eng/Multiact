#!/bin/sh
# Rebuild + verify everything: ./build.sh [library-cache-dir]
set -e
cd "$(dirname "$0")"
L=${1:-.libcache}
mkdir -p "$L"
[ -d "$L/sym7" ] || git clone -q --depth 1 --branch 7.0.11 https://gitlab.com/kicad/libraries/kicad-symbols.git "$L/sym7"
[ -d "$L/fp7" ]  || git clone -q --depth 1 --branch 7.0.11 --filter=blob:none --no-checkout https://gitlab.com/kicad/libraries/kicad-footprints.git "$L/fp7"
if [ ! -d "$L/pindata" ]; then
  git clone -q --depth 1 --filter=blob:none --sparse https://github.com/STMicroelectronics/STM32_open_pin_data.git "$L/pindata"
  (cd "$L/pindata" && git sparse-checkout set --no-cone '/mcu/STM32G431C(6-8-B)Tx.xml' '/mcu/STM32C011F(4-6)Px.xml')
fi
python3 check_and_build.py --symbols "$L/sym7" --footprints "$L/fp7" --pindata "$L/pindata"   # 1. design rules
python3 calc_v2.py > /dev/null                                                               # 2. calculations
python3 gen_kicad.py --symbols "$L/sym7" --footprints "$L/fp7"                               # 3. KiCad project
if command -v kicad-cli >/dev/null; then                                                      # 4. KiCad round-trip
  (cd out/kicad && kicad-cli sch export netlist --format kicadsexpr -o ../sch_export.net bldc_cooler_v2.kicad_sch >/dev/null)
  python3 tools/compare_netlist.py out/sch_export.net
  python3 tools/check_pcb_nets.py out/kicad/bldc_cooler_v2.kicad_pcb
  python3 tools/drc.py out/kicad/bldc_cooler_v2.kicad_pcb out/drc_report.txt
  (cd out/kicad && kicad-cli sch export pdf -o ../bldc_cooler_v2_schematic.pdf bldc_cooler_v2.kicad_sch >/dev/null \
     && kicad-cli pcb export pdf --layers "F.Cu,F.SilkS,Edge.Cuts,F.Fab,Dwgs.User" -o ../bldc_cooler_v2_placement.pdf bldc_cooler_v2.kicad_pcb >/dev/null)
fi
echo "all checks passed"
