#!/usr/bin/env python3
"""
bldc_calc.py - Design calculations for the 310 V DC BLDC drive.

All inputs are in the INPUT block. Values marked [DS] are datasheet-type
numbers for the IPM / parts; replace them with the figures of the exact
part you buy and re-run. Writes hardware/calc/results.md.

    python3 hardware/calc/bldc_calc.py
"""
import math, os

# ============================ INPUT ============================
P_SHAFT   = 750.0      # W, rated mechanical output
ETA_MOTOR = 0.88
ETA_DRIVE = 0.97
VAC_NOM, VAC_MIN, VAC_MAX = 230.0, 195.0, 264.0
F_LINE    = 50.0
PF_INPUT  = 0.60       # capacitor-input rectifier without PFC
RPM_RATED = 3000.0
POLE_PAIRS = 4
KE_LL     = 0.573      # V/(rad/s) line-line peak (motor)
R_PH      = 1.5        # ohm per phase
L_PH      = 8e-3       # H per phase

F_SW      = 16e3
D_MAX     = 0.96

# IPM (STGIB15CH60TS-L class, 600 V / 15 A) [DS]
VCE0, RCE = 0.80, 0.050     # Vce(I) = VCE0 + RCE*I
VF0,  RF  = 0.90, 0.055     # Vf(I)  = VF0 + RF*I
ESW_REF   = 0.60e-3         # J, Eon+Eoff at 15 A / 300 V
ERR_REF   = 0.10e-3         # J, diode reverse recovery at 15 A / 300 V
I_REF, V_REF = 15.0, 300.0
RTH_JC_IGBT = 3.0           # K/W per IGBT
RTH_CS      = 0.20          # K/W, module to heatsink (thermal paste)
TJ_DESIGN   = 125.0         # deg C design limit (abs max 150)
TA_MAX      = 50.0          # deg C ambient inside enclosure
QG_HS       = 60e-9         # C, high-side gate charge incl. level shifter
I_QBS       = 150e-6        # A, high-side floating quiescent current
R_BOOT      = 50.0          # ohm, bootstrap path (integrated diode + R)
VCC         = 15.0
VBS_UVLO    = 11.5          # V, high-side under-voltage lockout (falling)

# Bridge rectifier (GBU1006 class)
VF_BRIDGE = 0.95
# Bulk capacitors
C_BULK = 2 * 470e-6
# Shunt / sensing
R_SHUNT = 0.033
PGA = 4.0
V_CIN_TRIP = 0.50           # IPM CIN comparator threshold [DS]
I_CBC = 10.0
VDIV_TOP, VDIV_BOT = 3 * 330e3, 7.5e3
C_BEMF = 220e-12
ADC_FS, VREF = 4095, 3.3
# Aux supply loads (mA at 15 V)
AUX_LOADS = {
    'IPM VCC + gate drive': 8, 'MCU 3.3 V via buck (40 mA @3.3 V, eta 0.8)': 11,
    'relay coil (12 V, 30 mA)': 30, 'LEDs': 6, 'isolator (field side)': 3,
}
# ===============================================================

out = []
def h(t): out.append(f'\n### {t}\n\n| quantity | value | note |\n|---|---|---|')
def r(q, v, n=''): out.append(f'| {q} | {v} | {n} |')

# --- power flow ---
p_motor_in = P_SHAFT / ETA_MOTOR
p_in = p_motor_in / ETA_DRIVE
vbus_nom = VAC_NOM * math.sqrt(2)
vbus_min = VAC_MIN * math.sqrt(2)
vbus_max = VAC_MAX * math.sqrt(2)
h('1. Power flow and DC bus')
r('motor input power', f'{p_motor_in:.0f} W', f'shaft {P_SHAFT:.0f} W / eta {ETA_MOTOR}')
r('drive input power', f'{p_in:.0f} W')
r('Vbus nominal / min / max (no load)', f'{vbus_nom:.0f} / {vbus_min:.0f} / {vbus_max:.0f} V', '230 VAC -15 %/+15 %')
i_ac = p_in / (VAC_MIN * PF_INPUT)
r('AC input current (worst, 195 VAC)', f'{i_ac:.1f} A rms', f'PF {PF_INPUT} without PFC -> fuse 10 A T, CM choke >= 8 A')
i_dc = p_motor_in / vbus_nom
r('DC-link average current', f'{i_dc:.2f} A')

# --- bulk capacitor ---
dv = i_dc / (2 * F_LINE * C_BULK)
h('2. Bulk capacitor')
r('C bulk', f'{C_BULK*1e6:.0f} uF', '2 x 470 uF / 450 V, 105 C, snap-in')
r('100 Hz ripple (p-p) at rated power', f'{dv:.0f} V', f'valley ~{vbus_nom - dv:.0f} V')
i_cap = math.sqrt(max(i_ac**2 - i_dc**2, 0))
r('capacitor ripple current (100 Hz part)', f'{i_cap:.1f} A rms', f'{i_cap/2:.1f} A per cap -> each cap rated >= {math.ceil(i_cap/2*10)/10:.1f} A @100 Hz/105 C (or 3 x 330 uF)')
e_cap = 0.5 * C_BULK * vbus_max**2
r('stored energy at 264 VAC', f'{e_cap:.0f} J', 'inrush NTC must be rated for this C at 264 VAC')
r('inrush peak with 5 ohm NTC (cold)', f'{vbus_max/5:.0f} A', 'bridge IFSM (GBU1006: 220 A) OK; relay bypasses NTC after charge')
r('bleeder 2 x 100 k (series) time to < 60 V', f'{-200e3*C_BULK*math.log(60/vbus_max):.0f} s', f'{vbus_max**2/200e3:.2f} W total')

# --- phase current ---
d_rated = min((KE_LL * RPM_RATED * 2 * math.pi / 60 + 2 * R_PH * 4) / vbus_nom, D_MAX)
i_ph = p_motor_in / (vbus_nom * d_rated)
i_ph_rms = i_ph * math.sqrt(2 / 3)
h('3. Phase current (six-step, 120 deg conduction)')
r('duty at rated speed', f'{d_rated:.2f}', 'BEMF + IR drop')
r('flat-top phase current', f'{i_ph:.2f} A', 'I = P / (Vbus * d)')
r('phase current rms', f'{i_ph_rms:.2f} A', 'I * sqrt(2/3)')
r('bus BEMF line peak at rated speed', f'{KE_LL*RPM_RATED*2*math.pi/60:.0f} V')

# --- IPM losses ---
vce = VCE0 + RCE * i_ph
vf = VF0 + RF * i_ph
p_cond = i_ph * (d_rated * vce + (1 - d_rated) * vf + vce)
k = (i_ph / I_REF) * (vbus_nom / V_REF)
p_sw = (ESW_REF + ERR_REF) * k * F_SW
p_ipm = p_cond + p_sw
h('4. IPM losses and heatsink')
r('conduction loss (whole module)', f'{p_cond:.1f} W', 'PWM-leg IGBT d, its low diode 1-d, low-leg IGBT 100 %')
r('switching + recovery loss', f'{p_sw:.1f} W', 'one leg switches at a time')
r('total IPM loss', f'{p_ipm:.1f} W')
p_bridge = 2 * VF_BRIDGE * i_dc
r('bridge rectifier loss', f'{p_bridge:.1f} W', 'mount on the same heatsink')
p_hot = max(vce * i_ph, d_rated * vce * i_ph + 0.5 * p_sw)
tc_max = TJ_DESIGN - p_hot * RTH_JC_IGBT
rth_sa = (tc_max - TA_MAX) / (p_ipm + p_bridge) - RTH_CS
r('hottest IGBT loss', f'{p_hot:.1f} W', 'low-leg IGBT conducts the full 120 deg')
r('max case temperature for Tj = 125 C', f'{tc_max:.0f} C')
r('required heatsink Rth(s-a)', f'<= {rth_sa:.1f} K/W', 'choose <= 2.0 K/W for stall/overload margin')

# --- bootstrap ---
t_off_min = (1 - D_MAX) / F_SW
q_cycle = QG_HS + I_QBS / F_SW
dv_eq = q_cycle * R_BOOT / t_off_min
vbs = VCC - 0.7 - dv_eq
h('5. Bootstrap supply (high side)')
r('min refresh window per period', f'{t_off_min*1e6:.2f} us', f'D_MAX = {D_MAX}')
r('charge drawn per period', f'{q_cycle*1e9:.0f} nC')
r('equilibrium droop through R_boot', f'{dv_eq:.2f} V', f'R_boot = {R_BOOT:.0f} ohm')
r('Vbs at max duty', f'{vbs:.1f} V', 'OK' if vbs > VBS_UVLO + 1 else 'TOO LOW: lower D_MAX or add ext. diode + 10 ohm')
c_bs = 10 * q_cycle / 0.5
r('C_boot (<= 0.5 V droop per period, x10 margin)', f'>= {c_bs*1e6:.1f} uF', 'use 4.7 uF X7R 25 V + 100 nF; low-ON legs are refreshed by pre-charge')

# --- sensing ---
h('6. Current sensing / protection')
r('shunt', f'{R_SHUNT*1e3:.0f} mOhm', '2 x 68 mOhm 2512 1 % in parallel')
r('shunt loss at rated (rms bus ~ phase rms)', f'{i_ph_rms**2*R_SHUNT:.2f} W')
r('IPM CIN hard trip current', f'{V_CIN_TRIP/R_SHUNT:.1f} A', f'Vref(CIN) {V_CIN_TRIP} V [DS]')
r('COMP2 cycle-by-cycle limit', f'{I_CBC:.1f} A', f'DAC3 = {I_CBC*R_SHUNT/VREF*ADC_FS:.0f} counts')
fs = VREF / (R_SHUNT * PGA)
r('ADC full scale (PGA x4)', f'{fs:.1f} A', f'{fs/ADC_FS*1e3:.1f} mA / count')

h('7. Voltage / BEMF dividers')
ratio = (VDIV_TOP + VDIV_BOT) / VDIV_BOT
r('divider ratio', f'{ratio:.1f}', '3 x 330 k (1206, 200 V each) + 7.5 k')
r('ADC volts at 400 V', f'{400/ratio:.2f} V')
r('bus volts per ADC count', f'{VREF/ADC_FS*ratio*1e3:.0f} mV')
r('voltage across each 330 k at 400 V', f'{400/3:.0f} V', '1206 working voltage 200 V; keep creepage')
r('divider dissipation at 400 V', f'{400**2/(VDIV_TOP+VDIV_BOT)*1e3:.0f} mW')
rth = VDIV_TOP * VDIV_BOT / (VDIV_TOP + VDIV_BOT)
r('BEMF filter tau', f'{rth*C_BEMF*1e6:.2f} us', f'Rth {rth/1e3:.2f} k, C {C_BEMF*1e12:.0f} pF')
r('BEMF at 300 rpm (min closed loop), phase', f'{KE_LL/2*300*2*math.pi/60:.1f} V',
  f'{KE_LL/2*300*2*math.pi/60/(VREF/ADC_FS*ratio):.0f} counts')

h('8. Auxiliary 15 V supply')
tot = sum(AUX_LOADS.values())
for k_, v_ in AUX_LOADS.items():
    r(k_, f'{v_} mA')
r('total', f'{tot} mA', 'LNK306DN buck (360 mA max) -> margin x5')
r('input power of aux supply', f'{tot*1e-3*15/0.75:.1f} W', 'eta ~75 %')

text = '# Calculation results (generated by bldc_calc.py)\n' + '\n'.join(out) + '\n'
dst = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'results.md')
open(dst, 'w').write(text)
print(text)
