#!/usr/bin/env python3
"""calc_v2.py - design numbers for the v2 cooler drive (PFC, aux flyback, sensing, panel bus)."""
import math
out = []
def row(q, v, n=""): out.append(f"| {q} | {v} | {n} |")
def head(t): out.append(f"\n### {t}\n\n| quantity | value | note |\n|---|---|---|")

# ---------------- PFC (boost CCM)
P, eta, Vac_min, Vout, fs = 1300.0, 0.95, 180.0, 390.0, 67e3
Vpk = Vac_min * math.sqrt(2)
Ipk = math.sqrt(2) * P / (eta * Vac_min)
D = 1 - Vpk / Vout
dI = 0.25 * Ipk
L = Vpk * D / (fs * dI)
head("PFC boost (NCP1653, 67 kHz CCM)")
row("input current rms at 180 VAC, 1.3 kW", f"{P/(eta*Vac_min):.1f} A", "fuse 10 A T, relay 10 A, bridge 10 A")
row("peak inductor current (line peak)", f"{Ipk:.1f} A", "+ ripple/2")
row("duty at line peak (180 VAC)", f"{D:.2f}")
row("inductance for 25 % ripple", f"{L*1e6:.0f} uH", "-> 470 uH, Isat >= 13 A")
row("stored energy 1/2 L I^2", f"{0.5*470e-6*(Ipk+dI/2)**2*1e3:.0f} mJ", "toroid ~ 46 mm OD")
Irms_sh = P / (eta * Vac_min)
row("PFC shunt 25 mOhm loss", f"{Irms_sh**2*0.025:.2f} W", "2 x 50 mOhm 3 W 2512 in parallel")
Imos = P / (eta * Vac_min) * math.sqrt(1 - 8 * math.sqrt(2) * Vac_min / (3 * math.pi * Vout))
row("MOSFET rms current", f"{Imos:.1f} A", f"95 mOhm @ 100 C (x1.8): {Imos**2*0.095*1.8:.1f} W conduction")
C = 940e-6
row("bus capacitance", "940 uF / 450 V", "2 x 470 uF 450 V 105 C snap-in in parallel (new)")
row("bus 100 Hz ripple at 1.2 kW", f"{1200/(2*math.pi*50*C*Vout):.1f} V p-p")
row("hold-up 390 -> 300 V at 600 W", f"{0.5*C*(Vout**2-300**2)/600*1e3:.0f} ms", "rides through 2 mains cycles")
row("capacitor ripple current (PFC 100 Hz + inverter)", "~3 A rms total", "2 x >= 2.5 A rating -> margin")
row("pre-charge energy in the PTC", f"{0.5*C*373**2:.0f} J", "PTC must be rated >= 80 J")
row("bleeder 3 x 100k", f"{390**2/300e3:.2f} W", f"390 V -> 60 V in {300e3*C*math.log(390/60)/60:.0f} min")

# ---------------- aux flyback
Pout = 2.25 + 0.3 + 3.7
Pd = 8.0
Vmin, Dmax, f = 200.0, 0.40, 60e3
Lp = (Vmin * Dmax) ** 2 / (2 * Pd / 0.8 * f)
Ip = Vmin * Dmax / (Lp * f)
Ae, Bmax = 31e-6, 0.28
Np = math.ceil(Lp * Ip / (Bmax * Ae))
VOR = 90.0
N15 = round(Np * (15 + 0.7) / VOR)
N31 = round(N15 * (31 + 0.7) / (15 + 0.7))
N5 = round(N15 * (5.5 + 0.4) / (15 + 0.7))
head("Auxiliary flyback (VIPer26LN, 60 kHz, EF20)")
row("loads", f"{Pout:.1f} W", "15 V 150 mA (3 x IRS2186, relays, buck 3.3 V) + SELV 5 V 60 mA + SELV 31 V 120 mA")
row("design power", f"{Pd:.0f} W")
row("input range (after 1500 V pre-regulator)", "200 ... 395 V", "440 VAC neutral loss -> clamped")
row("primary inductance", f"{Lp*1e3:.1f} mH", f"DCM, Dmax {Dmax} at {Vmin:.0f} V")
row("peak primary current", f"{Ip:.2f} A", "VIPer26 limit ~0.7 A")
row("turns P / 15 V hot / 31 V SELV / 5.5 V SELV", f"{Np} / {N15} / {N31} / {N5}", f"Bmax {Bmax} T, Ae 31 mm^2, AL ~{Lp/Np**2*1e9:.0f} nH")
row("drain stress at 395 V in", f"{395 + VOR + 200:.0f} V max", "with P6KE200 clamp -> < 800 V rating")
row("insulation", "reinforced", "triple-insulated wire on SELV windings, 6.4 mm creepage on bobbin")
row("pre-regulator Q3 loss at 440 VAC", f"{(440*math.sqrt(2)*0.9-395)*Pd/0.8/300:.1f} W", "approx (average); on main heatsink")

# ---------------- inverter: discrete MOSFET options (sinusoidal 3-phase, 16 kHz, 390 V)
head("Inverter switch losses per device set (1.2 kW, 3.6 A rms, 16 kHz, 390 V, Tj 100 C)")
Iph, fsw, Vb = 3.6, 16e3, 390.0
Ipk_ph = Iph * math.sqrt(2)
def inv_loss(rds25, tr_tf_ns, qrr_uc, k_t=1.9):
    cond = 6 * (Iph ** 2 / 2) * rds25 * k_t                     # each device conducts ~half
    sw = 6 * 0.5 * Vb * (2 / math.pi) * Ipk_ph * tr_tf_ns * 1e-9 * fsw
    rr = 6 * 0.5 * Vb * qrr_uc * 1e-6 * fsw                     # body-diode reverse recovery (avg ~0.5 Qrr)
    return cond, sw, rr
for name, rds, t, qrr, note in (("IRF840 (500 V, 0.85 ohm, trr 460 ns)", 0.85, 60, 4.2, "NOT suitable: 500 V = 1.28 x margin, recovery losses"),
                                ("STW24N60DM2 (600 V, 0.19 ohm, fast diode)", 0.19, 40, 0.35, "chosen"),
                                ("STGW20H60DF IGBT + co-pack diode", 0.0, 60, 0.25, "alternative (Vce 1.6 V)")):
    c, sw, rr = inv_loss(rds, t, qrr)
    if rds == 0.0:
        c = 6 * 1.6 * Ipk_ph / math.pi / 2 * 1.0
    row(name, f"{c + sw + rr:.1f} W", f"cond {c:.1f} + sw {sw:.1f} + recovery {rr:.1f} W; {note}")

# ---------------- current sensing (3 shunts, level shift, PGA x4)
Rsh, gain_div, off, pga = 0.025, 10 / 11, 3.3 / 11, 4.0
head("Phase current sensing (3 x 25 mOhm, offset 0.3 V, OPAMP PGA x4)")
row("ADC volts per amp", f"{Rsh*gain_div*pga:.3f} V/A")
row("zero-current ADC level", f"{off*pga:.2f} V ({off*pga/3.3*4095:.0f} counts)")
row("range", f"{-off/(Rsh*gain_div):.1f} ... {(3.3/pga-off)/(Rsh*gain_div):.1f} A", "negative side covers freewheeling")
row("resolution", f"{3.3/4095/(Rsh*gain_div*pga)*1e3:.1f} mA/count", "simulator used 8 mA")
row("shunt loss at 3.6 A rms phase", f"{(3.6**2/2)*Rsh:.2f} W", "low-side conducts ~half the time")

# ---------------- panel bus
head("Panel bus (SELV)")
row("mark / space voltage", f"{1.25*(1+4300/240):.1f} V / {1.25*(1+(1/(1/4300+1/8200))/240):.1f} V", "LM317: 240 R, 4.3 k, 8.2 k switched")
row("short-circuit current", f"{1.25/8.2*1e3:.0f} mA", "LM317 as current source; PTC 0.1 A hold")
row("return sense", f"{4.7*1e3:.0f} mV/A", "80 mA -> 376 mV, 20 mA signal -> 94 mV (116 counts)")

# ---------------- relays
head("Relay coils (12 V coils from +15 V)")
row("Finder 40.11 12 V (~0.5 W, ~290 ohm)", "68 ohm series", "verify coil resistance of the ordered part")
row("Finder 34.51 12 V (~0.17 W, ~850 ohm)", "220 ohm series", "verify")

text = "# v2 design numbers (generated by calc_v2.py)\n" + "\n".join(out) + "\n"
open(__file__.replace("calc_v2.py", "out/calc_v2.md"), "w").write(text)
print(text)
