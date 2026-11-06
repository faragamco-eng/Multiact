# BLDC cooler motor drive v2 (220 VAC, PFC, sensorless FOC) — netlist (generated from schematic.py, do not edit)

Parts: 289 · nets: 199 · check errors: 0


## Sheet 1 AC input, protection, relays, mains sensing

| Ref | Part | Value | Pins → nets | Note |
|---|---|---|---|---|
| J1 | Phoenix MKDS 1,5/3-5,08 | AC IN L/N/PE | 1→`AC_L`, 2→`AC_N`, 3→`PE` |  |
| F1 | 5x20 ceramic 10 A time-lag | 10A T 250VAC | 1→`AC_L`, 2→`AC_LF0` |  |
| RV1 | EPCOS S14K385 | S14K385 | 1→`AC_LF0`, 2→`AC_N` | 385 VAC: survives 380-440 V neutral loss |
| RV2 | Device:Varistor | S14K385 | 1→`AC_LF0`, 2→`SURGE_PE` |  |
| RV3 | Device:Varistor | S14K385 | 1→`AC_N`, 2→`SURGE_PE` |  |
| GDT1 | axial 2-electrode GDT 600 V (e.g. Bourns 2026-60 / EPCOS EC600X) | GDT 600V | 1→`SURGE_PE`, 3→`PE` |  |
| CX1 | X2 310VAC | 470nF X2 | 1→`AC_LF0`, 2→`AC_N` |  |
| R1 | Device:R | 470k | 1→`AC_LF0`, 2→`XDIS` | X-cap discharge |
| R2 | Device:R | 470k | 1→`XDIS`, 2→`AC_N` | X-cap discharge |
| L1 | toroidal CM choke 2 x 4.7 mH, >= 10 A (e.g. Schaffner RN series); verify pin map | 2x4.7mH 10A | 1→`AC_LF0`, 2→`AC_LF`, 3→`AC_N`, 4→`AC_NF` |  |
| CX2 | Device:C | 470nF X2 | 1→`AC_LF`, 2→`AC_NF` |  |
| CY1 | Y2 300VAC | 2.2nF Y2 | 1→`AC_LF`, 2→`PE` |  |
| CY2 | Y2 300VAC | 2.2nF Y2 | 1→`AC_NF`, 2→`PE` |  |
| K1 | Finder 40.11.9.012.0000 (10 A, 12 V coil) | Finder 40.11.9.012 | A1→`K1_A1`, A2→`K1_DRV`, 11→`AC_LF`, 14→`L_SW`, 12→`NC_K1` | opens on Vin > 280 V within one half cycle |
| K2 | Finder 34.51.7.012.0010 | Finder 34.51.7.012 | A1→`K2_A1`, A2→`K2_DRV`, 11→`AC_LF`, 14→`PRE_L`, 12→`NC_K2` |  |
| PTC1 | inrush PTC rated >= 80 J at 265 VAC (940 uF @ 373 V = 65 J), e.g. TDK/EPCOS J1xx series - verify | PTC 120R inrush | 1→`PRE_L`, 2→`L_SW` | self-protecting if the pre-charge path stays closed into a fault |
| D1 | 1N4007 | 1N4007 | 1→`AUX_RAW`, 2→`AC_LF` |  |
| D2 | 1N4007 | 1N4007 | 1→`AUX_RAW`, 2→`AC_NF` |  |
| D3 | 1N4007 | 1N4007 | 1→`AC_LF`, 2→`PGND` |  |
| C1 | Device:C | 100nF 630V | 1→`AUX_RAW`, 2→`PGND` |  |
| D29 | UF4007 (1000 V) | UF4007 | 1→`AUX_RAW`, 2→`VBUS` |  |
| R3 | Device:R | 1M | 1→`AC_LF`, 2→`VIN_L_A` | 1206 200 V each, 3 in series |
| R4 | Device:R | 1M | 1→`VIN_L_A`, 2→`VIN_L_B` |  |
| R5 | Device:R | 1M | 1→`VIN_L_B`, 2→`VIN_L_C` |  |
| R6 | Device:R | 10k 0.1% | 1→`VIN_L_C`, 2→`GND` |  |
| D4 | BAT54S | BAT54S | 1→`GND`, 2→`+3V3A`, 3/COM→`VIN_L_C` |  |
| C2 | Device:C | 1nF | 1→`VIN_L_C`, 2→`GND` |  |
| R11 | Device:R | 1k | 1→`VIN_L_C`, 2→`VIN_L` |  |
| R7 | Device:R | 1M | 1→`AC_NF`, 2→`VIN_N_A` | 1206 200 V each, 3 in series |
| R8 | Device:R | 1M | 1→`VIN_N_A`, 2→`VIN_N_B` |  |
| R9 | Device:R | 1M | 1→`VIN_N_B`, 2→`VIN_N_C` |  |
| R10 | Device:R | 10k 0.1% | 1→`VIN_N_C`, 2→`GND` |  |
| D5 | BAT54S | BAT54S | 1→`GND`, 2→`+3V3A`, 3/COM→`VIN_N_C` |  |
| C3 | Device:C | 1nF | 1→`VIN_N_C`, 2→`GND` |  |
| R12 | Device:R | 1k | 1→`VIN_N_C`, 2→`VIN_N` |  |

## Sheet 2 Rectifier and PFC (CCM, NCP1653, 390 V)

| Ref | Part | Value | Pins → nets | Note |
|---|---|---|---|---|
| BR1 | GBU1010 (10 A 1000 V) on heatsink | GBU1010 | 1→`RECT_P`, 2→`L_SW`, 3→`AC_NF`, 4→`PFC_RTN` |  |
| C4 | Device:C | 1uF 630V | 1→`RECT_P`, 2→`PFC_RTN` |  |
| L2 | toroid ~46 mm OD (e.g. Kool Mu 0077083A7 or High Flux 58083), 470 uH at 11 A, Isat >= 13 A | 470uH 13A | 1→`RECT_P`, 2→`SW_PFC` |  |
| D6 | Wolfspeed C3D08065A / Infineon IDH08G65C6 | SiC 650V 8A | 1→`VBUS`, 2→`SW_PFC` | TO-220-2: pad 1 = K (verify part) |
| D7 | P600M / 1N5408 inrush bypass | P600M | 1→`VBUS`, 2→`RECT_P` |  |
| Q1 | Infineon IPW65R095C7 / ST STW40N60M6 (heatsink, insulated) | 650V 95mOhm SJ | 1/G→`PFC_G`, 2/D→`SW_PFC`, 3/S→`PGND` |  |
| R13 | Device:R | 10R | 1→`PFC_DRV`, 2→`PFC_G` |  |
| R14 | Device:R | 10k | 1→`PFC_G`, 2→`PGND` |  |
| D8 | BZT52C18 | BZT52C18 | 1→`PFC_G`, 2→`PGND` |  |
| R15 | Device:R | 50m 3W | 1→`PFC_RTN`, 2→`PGND` | 2 x 50 mOhm in parallel -> 25 mOhm |
| R16 | Device:R | 50m 3W | 1→`PFC_RTN`, 2→`PGND` |  |
| U1 | onsemi NCP1653ADR2G (67 kHz) | NCP1653 | 1/FB/SD→`PFC_FB`, 2/VCONTROL→`PFC_VCTRL`, 3/IN→`PFC_IN`, 4/CS→`PFC_CS`, 5/VM→`PFC_VM`, 6/GND→`PGND`, 7/DRV→`PFC_DRV`, 8/VCC→`PFC_VCC` | controller passives = SEED values, confirm with the onsemi NCP1653 design worksheet |
| R17 | Device:R | 10R | 1→`+15V`, 2→`PFC_VCC` |  |
| C5 | Device:C_Polarized | 22uF 25V | 1→`PFC_VCC`, 2→`PGND` |  |
| C6 | Device:C | 100nF | 1→`PFC_VCC`, 2→`PGND` |  |
| R18 | Device:R | 470k 1% | 1→`VBUS`, 2→`PFC_FBD1` |  |
| R19 | Device:R | 470k 1% | 1→`PFC_FBD1`, 2→`PFC_FBD2` |  |
| R20 | Device:R | 470k 1% | 1→`PFC_FBD2`, 2→`PFC_FBD3` |  |
| R21 | Device:R | 470k 1% | 1→`PFC_FBD3`, 2→`PFC_FB` |  |
| C7 | Device:C | 1nF | 1→`PFC_FB`, 2→`PGND` |  |
| R22 | Device:R | 1M | 1→`RECT_P`, 2→`PFC_IND1` |  |
| R23 | Device:R | 1M | 1→`PFC_IND1`, 2→`PFC_IND2` |  |
| R24 | Device:R | 1M | 1→`PFC_IND2`, 2→`PFC_IND3` |  |
| R25 | Device:R | 1M | 1→`PFC_IND3`, 2→`PFC_IN` |  |
| C8 | Device:C | 1nF | 1→`PFC_IN`, 2→`PGND` |  |
| R26 | Device:R | 4.7k | 1→`PFC_RTN`, 2→`PFC_CS` | CS gain (verify) |
| C9 | Device:C | 100pF | 1→`PFC_CS`, 2→`PGND` |  |
| R27 | Device:R | 22k | 1→`PFC_VM`, 2→`PGND` | multiplier output (verify) |
| C10 | Device:C | 220pF | 1→`PFC_VM`, 2→`PGND` |  |
| C11 | Device:C | 1uF | 1→`PFC_VCTRL`, 2→`PGND` | voltage-loop compensation (verify) |
| Q2 | 2N7002 | 2N7002 | 1/G→`PFC_OFF`, 2/S→`PGND`, 3/D→`PFC_FB` |  |
| R28 | Device:R | 100k | 1→`PFC_OFF`, 2→`GND` |  |
| C12 | 470 uF 450 V 105 C >= 5000 h snap-in D35 (e.g. Nichicon LGN/LGU, TDK B43544, Rubycon USR) | 470uF 450V | 1→`VBUS`, 2→`PGND` | ripple rating >= 2.5 A @ 100 Hz each |
| C13 | 470 uF 450 V 105 C >= 5000 h snap-in D35 (e.g. Nichicon LGN/LGU, TDK B43544, Rubycon USR) | 470uF 450V | 1→`VBUS`, 2→`PGND` | ripple rating >= 2.5 A @ 100 Hz each |
| R29 | Device:R | 100k | 1→`VBUS`, 2→`BLD1` |  |
| R30 | Device:R | 100k | 1→`BLD1`, 2→`BLD2` |  |
| R31 | Device:R | 100k | 1→`BLD2`, 2→`BLD3` |  |
| LED1 | high-efficiency red, 'HIGH VOLTAGE' | red HV | 1→`PGND`, 2→`BLD3` |  |

## Sheet 3 Auxiliary supply (VIPer26 flyback, 1500 V pre-regulator)

| Ref | Part | Value | Pins → nets | Note |
|---|---|---|---|---|
| Q3 | ST STW4N150 (on main heatsink, insulated) | 1500V 4A | 1/G→`PRE_G`, 2/D→`AUX_RAW`, 3/S→`AUX_HV` |  |
| R32 | Device:R | 1M | 1→`AUX_RAW`, 2→`PREG1` |  |
| R33 | Device:R | 1M | 1→`PREG1`, 2→`PREG2` |  |
| R34 | Device:R | 1M | 1→`PREG2`, 2→`PREG3` |  |
| R35 | Device:R | 1M | 1→`PREG3`, 2→`PRE_G` |  |
| D9 | 1N4764A | 1N4764A 100V | 1→`PRE_G`, 2→`PREZ1` |  |
| D10 | 1N4764A | 1N4764A 100V | 1→`PREZ1`, 2→`PREZ2` |  |
| D11 | 1N4764A | 1N4764A 100V | 1→`PREZ2`, 2→`PREZ3` |  |
| D12 | 1N4764A | 1N4764A 100V | 1→`PREZ3`, 2→`PGND` |  |
| D13 | BZT52C15 | BZT52C15 | 1→`PRE_G`, 2→`AUX_HV` |  |
| C14 | Device:C_Polarized | 10uF 450V | 1→`AUX_HV`, 2→`PGND` |  |
| T1 | custom, see calc/transformer spec | EF20 flyback | 1→`AUX_HV`, 2→`VIP_D`, 4→`T_H15`, 5→`PGND`, 6→`GND_S`, 7→`T_S5`, 8→`T_S31` | pins 1-5 hot side, 6-10 SELV side: reinforced insulation |
| U2 | ST VIPer26LN (60 kHz) | VIPer26LN | 1/GND→`PGND`, 2/VDD→`VIP_VDD`, 3/LIM→`VIP_LIM`, 4/FB→`VIP_FB`, 5/COMP→`VIP_COMP`, 7/DRAIN→`VIP_D`, 8/DRAIN→`VIP_D` |  |
| D14 | UF4007 | UF4007 | 1→`P6K_A`, 2→`VIP_D` |  |
| D15 | P6KE200CA (bidirectional, 200 V clamp) | P6KE200CA | 1→`AUX_HV`, 2→`P6K_A` |  |
| D16 | ES1D | ES1D | 1→`+15V`, 2→`T_H15` |  |
| C15 | Device:C_Polarized | 220uF 35V | 1→`+15V`, 2→`PGND` |  |
| D17 | 1N4148W | 1N4148W | 1→`VIP_VDD`, 2→`+15V` |  |
| C16 | Device:C_Polarized | 10uF 35V | 1→`VIP_VDD`, 2→`PGND` |  |
| R36 | Device:R | 36k 1% | 1→`+15V`, 2→`VIP_FB` |  |
| R37 | Device:R | 10k 1% | 1→`VIP_FB`, 2→`PGND` |  |
| R38 | Device:R | 2.2k | 1→`VIP_COMP`, 2→`VIP_CZ` |  |
| C17 | Device:C | 100nF | 1→`VIP_CZ`, 2→`PGND` |  |
| C18 | Device:C | 2.2nF | 1→`VIP_COMP`, 2→`PGND` |  |
| R39 | Device:R | DNP | 1→`VIP_LIM`, 2→`PGND` | optional: lowers the current limit |
| D18 | SMBJ18CA | SMBJ18CA | 1→`+15V`, 2→`PGND` |  |
| U3 | Diodes AP63203WU (fixed 3.3 V) | AP63203WU | 1/FB→`+3V3`, 2/EN→`+15V`, 3/IN→`+15V`, 4/GND→`GND`, 5/SW→`BUCK_SW`, 6/BST→`BUCK_BST` |  |
| C19 | Device:C | 10uF 25V | 1→`+15V`, 2→`GND` |  |
| C20 | Device:C | 100nF | 1→`BUCK_BST`, 2→`BUCK_SW` |  |
| L3 | Bourns SRN6045TA-6R8M | 6.8uH 1A | 1→`BUCK_SW`, 2→`+3V3` |  |
| C21 | Device:C | 22uF 10V | 1→`+3V3`, 2→`GND` |  |
| FB1 | ferrite bead | 600R@100MHz | 1→`+3V3`, 2→`+3V3A` |  |
| C22 | Device:C | 1uF | 1→`+3V3A`, 2→`GND` |  |
| D19 | SS34 | SS34 | 1→`V5S`, 2→`T_S5` |  |
| C23 | Device:C_Polarized | 220uF 16V | 1→`V5S`, 2→`GND_S` |  |
| U4 | Diodes AP2112K-3.3TRG1 | AP2112K-3.3 | 1/VIN→`V5S`, 2/GND→`GND_S`, 3/EN→`V5S`, 5/VOUT→`+3V3S` | pin 4 NC |
| C24 | Device:C | 1uF | 1→`+3V3S`, 2→`GND_S` |  |
| D20 | ES1D | ES1D | 1→`V31S`, 2→`T_S31` |  |
| C25 | Device:C_Polarized | 470uF 50V | 1→`V31S`, 2→`GND_S` |  |

## Sheet 4 Inverter (discrete: 3 x IRS2186 + 6 x TO-247 MOSFET), 3-shunt sensing

| Ref | Part | Value | Pins → nets | Note |
|---|---|---|---|---|
| U5 | Infineon IRS2186PBF (DIP-8 in socket) | IRS2186PBF | 1/HIN→`HIN_U`, 2/LIN→`LIN_U`, 3/COM→`PGND`, 4/LO→`LO_U`, 5/VCC→`+15V`, 6/VS→`OUTU`, 7/HO→`HO_U`, 8/VB→`VBOOTU` | COM to the shunt star (PGND), 4 A / 4 A gate drive |
| Q9 | ST STW24N60DM2 (fast body diode) / alt. STGW20H60DF IGBT | 600V 18A | 1/G→`GH_U`, 2/D→`VBUS`, 3/S→`OUTU` | high side, on heatsink with insulating pad |
| Q10 | ST STW24N60DM2 (fast body diode) / alt. STGW20H60DF IGBT | 600V 18A | 1/G→`GL_U`, 2/D→`OUTU`, 3/S→`SH_U` | low side, source to its own shunt |
| R107 | Device:R | 10R | 1→`HO_U`, 2→`GH_U` | gate resistor (tune for dv/dt <= 10 V/ns) |
| R108 | Device:R | 10k | 1→`GH_U`, 2→`OUTU` |  |
| R109 | Device:R | 10R | 1→`LO_U`, 2→`GL_U` | gate resistor |
| R110 | Device:R | 10k | 1→`GL_U`, 2→`SH_U` |  |
| R119 | Device:R | 10R | 1→`+15V`, 2→`BST_U` | bootstrap charge current limit |
| D26 | UF4007 (1000 V, 75 ns) | UF4007 | 1→`VBOOTU`, 2→`BST_U` |  |
| C28 | Device:C | 4.7uF 25V X7R | 1→`VBOOTU`, 2→`OUTU` | bootstrap |
| C29 | Device:C | 100nF | 1→`VBOOTU`, 2→`OUTU` |  |
| C63 | Device:C | 10uF 25V | 1→`+15V`, 2→`PGND` | driver VCC, at the socket |
| C64 | Device:C | 100nF | 1→`+15V`, 2→`PGND` |  |
| U12 | Infineon IRS2186PBF (DIP-8 in socket) | IRS2186PBF | 1/HIN→`HIN_V`, 2/LIN→`LIN_V`, 3/COM→`PGND`, 4/LO→`LO_V`, 5/VCC→`+15V`, 6/VS→`OUTV`, 7/HO→`HO_V`, 8/VB→`VBOOTV` | COM to the shunt star (PGND), 4 A / 4 A gate drive |
| Q11 | ST STW24N60DM2 (fast body diode) / alt. STGW20H60DF IGBT | 600V 18A | 1/G→`GH_V`, 2/D→`VBUS`, 3/S→`OUTV` | high side, on heatsink with insulating pad |
| Q12 | ST STW24N60DM2 (fast body diode) / alt. STGW20H60DF IGBT | 600V 18A | 1/G→`GL_V`, 2/D→`OUTV`, 3/S→`SH_V` | low side, source to its own shunt |
| R111 | Device:R | 10R | 1→`HO_V`, 2→`GH_V` | gate resistor (tune for dv/dt <= 10 V/ns) |
| R112 | Device:R | 10k | 1→`GH_V`, 2→`OUTV` |  |
| R113 | Device:R | 10R | 1→`LO_V`, 2→`GL_V` | gate resistor |
| R114 | Device:R | 10k | 1→`GL_V`, 2→`SH_V` |  |
| R120 | Device:R | 10R | 1→`+15V`, 2→`BST_V` | bootstrap charge current limit |
| D27 | UF4007 (1000 V, 75 ns) | UF4007 | 1→`VBOOTV`, 2→`BST_V` |  |
| C30 | Device:C | 4.7uF 25V X7R | 1→`VBOOTV`, 2→`OUTV` | bootstrap |
| C31 | Device:C | 100nF | 1→`VBOOTV`, 2→`OUTV` |  |
| C65 | Device:C | 10uF 25V | 1→`+15V`, 2→`PGND` | driver VCC, at the socket |
| C66 | Device:C | 100nF | 1→`+15V`, 2→`PGND` |  |
| U13 | Infineon IRS2186PBF (DIP-8 in socket) | IRS2186PBF | 1/HIN→`HIN_W`, 2/LIN→`LIN_W`, 3/COM→`PGND`, 4/LO→`LO_W`, 5/VCC→`+15V`, 6/VS→`OUTW`, 7/HO→`HO_W`, 8/VB→`VBOOTW` | COM to the shunt star (PGND), 4 A / 4 A gate drive |
| Q13 | ST STW24N60DM2 (fast body diode) / alt. STGW20H60DF IGBT | 600V 18A | 1/G→`GH_W`, 2/D→`VBUS`, 3/S→`OUTW` | high side, on heatsink with insulating pad |
| Q14 | ST STW24N60DM2 (fast body diode) / alt. STGW20H60DF IGBT | 600V 18A | 1/G→`GL_W`, 2/D→`OUTW`, 3/S→`SH_W` | low side, source to its own shunt |
| R115 | Device:R | 10R | 1→`HO_W`, 2→`GH_W` | gate resistor (tune for dv/dt <= 10 V/ns) |
| R116 | Device:R | 10k | 1→`GH_W`, 2→`OUTW` |  |
| R117 | Device:R | 10R | 1→`LO_W`, 2→`GL_W` | gate resistor |
| R118 | Device:R | 10k | 1→`GL_W`, 2→`SH_W` |  |
| R121 | Device:R | 10R | 1→`+15V`, 2→`BST_W` | bootstrap charge current limit |
| D28 | UF4007 (1000 V, 75 ns) | UF4007 | 1→`VBOOTW`, 2→`BST_W` |  |
| C32 | Device:C | 4.7uF 25V X7R | 1→`VBOOTW`, 2→`OUTW` | bootstrap |
| C33 | Device:C | 100nF | 1→`VBOOTW`, 2→`OUTW` |  |
| C67 | Device:C | 10uF 25V | 1→`+15V`, 2→`PGND` | driver VCC, at the socket |
| C68 | Device:C | 100nF | 1→`+15V`, 2→`PGND` |  |
| C26 | Device:C | 10uF 25V | 1→`+15V`, 2→`GND` |  |
| C27 | Device:C | 100nF | 1→`+15V`, 2→`GND` |  |
| C34 | Device:C | 1uF 630V | 1→`VBUS`, 2→`PGND` | bus film cap, across the three legs |
| C35 | Device:C | 100nF 630V | 1→`VBUS`, 2→`PGND` | leg U decoupling, < 15 mm from the MOSFETs |
| C40 | Device:C | 100nF 630V | 1→`VBUS`, 2→`PGND` | leg V decoupling, < 15 mm from the MOSFETs |
| C36 | Device:C | 100nF 630V | 1→`VBUS`, 2→`PGND` | leg W decoupling, < 15 mm from the MOSFETs |
| R40 | Device:R | 100R | 1→`PWM_UH`, 2→`HIN_U` |  |
| R41 | Device:R | 10k | 1→`HIN_U`, 2→`GND` |  |
| R42 | Device:R | 100R | 1→`PWM_UL`, 2→`LIN_U` |  |
| R43 | Device:R | 10k | 1→`LIN_U`, 2→`GND` |  |
| R44 | Device:R | 100R | 1→`PWM_VH`, 2→`HIN_V` |  |
| R45 | Device:R | 10k | 1→`HIN_V`, 2→`GND` |  |
| R46 | Device:R | 100R | 1→`PWM_VL`, 2→`LIN_V` |  |
| R47 | Device:R | 10k | 1→`LIN_V`, 2→`GND` |  |
| R48 | Device:R | 100R | 1→`PWM_WH`, 2→`HIN_W` |  |
| R49 | Device:R | 10k | 1→`HIN_W`, 2→`GND` |  |
| R50 | Device:R | 100R | 1→`PWM_WL`, 2→`LIN_W` |  |
| R51 | Device:R | 10k | 1→`LIN_W`, 2→`GND` |  |
| R52 | Device:R | 10k | 1→`+3V3`, 2→`HS_TSW` |  |
| C69 | Device:C | 10nF | 1→`HS_TSW`, 2→`GND` |  |
| R53 | Device:R | 1k | 1→`HS_TSW`, 2→`MCU_BKIN` |  |
| J10 | KSD301 90 C normally closed, on heatsink | TSW 90C NC | 1→`HS_TSW`, 2→`GND` |  |
| R54 | Device:R | 25m 2W | 1→`SH_U`, 2→`PGND` | kelvin to PGND star |
| R57 | Device:R | 1k 0.1% | 1→`SH_U`, 2→`ISENSE_U` |  |
| R60 | Device:R | 10k 0.1% | 1→`ISENSE_U`, 2→`+3V3A` |  |
| C37 | Device:C | 470pF C0G | 1→`ISENSE_U`, 2→`GND` |  |
| R55 | Device:R | 25m 2W | 1→`SH_V`, 2→`PGND` | kelvin to PGND star |
| R58 | Device:R | 1k 0.1% | 1→`SH_V`, 2→`ISENSE_V` |  |
| R61 | Device:R | 10k 0.1% | 1→`ISENSE_V`, 2→`+3V3A` |  |
| C38 | Device:C | 470pF C0G | 1→`ISENSE_V`, 2→`GND` |  |
| R56 | Device:R | 25m 2W | 1→`SH_W`, 2→`PGND` | kelvin to PGND star |
| R59 | Device:R | 1k 0.1% | 1→`SH_W`, 2→`ISENSE_W` |  |
| R62 | Device:R | 10k 0.1% | 1→`ISENSE_W`, 2→`+3V3A` |  |
| C39 | Device:C | 470pF C0G | 1→`ISENSE_W`, 2→`GND` |  |
| NT1 | Device:NetTie_2 | STAR | 1→`PGND`, 2→`GND` | single connection of signal ground to power ground, at the shunt star point |
| J2 | Connector:Screw_Terminal_01x04 | MOTOR U/V/W/PE | 1→`OUTU`, 2→`OUTV`, 3→`OUTW`, 4→`PE` |  |
| R66 | Device:R | 330k 1% | 1→`VBUS`, 2→`VBD1` |  |
| R67 | Device:R | 330k 1% | 1→`VBD1`, 2→`VBD2` |  |
| R68 | Device:R | 330k 1% | 1→`VBD2`, 2→`VBD3` |  |
| R69 | Device:R | 6.65k 0.1% | 1→`VBD3`, 2→`GND` |  |
| C41 | Device:C | 1nF | 1→`VBD3`, 2→`GND` |  |
| R70 | Device:R | 1k | 1→`VBD3`, 2→`VBUS_SENSE` |  |
| J3 | screw-lug NTC 10k B3435 on heatsink | NTC 10k B3435 | 1→`NTC_HS`, 2→`GND` |  |
| R71 | Device:R | 10k 1% | 1→`+3V3A`, 2→`NTC_HS` |  |
| C42 | Device:C | 100nF | 1→`NTC_HS`, 2→`GND` |  |

## Sheet 5 MCU STM32G431CBT6 (hot side)

| Ref | Part | Value | Pins → nets | Note |
|---|---|---|---|---|
| U6 | ST STM32G431CBT6 | STM32G431CBT6 | 1/VBAT→`+3V3`, 2/PC13→`NC_PC13`, 3/PC14→`NC_PC14`, 4/PC15→`NC_PC15`, 5/PF0→`NC_PF0`, 6/PF1→`NC_PF1`, 7/PG10→`NRST`, 8/PA0→`VBUS_SENSE`, 9/PA1→`ISENSE_U`, 10/PA2→`VIN_L`, 11/PA3→`VIN_N`, 12/PA4→`TP_DAC`, 13/PA5→`NC_PA5`, 14/PA6→`NTC_HS`, 15/PA7→`ISENSE_V`, 16/PB0→`ISENSE_W`, 17/PB1→`NC_PB1`, 18/PB2→`NC_PB2`, 19/VSSA→`GND`, 20/VREF+→`+3V3A`, 21/VDDA→`+3V3A`, 22/PB10→`K3_ON`, 23/VSS→`GND`, 24/VDD→`+3V3`, 25/PB11→`K4_ON`, 26/PB12→`MCU_BKIN`, 27/PB13→`PWM_UL`, 28/PB14→`PWM_VL`, 29/PB15→`PWM_WL`, 30/PA8→`PWM_UH`, 31/PA9→`PWM_VH`, 32/PA10→`PWM_WH`, 33/PA11→`PFC_OFF`, 34/PA12→`LED_STAT`, 35/VSS→`GND`, 36/VDD→`+3V3`, 37/PA13→`SWDIO`, 38/PA14→`SWCLK`, 39/PA15→`NC_PA15`, 40/PB3→`SWO`, 41/PB4→`K1_ON`, 42/PB5→`K2_ON`, 43/PB6→`UART_H_TX`, 44/PB7→`UART_H_RX`, 45/PB8→`BOOT0`, 46/PB9→`NC_PB9`, 47/VSS→`GND`, 48/VDD→`+3V3` |  |
| C43 | Device:C | 100nF | 1→`+3V3`, 2→`GND` |  |
| C44 | Device:C | 100nF | 1→`+3V3`, 2→`GND` |  |
| C45 | Device:C | 100nF | 1→`+3V3`, 2→`GND` |  |
| C46 | Device:C | 4.7uF | 1→`+3V3`, 2→`GND` |  |
| C47 | Device:C | 1uF | 1→`+3V3A`, 2→`GND` |  |
| C48 | Device:C | 100nF | 1→`+3V3A`, 2→`GND` |  |
| C49 | Device:C | 100nF | 1→`NRST`, 2→`GND` |  |
| R72 | Device:R | 10k | 1→`BOOT0`, 2→`GND` |  |
| R73 | Device:R | 1k | 1→`LED_STAT`, 2→`LED2_A` |  |
| LED2 | Device:LED | green | 1→`GND`, 2→`LED2_A` |  |
| TP1 | Connector:TestPoint | DAC | 1→`TP_DAC` |  |
| J4 | Connector_Generic:Conn_02x05_Odd_Even | SWD (HOT!) | 1→`+3V3`, 2→`SWDIO`, 3→`GND`, 4→`SWCLK`, 5→`GND`, 6→`SWO`, 7→`NC_J4_7`, 8→`NC_J4_8`, 9→`GND`, 10→`NRST` | mains referenced: isolated debugger or isolation transformer only |

## Sheet 6 Relay drivers, pump and drain outputs (220 VAC)

| Ref | Part | Value | Pins → nets | Note |
|---|---|---|---|---|
| R74 | Device:R | 68R 0.5W | 1→`+15V`, 2→`K1_A1` | drops 15 V to the 12 V coil (verify coil R) |
| Q4 | 2N7002 | 2N7002 | 1/G→`K1_G`, 2/S→`GND`, 3/D→`K1_DRV` |  |
| R75 | Device:R | 100R | 1→`K1_ON`, 2→`K1_G` |  |
| R76 | Device:R | 100k | 1→`K1_G`, 2→`GND` |  |
| D21 | 1N4148W | 1N4148W | 1→`K1_A1`, 2→`K1_DRV` |  |
| R77 | Device:R | 220R | 1→`+15V`, 2→`K2_A1` | drops 15 V to the 12 V coil (verify coil R) |
| Q5 | 2N7002 | 2N7002 | 1/G→`K2_G`, 2/S→`GND`, 3/D→`K2_DRV` |  |
| R78 | Device:R | 100R | 1→`K2_ON`, 2→`K2_G` |  |
| R79 | Device:R | 100k | 1→`K2_G`, 2→`GND` |  |
| D22 | 1N4148W | 1N4148W | 1→`K2_A1`, 2→`K2_DRV` |  |
| R80 | Device:R | 220R | 1→`+15V`, 2→`K3_A1` | drops 15 V to the 12 V coil (verify coil R) |
| Q6 | 2N7002 | 2N7002 | 1/G→`K3_G`, 2/S→`GND`, 3/D→`K3_DRV` |  |
| R81 | Device:R | 100R | 1→`K3_ON`, 2→`K3_G` |  |
| R82 | Device:R | 100k | 1→`K3_G`, 2→`GND` |  |
| D23 | 1N4148W | 1N4148W | 1→`K3_A1`, 2→`K3_DRV` |  |
| R83 | Device:R | 220R | 1→`+15V`, 2→`K4_A1` | drops 15 V to the 12 V coil (verify coil R) |
| Q7 | 2N7002 | 2N7002 | 1/G→`K4_G`, 2/S→`GND`, 3/D→`K4_DRV` |  |
| R84 | Device:R | 100R | 1→`K4_ON`, 2→`K4_G` |  |
| R85 | Device:R | 100k | 1→`K4_G`, 2→`GND` |  |
| D24 | 1N4148W | 1N4148W | 1→`K4_A1`, 2→`K4_DRV` |  |
| F2 | 5x20 2 A time-lag | 2A T | 1→`L_SW`, 2→`L_LOAD` | pump + drain branch |
| K3 | Finder 34.51.7.012.0010 | Finder 34.51.7.012 | A1→`K3_A1`, A2→`K3_DRV`, 11→`L_LOAD`, 14→`PUMP_L`, 12→`NC_K3` | water pump 220 VAC |
| K4 | Finder 34.51.7.012.0010 | Finder 34.51.7.012 | A1→`K4_A1`, A2→`K4_DRV`, 11→`L_LOAD`, 14→`DRAIN_L`, 12→`NC_K4` | drain valve 220 VAC |
| J5 | Connector:Screw_Terminal_01x04 | PUMP L/N DRAIN L/N | 1→`PUMP_L`, 2→`AC_NF`, 3→`DRAIN_L`, 4→`AC_NF` |  |
| C50 (DNP) | Device:C | 47nF X2 | 1→`PUMP_L`, 2→`PUMP_SN` | optional RC snubber |
| R86 | Device:R | 100R 1W | 1→`PUMP_SN`, 2→`AC_NF` |  |
| C51 (DNP) | Device:C | 47nF X2 | 1→`DRAIN_L`, 2→`DRAIN_SN` | optional RC snubber |
| R87 | Device:R | 100R 1W | 1→`DRAIN_SN`, 2→`AC_NF` |  |

## Sheet 7 Isolated side: isolator, PHY MCU, 2-wire panel bus, RS-485 to battery module

| Ref | Part | Value | Pins → nets | Note |
|---|---|---|---|---|
| U7 | TI ISO7762DWR (reinforced 5 kVrms, SOIC-16 wide) | ISO7762DW | 1/Vcc1→`+3V3`, 2/INA→`UART_H_TX`, 3→`GND`, 4→`GND`, 5→`GND`, 6/OUTE→`UART_H_RX`, 7→`NC_ISO_OUTF`, 8/GND1→`GND`, 9/GND2→`GND_S`, 10→`GND_S`, 11/INE→`UART_S_TX`, 12→`NC_ISO_OUTD`, 13→`NC_ISO_OUTC`, 14→`NC_ISO_OUTB`, 15/OUTA→`UART_S_RX`, 16/Vcc2→`+3V3S` | ch A hot->SELV, ch E SELV->hot; unused inputs grounded; slot under the package, 8 mm creepage |
| C52 | Device:C | 100nF | 1→`+3V3`, 2→`GND` |  |
| C53 | Device:C | 100nF | 1→`+3V3S`, 2→`GND_S` |  |
| U8 | ST STM32C011F6P6 | STM32C011F6P6 | 1/PB7→`UART_S_RX`, 2→`NC_S14`, 3→`NC_S15`, 4/VDD→`+3V3S`, 5/VSS→`GND_S`, 6/PF2→`NRST_S`, 7/PA0→`LED_S`, 8/PA1→`RS485_DE`, 9/PA2→`RS485_DI`, 10/PA3→`RS485_RO`, 11/PA4→`PB_TX`, 12/PA5→`PB_ISNS`, 13/PA6→`PB_VSNS`, 14→`NC_S7`, 15→`NC_S8`, 16→`NC_S16`, 17→`NC_S17`, 18/PA13→`SWDIO_S`, 19/PA14→`SWCLK_S`, 20/PB6→`UART_S_TX` |  |
| C54 | Device:C | 100nF | 1→`+3V3S`, 2→`GND_S` |  |
| C55 | Device:C | 1uF | 1→`+3V3S`, 2→`GND_S` |  |
| C56 | Device:C | 100nF | 1→`NRST_S`, 2→`GND_S` |  |
| R88 | Device:R | 1k | 1→`LED_S`, 2→`LED3_A` |  |
| LED3 | Device:LED | yellow | 1→`GND_S`, 2→`LED3_A` |  |
| J6 | Connector_Generic:Conn_01x05 | SWD SELV | 1→`+3V3S`, 2→`SWDIO_S`, 3→`SWCLK_S`, 4→`NRST_S`, 5→`GND_S` |  |
| U9 | LM317T (small heatsink) | LM317 CC | 1/ADJ→`PB_CC`, 2/VO→`PB_CCO`, 3/VI→`V31S` |  |
| R89 | Device:R | 8.2R 0.5W | 1→`PB_CCO`, 2→`PB_CC` | 1.25 V / 8.2 R = 150 mA short-circuit limit |
| U10 | LM317T | LM317 REG | 1/ADJ→`PB_ADJ`, 2/VO→`PB_REG`, 3/VI→`PB_CC` |  |
| R90 | Device:R | 240R 1% | 1→`PB_REG`, 2→`PB_ADJ` |  |
| R91 | Device:R | 4.3k 1% | 1→`PB_ADJ`, 2→`GND_S` | 24 V mark |
| R92 | Device:R | 8.2k 1% | 1→`PB_ADJ`, 2→`PB_TXD` | switched in parallel -> 16 V space |
| Q8 | 2N7002 | 2N7002 | 1/G→`PB_TXG`, 2/S→`GND_S`, 3/D→`PB_TXD` |  |
| R93 | Device:R | 100R | 1→`PB_TX`, 2→`PB_TXG` |  |
| R94 | Device:R | 100k | 1→`PB_TXG`, 2→`GND_S` |  |
| C57 | Device:C | 1uF 50V | 1→`V31S`, 2→`GND_S` |  |
| C58 | Device:C | 10uF 35V | 1→`PB_REG`, 2→`GND_S` |  |
| F3 | Bourns MF-MSMF010 | 0.1A hold | 1→`PB_REG`, 2→`PANEL_P` |  |
| D25 | SMBJ33CA | SMBJ33CA | 1→`PANEL_P`, 2→`PANEL_N` |  |
| R95 | Device:R | 4.7R 1% | 1→`PANEL_N`, 2→`GND_S` | return current sense |
| R96 | Device:R | 1k | 1→`PANEL_N`, 2→`PB_ISNS` |  |
| C59 | Device:C | 10nF | 1→`PB_ISNS`, 2→`GND_S` |  |
| R97 | Device:R | 100k 1% | 1→`PANEL_P`, 2→`PB_VSNS` |  |
| R98 | Device:R | 10k 1% | 1→`PB_VSNS`, 2→`GND_S` |  |
| J7 | Connector:Screw_Terminal_01x02 | PANEL 2-WIRE | 1→`PANEL_P`, 2→`PANEL_N` | SELV: 24 V mark / 16 V space; panel is polarity-free |
| U11 (DNP) | MaxLinear SP3485EN (3.3 V half duplex) | SP3485EN | 1/RO→`RS485_RO`, 2/~{RE}→`RS485_DE`, 3/DE→`RS485_DE`, 4/DI→`RS485_DI`, 5/GND→`GND_S`, 6/A→`RS485_A`, 7/B→`RS485_B`, 8/VCC→`+3V3S` | OPTION battery module |
| C60 (DNP) | Device:C | 100nF | 1→`+3V3S`, 2→`GND_S` | OPTION battery module |
| R99 (DNP) | Device:R | 120R | 1→`RS485_A`, 2→`RS485_T` | OPTION battery module: termination |
| JP1 (DNP) | Jumper:SolderJumper_2_Open | TERM | 1→`RS485_T`, 2→`RS485_B` | OPTION battery module |
| J8 (DNP) | Connector:Screw_Terminal_01x03 | BATT MODULE RS-485 | 1→`RS485_A`, 2→`RS485_B`, 3→`GND_S` | OPTION battery module |

## Sheet 8 OPTION battery module (DNP in the mains-only version), mechanics, test points

| Ref | Part | Value | Pins → nets | Note |
|---|---|---|---|---|
| F4 (DNP) | 5x20 ceramic, DC rated >= 500 V (e.g. Littelfuse 505 series) | 5A 500VDC | 1→`VBUS`, 2→`VBUS_EXT` | OPTION battery module |
| J9 (DNP) | Connector:Screw_Terminal_01x03 | BATT MODULE DC BUS | 1→`VBUS_EXT`, 2→`NC_J9`, 3→`PGND` | OPTION battery module; middle position empty for creepage; module does its own pre-charge |
| H1 | Mechanical:MountingHole | M3 |  | non-plated: no creepage path from mounting screws |
| H2 | Mechanical:MountingHole | M3 |  | non-plated: no creepage path from mounting screws |
| H3 | Mechanical:MountingHole | M3 |  | non-plated: no creepage path from mounting screws |
| H4 | Mechanical:MountingHole | M3 |  | non-plated: no creepage path from mounting screws |
| FID1 | Mechanical:Fiducial | FID |  |  |
| FID2 | Mechanical:Fiducial | FID |  |  |
| FID3 | Mechanical:Fiducial | FID |  |  |
| TP2 | Connector:TestPoint | +15V | 1→`+15V` |  |
| TP3 | Connector:TestPoint | +3V3 | 1→`+3V3` |  |
| TP4 | Connector:TestPoint | GND | 1→`GND` |  |
| TP5 | Connector:TestPoint | PFC_DRV | 1→`PFC_DRV` |  |
| TP6 | Connector:TestPoint | ISENSE_U | 1→`ISENSE_U` |  |
| TP7 | Connector:TestPoint | UART_H_TX | 1→`UART_H_TX` |  |
| TP8 | Connector:TestPoint | +3V3S | 1→`+3V3S` |  |
| TP9 | Connector:TestPoint | GND_S | 1→`GND_S` |  |
| TP10 | Connector:TestPoint | PANEL_P | 1→`PANEL_P` |  |

## Net index

| Net | Nodes |
|---|---|
| `+15V` | R17.1 D16.1 C15.1 D17.2 R36.1 D18.1 U3.2 U3.3 C19.1 U5.5 R119.1 C63.1 C64.1 U12.5 R120.1 C65.1 C66.1 U13.5 R121.1 C67.1 C68.1 C26.1 C27.1 R74.1 R77.1 R80.1 R83.1 TP2.1 |
| `+3V3` | U3.1 L3.2 C21.1 FB1.1 R52.1 U6.1 U6.24 U6.36 U6.48 C43.1 C44.1 C45.1 C46.1 J4.1 U7.1 C52.1 TP3.1 |
| `+3V3A` | D4.2 D5.2 FB1.2 C22.1 R60.2 R61.2 R62.2 R71.1 U6.20 U6.21 C47.1 C48.1 |
| `+3V3S` | U4.5 C24.1 U7.16 C53.1 U8.4 C54.1 C55.1 J6.1 U11.8 C60.1 TP8.1 |
| `AC_L` | J1.1 F1.1 |
| `AC_LF` | L1.2 CX2.1 CY1.1 K1.11 K2.11 D1.2 D3.1 R3.1 |
| `AC_LF0` | F1.2 RV1.1 RV2.1 CX1.1 R1.1 L1.1 |
| `AC_N` | J1.2 RV1.2 RV3.1 CX1.2 R2.2 L1.3 |
| `AC_NF` | L1.4 CX2.2 CY2.1 D2.2 R7.1 BR1.3 J5.2 J5.4 R86.2 R87.2 |
| `AUX_HV` | Q3.3 D13.2 C14.1 T1.1 D15.1 |
| `AUX_RAW` | D1.1 D2.1 C1.1 D29.1 Q3.2 R32.1 |
| `BLD1` | R29.2 R30.1 |
| `BLD2` | R30.2 R31.1 |
| `BLD3` | R31.2 LED1.2 |
| `BOOT0` | U6.45 R72.1 |
| `BST_U` | R119.2 D26.2 |
| `BST_V` | R120.2 D27.2 |
| `BST_W` | R121.2 D28.2 |
| `BUCK_BST` | U3.6 C20.1 |
| `BUCK_SW` | U3.5 C20.2 L3.1 |
| `DRAIN_L` | K4.14 J5.3 C51.1 |
| `DRAIN_SN` | C51.2 R87.1 |
| `GH_U` | Q9.1 R107.2 R108.1 |
| `GH_V` | Q11.1 R111.2 R112.1 |
| `GH_W` | Q13.1 R115.2 R116.1 |
| `GL_U` | Q10.1 R109.2 R110.1 |
| `GL_V` | Q12.1 R113.2 R114.1 |
| `GL_W` | Q14.1 R117.2 R118.1 |
| `GND` | R6.2 D4.1 C2.2 R10.2 D5.1 C3.2 R28.2 U3.4 C19.2 C21.2 C22.2 C26.2 C27.2 R41.2 R43.2 R45.2 R47.2 R49.2 R51.2 C69.2 J10.2 C37.2 C38.2 C39.2 NT1.2 R69.2 C41.2 J3.2 C42.2 U6.19 U6.23 U6.35 U6.47 C43.2 C44.2 C45.2 C46.2 C47.2 C48.2 C49.2 R72.2 LED2.1 J4.3 J4.5 J4.9 Q4.2 R76.2 Q5.2 R79.2 Q6.2 R82.2 Q7.2 R85.2 U7.3 U7.4 U7.5 U7.8 C52.2 TP4.1 |
| `GND_S` | T1.6 C23.2 U4.2 C24.2 C25.2 U7.9 U7.10 C53.2 U8.5 C54.2 C55.2 C56.2 LED3.1 J6.5 R91.2 Q8.2 R94.2 C57.2 C58.2 R95.2 C59.2 R98.2 U11.5 C60.2 J8.3 TP9.1 |
| `HIN_U` | U5.1 R40.2 R41.1 |
| `HIN_V` | U12.1 R44.2 R45.1 |
| `HIN_W` | U13.1 R48.2 R49.1 |
| `HO_U` | U5.7 R107.1 |
| `HO_V` | U12.7 R111.1 |
| `HO_W` | U13.7 R115.1 |
| `HS_TSW` | R52.2 C69.1 R53.1 J10.1 |
| `ISENSE_U` | R57.2 R60.1 C37.1 U6.9 TP6.1 |
| `ISENSE_V` | R58.2 R61.1 C38.1 U6.15 |
| `ISENSE_W` | R59.2 R62.1 C39.1 U6.16 |
| `K1_A1` | K1.A1 R74.2 D21.1 |
| `K1_DRV` | K1.A2 Q4.3 D21.2 |
| `K1_G` | Q4.1 R75.2 R76.1 |
| `K1_ON` | U6.41 R75.1 |
| `K2_A1` | K2.A1 R77.2 D22.1 |
| `K2_DRV` | K2.A2 Q5.3 D22.2 |
| `K2_G` | Q5.1 R78.2 R79.1 |
| `K2_ON` | U6.42 R78.1 |
| `K3_A1` | R80.2 D23.1 K3.A1 |
| `K3_DRV` | Q6.3 D23.2 K3.A2 |
| `K3_G` | Q6.1 R81.2 R82.1 |
| `K3_ON` | U6.22 R81.1 |
| `K4_A1` | R83.2 D24.1 K4.A1 |
| `K4_DRV` | Q7.3 D24.2 K4.A2 |
| `K4_G` | Q7.1 R84.2 R85.1 |
| `K4_ON` | U6.25 R84.1 |
| `LED2_A` | R73.2 LED2.2 |
| `LED3_A` | R88.2 LED3.2 |
| `LED_S` | U8.7 R88.1 |
| `LED_STAT` | U6.34 R73.1 |
| `LIN_U` | U5.2 R42.2 R43.1 |
| `LIN_V` | U12.2 R46.2 R47.1 |
| `LIN_W` | U13.2 R50.2 R51.1 |
| `LO_U` | U5.4 R109.1 |
| `LO_V` | U12.4 R113.1 |
| `LO_W` | U13.4 R117.1 |
| `L_LOAD` | F2.2 K3.11 K4.11 |
| `L_SW` | K1.14 PTC1.2 BR1.2 F2.1 |
| `MCU_BKIN` | R53.2 U6.26 |
| `NC_ISO_OUTB` | U7.14 |
| `NC_ISO_OUTC` | U7.13 |
| `NC_ISO_OUTD` | U7.12 |
| `NC_ISO_OUTF` | U7.7 |
| `NC_J4_7` | J4.7 |
| `NC_J4_8` | J4.8 |
| `NC_J9` | J9.2 |
| `NC_K1` | K1.12 |
| `NC_K2` | K2.12 |
| `NC_K3` | K3.12 |
| `NC_K4` | K4.12 |
| `NC_PA15` | U6.39 |
| `NC_PA5` | U6.13 |
| `NC_PB1` | U6.17 |
| `NC_PB2` | U6.18 |
| `NC_PB9` | U6.46 |
| `NC_PC13` | U6.2 |
| `NC_PC14` | U6.3 |
| `NC_PC15` | U6.4 |
| `NC_PF0` | U6.5 |
| `NC_PF1` | U6.6 |
| `NC_S14` | U8.2 |
| `NC_S15` | U8.3 |
| `NC_S16` | U8.16 |
| `NC_S17` | U8.17 |
| `NC_S7` | U8.14 |
| `NC_S8` | U8.15 |
| `NRST` | U6.7 C49.1 J4.10 |
| `NRST_S` | U8.6 C56.1 J6.4 |
| `NTC_HS` | J3.1 R71.2 C42.1 U6.14 |
| `OUTU` | U5.6 Q9.3 Q10.2 R108.2 C28.2 C29.2 J2.1 |
| `OUTV` | U12.6 Q11.3 Q12.2 R112.2 C30.2 C31.2 J2.2 |
| `OUTW` | U13.6 Q13.3 Q14.2 R116.2 C32.2 C33.2 J2.3 |
| `P6K_A` | D14.1 D15.2 |
| `PANEL_N` | D25.2 R95.1 R96.1 J7.2 |
| `PANEL_P` | F3.2 D25.1 R97.1 J7.1 TP10.1 |
| `PB_ADJ` | U10.1 R90.2 R91.1 R92.1 |
| `PB_CC` | U9.1 R89.2 U10.3 |
| `PB_CCO` | U9.2 R89.1 |
| `PB_ISNS` | U8.12 R96.2 C59.1 |
| `PB_REG` | U10.2 R90.1 C58.1 F3.1 |
| `PB_TX` | U8.11 R93.1 |
| `PB_TXD` | R92.2 Q8.3 |
| `PB_TXG` | Q8.1 R93.2 R94.1 |
| `PB_VSNS` | U8.13 R97.2 R98.1 |
| `PE` | J1.3 GDT1.3 CY1.2 CY2.2 J2.4 |
| `PFC_CS` | U1.4 R26.2 C9.1 |
| `PFC_DRV` | R13.1 U1.7 TP5.1 |
| `PFC_FB` | U1.1 R21.2 C7.1 Q2.3 |
| `PFC_FBD1` | R18.2 R19.1 |
| `PFC_FBD2` | R19.2 R20.1 |
| `PFC_FBD3` | R20.2 R21.1 |
| `PFC_G` | Q1.1 R13.2 R14.1 D8.1 |
| `PFC_IN` | U1.3 R25.2 C8.1 |
| `PFC_IND1` | R22.2 R23.1 |
| `PFC_IND2` | R23.2 R24.1 |
| `PFC_IND3` | R24.2 R25.1 |
| `PFC_OFF` | Q2.1 R28.1 U6.33 |
| `PFC_RTN` | BR1.4 C4.2 R15.1 R16.1 R26.1 |
| `PFC_VCC` | U1.8 R17.2 C5.1 C6.1 |
| `PFC_VCTRL` | U1.2 C11.1 |
| `PFC_VM` | U1.5 R27.1 C10.1 |
| `PGND` | D3.2 C1.2 Q1.3 R14.2 D8.2 R15.2 R16.2 U1.6 C5.2 C6.2 C7.2 C8.2 C9.2 R27.2 C10.2 C11.2 Q2.2 C12.2 C13.2 LED1.1 D12.2 C14.2 T1.5 U2.1 C15.2 C16.2 R37.2 C17.2 C18.2 R39.2 D18.2 U5.3 C63.2 C64.2 U12.3 C65.2 C66.2 U13.3 C67.2 C68.2 C34.2 C35.2 C40.2 C36.2 R54.2 R55.2 R56.2 NT1.1 J9.3 |
| `PREG1` | R32.2 R33.1 |
| `PREG2` | R33.2 R34.1 |
| `PREG3` | R34.2 R35.1 |
| `PREZ1` | D9.2 D10.1 |
| `PREZ2` | D10.2 D11.1 |
| `PREZ3` | D11.2 D12.1 |
| `PRE_G` | Q3.1 R35.2 D9.1 D13.1 |
| `PRE_L` | K2.14 PTC1.1 |
| `PUMP_L` | K3.14 J5.1 C50.1 |
| `PUMP_SN` | C50.2 R86.1 |
| `PWM_UH` | R40.1 U6.30 |
| `PWM_UL` | R42.1 U6.27 |
| `PWM_VH` | R44.1 U6.31 |
| `PWM_VL` | R46.1 U6.28 |
| `PWM_WH` | R48.1 U6.32 |
| `PWM_WL` | R50.1 U6.29 |
| `RECT_P` | BR1.1 C4.1 L2.1 D7.2 R22.1 |
| `RS485_A` | U11.6 R99.1 J8.1 |
| `RS485_B` | U11.7 JP1.2 J8.2 |
| `RS485_DE` | U8.8 U11.2 U11.3 |
| `RS485_DI` | U8.9 U11.4 |
| `RS485_RO` | U8.10 U11.1 |
| `RS485_T` | R99.2 JP1.1 |
| `SH_U` | Q10.3 R110.2 R54.1 R57.1 |
| `SH_V` | Q12.3 R114.2 R55.1 R58.1 |
| `SH_W` | Q14.3 R118.2 R56.1 R59.1 |
| `SURGE_PE` | RV2.2 RV3.2 GDT1.1 |
| `SWCLK` | U6.38 J4.4 |
| `SWCLK_S` | U8.19 J6.3 |
| `SWDIO` | U6.37 J4.2 |
| `SWDIO_S` | U8.18 J6.2 |
| `SWO` | U6.40 J4.6 |
| `SW_PFC` | L2.2 D6.2 Q1.2 |
| `TP_DAC` | U6.12 TP1.1 |
| `T_H15` | T1.4 D16.2 |
| `T_S31` | T1.8 D20.2 |
| `T_S5` | T1.7 D19.2 |
| `UART_H_RX` | U6.44 U7.6 |
| `UART_H_TX` | U6.43 U7.2 TP7.1 |
| `UART_S_RX` | U7.15 U8.1 |
| `UART_S_TX` | U7.11 U8.20 |
| `V31S` | D20.1 C25.1 U9.3 C57.1 |
| `V5S` | D19.1 C23.1 U4.1 U4.3 |
| `VBD1` | R66.2 R67.1 |
| `VBD2` | R67.2 R68.1 |
| `VBD3` | R68.2 R69.1 C41.1 R70.1 |
| `VBOOTU` | U5.8 D26.1 C28.1 C29.1 |
| `VBOOTV` | U12.8 D27.1 C30.1 C31.1 |
| `VBOOTW` | U13.8 D28.1 C32.1 C33.1 |
| `VBUS` | D29.2 D6.1 D7.1 R18.1 C12.1 C13.1 R29.1 Q9.2 Q11.2 Q13.2 C34.1 C35.1 C40.1 C36.1 R66.1 F4.1 |
| `VBUS_EXT` | F4.2 J9.1 |
| `VBUS_SENSE` | R70.2 U6.8 |
| `VIN_L` | R11.2 U6.10 |
| `VIN_L_A` | R3.2 R4.1 |
| `VIN_L_B` | R4.2 R5.1 |
| `VIN_L_C` | R5.2 R6.1 D4.3 C2.1 R11.1 |
| `VIN_N` | R12.2 U6.11 |
| `VIN_N_A` | R7.2 R8.1 |
| `VIN_N_B` | R8.2 R9.1 |
| `VIN_N_C` | R9.2 R10.1 D5.3 C3.1 R12.1 |
| `VIP_COMP` | U2.5 R38.1 C18.1 |
| `VIP_CZ` | R38.2 C17.1 |
| `VIP_D` | T1.2 U2.7 U2.8 D14.2 |
| `VIP_FB` | U2.4 R36.2 R37.1 |
| `VIP_LIM` | U2.3 R39.1 |
| `VIP_VDD` | U2.2 D17.1 C16.1 |
| `XDIS` | R1.2 R2.1 |
