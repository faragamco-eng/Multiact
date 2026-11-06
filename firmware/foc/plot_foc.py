#!/usr/bin/env python3
"""Plot FOC simulator traces: python3 foc/plot_foc.py build/foc_id_run_A.csv build/foc_locked.csv docs/img"""
import csv, os, sys
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt

def load(fn):
    rows = list(csv.DictReader(open(fn)))
    return {k: [float(r[k]) for r in rows] for k in rows[0]}

names = ['IDLE', 'ID_R', 'ID_L', 'CATCH', 'START', 'RUN', 'ID_J', 'FAULT']
a, b, out = load(sys.argv[1]), load(sys.argv[2]), sys.argv[3]
os.makedirs(out, exist_ok=True)

fig, ax = plt.subplots(4, 1, figsize=(10, 10), sharex=True)
ax[0].plot(a['t'], a['ref_rpm'], color='#888', label='command (rpm)')
ax[0].plot(a['t'], a['rpm'], color='#1f77b4', label='true rotor speed')
ax[0].plot(a['t'], a['rpm_est'], '--', color='#d62728', lw=1, label='sensorless estimate (PLL)')
ax[0].set_ylabel('rpm'); ax[0].legend(loc='lower right'); ax[0].grid(alpha=.3)
ax[1].plot(a['t'], a['iq'], color='#9467bd', lw=.7, label='iq'); ax[1].plot(a['t'], a['id'], color='#2ca02c', lw=.7, label='id')
ax[1].set_ylabel('A'); ax[1].legend(loc='upper right'); ax[1].grid(alpha=.3)
run = [e if s == 5 else float('nan') for e, s in zip(a['theta_err_deg'], a['state'])]
ax[2].plot(a['t'], run, color='#ff7f0e', lw=.6)
ax[2].set_ylabel('angle error [deg el.]\n(RUN only)'); ax[2].grid(alpha=.3); ax[2].set_ylim(-30, 30)
ax[3].step(a['t'], a['state'], where='post', color='k')
ax[3].set_yticks(range(8)); ax[3].set_yticklabels(names); ax[3].grid(alpha=.3); ax[3].set_xlabel('time [s]')
ax[0].set_title('Motor A (1.2 kW, 20 pole pairs, blower J = 0.09 kg m²): auto-identification, start, '
                'load step +30 % at 45 s, 50 % at 55 s', fontsize=9)
fig.tight_layout(); fig.savefig(os.path.join(out, 'foc_id_and_run.png'), dpi=110)

fig, ax = plt.subplots(2, 1, figsize=(10, 5), sharex=True)
m = [i for i, t in enumerate(b['t']) if 28 < t < 34]
T = [b['t'][i] for i in m]
ax[0].plot(T, [b['rpm'][i] for i in m], label='rotor speed'); ax[0].plot(T, [b['rpm_est'][i] for i in m], '--', label='estimate')
ax[0].axvline(30, color='r', ls=':', label='rotor blocked'); ax[0].set_ylabel('rpm'); ax[0].legend(); ax[0].grid(alpha=.3)
ax[1].plot(T, [b['iq'][i] for i in m], color='#9467bd', label='iq')
ax[1].step(T, [b['state'][i] for i in m], where='post', color='k', label='state (7 = FAULT)')
ax[1].set_ylabel('A / state'); ax[1].legend(); ax[1].grid(alpha=.3); ax[1].set_xlabel('time [s]')
ax[0].set_title('Locked rotor at full speed: current held at the limit, alarm after 1.5 s', fontsize=9)
fig.tight_layout(); fig.savefig(os.path.join(out, 'foc_locked_rotor.png'), dpi=110)
print('written', out)
