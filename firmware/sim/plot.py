#!/usr/bin/env python3
"""Plot simulator output: python3 sim/plot.py <slow.csv> <hires.csv> <outdir>"""
import csv, os, sys
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt

def load(fn):
    with open(fn) as f:
        r = csv.DictReader(f)
        rows = list(r)
    return {k: [float(x[k]) for x in rows] for k in rows[0]}

slow, hi, out = sys.argv[1], sys.argv[2], sys.argv[3]
os.makedirs(out, exist_ok=True)
s = load(slow)
names = ['IDLE', 'PRECHARGE', 'ALIGN', 'RAMP', 'RUN', 'FAULT']

fig, ax = plt.subplots(4, 1, figsize=(10, 10), sharex=True)
ax[0].plot(s['t'], s['ref_ramped'], label='speed reference (ramped)', color='#888')
ax[0].plot(s['t'], s['rpm_true'], label='true rotor speed', color='#1f77b4')
ax[0].plot(s['t'], s['rpm_est'], '--', label='sensorless estimate', color='#d62728', lw=1)
ax[0].set_ylabel('rpm'); ax[0].legend(loc='upper left'); ax[0].grid(alpha=.3)
ax[1].plot(s['t'], [d * 100 for d in s['duty']], color='#2ca02c')
ax[1].set_ylabel('duty %'); ax[1].grid(alpha=.3)
ax[2].plot(s['t'], s['i_est'], color='#9467bd', label='phase current (ON-center shunt, filtered)')
ax[2].set_ylabel('A'); ax[2].legend(loc='upper left'); ax[2].grid(alpha=.3)
ax[3].step(s['t'], s['state'], where='post', color='k')
ax[3].set_yticks(range(6)); ax[3].set_yticklabels(names); ax[3].grid(alpha=.3)
ax[3].set_xlabel('time [s]')
ax[0].set_title('Simulated start-up and speed profile (fan load, +30 % load step at 4.2 s)')
fig.tight_layout(); fig.savefig(os.path.join(out, 'sim_speed_profile.png'), dpi=110)

h = load(hi)
t0 = h['t'][0]
t = [(x - t0) * 1e3 for x in h['t']]
fig, ax = plt.subplots(3, 1, figsize=(10, 8), sharex=True)
for k, c in zip(['va', 'vb', 'vc'], ['#1f77b4', '#ff7f0e', '#2ca02c']):
    ax[0].plot(t, h[k], lw=.6, color=c, label=k)
ax[0].set_ylabel('terminal V'); ax[0].legend(loc='upper right'); ax[0].grid(alpha=.3)
for k, c in zip(['ia', 'ib', 'ic'], ['#1f77b4', '#ff7f0e', '#2ca02c']):
    ax[1].plot(t, h[k], lw=.8, color=c, label=k)
ax[1].set_ylabel('phase A'); ax[1].legend(loc='upper right'); ax[1].grid(alpha=.3)
ax[2].step(t, h['step'], where='post', color='k', label='commutation step')
ax2 = ax[2].twinx(); ax2.plot(t, h['theta_e'], color='#d62728', lw=.8, label='rotor angle (elec)')
ax[2].set_ylabel('step'); ax2.set_ylabel('deg'); ax[2].grid(alpha=.3)
ax[2].set_xlabel('time [ms] (window at 2.40 s, 1500 rpm closed loop)')
ax[0].set_title('Phase voltages / currents in closed loop (H_PWM-L_ON, 16 kHz)')
fig.tight_layout(); fig.savefig(os.path.join(out, 'sim_waveforms.png'), dpi=110)
print('plots written to', out)
