#!/usr/bin/env python3
"""e08 -- identification mecanique par echelons de couple (mode 2, sans main).
Modele, tout en VOLTS (le couple s'exprime par la tension qui le produit) :
    J * w' = u - Ke * w - f * sign(w)
  J  inertie ramenee (V.s2/rad)   Ke  force contre-electromotrice (V.s/rad)
  f  frottement sec (V)
En regime etabli w_inf = (u - f)/Ke ; constante de temps tau = J/Ke.
Plusieurs u, les deux sens : une droite w_inf(u) donne Ke et f, tau donne J."""
import sys, os, time
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from lab import *
from scipy.optimize import curve_fit

US = [0.3, 0.5, 0.7, 0.9]
N1, N2 = 40, 40 + 500          # repos 20 ms, couple 250 ms, puis frein
res = []
with Lab() as L:
    L.set(sens=1, zea=0.31, vtest=40, mode=1)
    for u in US + [-x for x in US]:
        L.set(uq=u, mode=1); time.sleep(0.4)
        r = L.rec(0.5, s1n=N1, s1m=2, s2n=N2, s2m=1)
        t = r['t'] - r['t'][N1]
        w = vitesse(r, 3)
        m = (t > 0.003) & (t < (N2 - N1) * r['ts'] - 0.003)
        f = lambda tt, winf, tau, t0: winf * (1 - np.exp(-(tt - t0) / tau)) * (tt > t0)
        try:
            (winf, tau, t0), _ = curve_fit(f, t[m], w[m], p0=(u / 0.045, 0.03, 0.0))
        except Exception as e:
            print('fit KO', u, e); continue
        resid = (w[m] - f(t[m], winf, tau, t0)).std()
        res.append((u, winf, tau, t0))
        print(f'u={u:+.2f} V  w_inf={winf:+7.2f} rad/s  tau={tau*1000:5.1f} ms  '
              f't0={t0*1000:4.1f} ms  residu={resid:.2f} rad/s  err_i2c={r["err"].sum()}', flush=True)
        sauve(r, f'e08_u{u:+.1f}')
    L.set(mode=0)

res = np.array(res)
for sgn in (1, -1):
    k = np.sign(res[:, 0]) == sgn
    a, b = np.polyfit(res[k, 0], res[k, 1], 1)         # w = a u + b
    Ke = 1 / a; f = -b * Ke
    print(f'sens {sgn:+d}: Ke={Ke:.4f} V.s/rad  f={f*sgn:+.3f} V  '
          f'J=tau*Ke={np.mean(res[k,2])*Ke:.3e} V.s2/rad (tau moy {np.mean(res[k,2])*1000:.1f} ms)')
