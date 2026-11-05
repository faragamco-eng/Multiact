#!/usr/bin/env python3
"""Start-up robustness sweep: every scenario x 12 initial rotor angles,
optionally with config overrides compiled into a private copy.

    python3 sim/sweep.py                         # current inc/app_config.h
    python3 sim/sweep.py "RAMP_I_A=2.5f,ALIGN_MS=800u"
    python3 sim/sweep.py --tolerance             # R/L/Ke +-30..40 % study
"""
import itertools, math, os, re, subprocess, sys, tempfile
from concurrent.futures import ThreadPoolExecutor

FW = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SCEN = ['default', 'heavy', 'inertia', 'lowbus', 'reverse']

def build(over):
    d = tempfile.mkdtemp(prefix='sweep_', dir=os.path.join(FW, 'build'))
    cfg = open(os.path.join(FW, 'inc/app_config.h')).read()
    for kv in filter(None, over.split(',')):
        k, v = kv.split('=')
        cfg, n = re.subn(rf'^#define {k} .*$', f'#define {k} {v}', cfg, flags=re.M)
        assert n == 1, f'unknown macro {k}'
    open(os.path.join(d, 'app_config.h'), 'w').write(cfg)
    exe = os.path.join(d, 'motor_sim')
    subprocess.run(['gcc', '-O2', f'-I{d}', f'-I{FW}/core', f'{FW}/sim/motor_sim.c',
                    f'{FW}/core/sixstep.c', '-lm', '-o', exe], check=True)
    return exe

def run(job):
    exe, sc, env = job
    r = subprocess.run([exe, sc], env=dict(os.environ, **env), capture_output=True, text=True)
    return sc, env, r.returncode == 0

def main():
    args = sys.argv[1:]
    tol = '--tolerance' in args
    args = [a for a in args if a != '--tolerance'] or ['']
    for over in args:
        exe = build(over)
        jobs = []
        if tol:
            for rk, lk, kk in itertools.product([0.7, 1.0, 1.3], [0.6, 1.0, 1.4], [0.85, 1.0, 1.15]):
                for sc in ['default', 'heavy', 'inertia']:
                    for th in (0.01, 0.9):
                        jobs.append((exe, sc, dict(SIM_RK=str(rk), SIM_LK=str(lk), SIM_KEK=str(kk), SIM_THETA0=str(th))))
        else:
            for i in range(12):
                for sc in SCEN:
                    jobs.append((exe, sc, dict(SIM_THETA0=str(i * math.pi / 2 / 12 + 0.01))))
        with ThreadPoolExecutor(os.cpu_count()) as ex:
            res = list(ex.map(run, jobs))
        fails = [(sc, env) for sc, env, ok in res if not ok]
        print(f'[{over or "current config"}] {len(res) - len(fails)}/{len(res)} pass')
        for sc, env in fails:
            print('   FAIL', sc, ' '.join(f'{k}={v}' for k, v in env.items()))

if __name__ == '__main__':
    main()
