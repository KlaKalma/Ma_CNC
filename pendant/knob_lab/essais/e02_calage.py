#!/usr/bin/env python3
"""e02 -- calage electrique et signe du couple, mesures sans main.

On applique un vecteur de tension d'amplitude fixe a l'angle electrique
PP*mecanique + dlt (commutation suivie sur le capteur), pendant une duree
courte, et on releve le deplacement. Le couple vaut A*sin(dlt + c) : le
deplacement trace une sinusoide en dlt dont la phase donne le calage
EXACT et dont l'amplitude donne le couple disponible. Le dlt du maximum
positif fixe zea pour que u > 0 fasse croitre l'angle avec sens = +1.
"""
import sys, os, time
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from lab import *

MAG, N = 0.6, 24
N4 = 100                                       # echantillons de vecteur = 50 ms
with Lab() as L:
    L.set(mode=1, mag=MAG)
    time.sleep(0.3)
    res = []
    for tour in range(2):                       # deux passes, ordre inverse
        ordre = range(N) if tour == 0 else reversed(range(N))
        for i in ordre:
            dl = 2 * np.pi * i / N
            L.set(dlt=round(dl, 5), mode=1)
            # sequence firmware : 20 ech. au repos, N4 ech. de vecteur, frein
            r = L.rec(0.35, s1n=20, s1m=4, s2n=20 + N4, s2m=1)
            on = np.nonzero(r['u'] > 0)[0]
            if len(on) < 5:
                continue
            a = r['ang']
            d = a[on[-1]] - a[on[0]]
            res.append((dl, d, len(on)))
            print(f'dlt={np.degrees(dl):6.1f}  dep={np.degrees(d):7.2f} deg  n={len(on)}', flush=True)
            time.sleep(0.2)
    L.set(mode=0)

res = np.array(res)
dl, d = res[:, 0], res[:, 1]
M = np.column_stack([np.sin(dl), np.cos(dl), np.ones_like(dl)])
(a, b, c0), *_ = np.linalg.lstsq(M, d, rcond=None)
A = np.hypot(a, b); ph = np.arctan2(b, a)          # d = A sin(dl + ph) + c0
dmax = (np.pi / 2 - ph) % (2 * np.pi)              # dlt du couple max positif
zea = (np.pi / 2 - dmax) % (2 * np.pi)
resid = d - M @ np.array([a, b, c0])
print(f'\nA={np.degrees(A):.2f} deg  decalage={np.degrees(c0):.2f} deg  '
      f'residu rms={np.degrees(resid.std()):.2f} deg')
print(f'couple max positif a dlt={np.degrees(dmax):.2f} deg -> zea={zea:.4f} rad avec sens=+1')
import matplotlib; matplotlib.use('Agg'); import matplotlib.pyplot as plt
x = np.linspace(0, 2 * np.pi, 200)
plt.plot(np.degrees(dl), np.degrees(d), 'o', label='mesure')
plt.plot(np.degrees(x), np.degrees(A * np.sin(x + ph) + c0), label='A sin(dlt+c)')
plt.axvline(np.degrees(dmax), color='k', ls=':')
plt.xlabel('dlt (deg electrique)'); plt.ylabel('deplacement en 50 ms (deg)')
plt.title(f'calage : zea={zea:.4f} rad (sens=+1)'); plt.legend(); plt.grid(alpha=.3)
plt.savefig(os.path.join(OUT, 'e02_calage.png'), dpi=80)
