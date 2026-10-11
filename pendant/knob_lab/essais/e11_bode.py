#!/usr/bin/env python3
"""e11 -- reponse en frequence REELLE du systeme u -> angle mesure, sans main.
Chirp de couple injecte (f1 -> f2) sous un ressort tres faible qui empeche
seulement la derive. H = S_u,ang / S_u,u (Welch). Contient TOUT ce qui compte
pour la stabilite : inertie, frottement, resonances, retards capteur + calcul
+ PWM. Un double integrateur pur 1/(J s^2) aurait une phase de -180 deg : tout
ecart de phase supplementaire est du retard (ou une resonance)."""
import sys, os, time
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from lab import *
from scipy.signal import csd, welch

IA = float(sys.argv[1]) if len(sys.argv) > 1 else 1.0
F1, F2, DUR = 15.0, 400.0, 6.0
with Lab() as L:
    L.zero()
    L.set(mode=5, k=3, d=0.0, cible=0, vr=0, it=3, ia=IA, f1=F1, f2=F2, idur=DUR, umax=2.2)
    time.sleep(0.5)
    r = L.rec(DUR, nom='e11_chirp')
    L.set(it=0, mode=0)
ts = r['ts']; u = r['u'] - r['u'].mean(); a = r['ang'] - r['ang'].mean()
print(f'ts={ts*1e6:.0f} us  err={r["err"].mean()*100:.1f} %  sat={((r["fl"]&FL_SAT)!=0).mean()*100:.1f} %  '
      f'amplitude angle {np.ptp(a):.3f} rad')
nper = 1024
f, Sua = csd(u, a, fs=1/ts, nperseg=nper)
_, Suu = welch(u, fs=1/ts, nperseg=nper)
_, Saa = welch(a, fs=1/ts, nperseg=nper)
H = Sua / Suu
coh = np.abs(Sua)**2 / (Suu * Saa)
m = (f >= F1 * 1.2) & (f <= F2 * 0.9) & (coh > 0.8)
w = 2 * np.pi * f[m]
mag = np.abs(H[m]); ph = np.unwrap(np.angle(H[m]))
# inertie apparente : |H| = 1/(J w^2)  -> J = 1/(|H| w^2)
Japp = 1 / (mag * w**2)
# retard equivalent : phase = -pi - w*tau
tau = -(ph + np.pi) / w
for fr in (20, 40, 60, 80, 100, 120, 150, 200, 250, 300):
    k = np.argmin(np.abs(f[m] - fr))
    print(f'{f[m][k]:6.1f} Hz  |H|={mag[k]:.3e} rad/V  phase={np.degrees(ph[k]):7.1f} deg  '
          f'J_app={Japp[k]:.2e}  retard_eq={tau[k]*1000:5.2f} ms  coh={coh[m][k]:.2f}')
np.savez(os.path.join(OUT, 'e11_bode.npz'), f=f[m], H=H[m], coh=coh[m])
import matplotlib; matplotlib.use('Agg'); import matplotlib.pyplot as plt
fig, ax = plt.subplots(3, 1, figsize=(11, 10), sharex=True)
ax[0].loglog(f[m], mag); ax[0].set_ylabel('|angle/u| (rad/V)'); ax[0].grid(True, which='both', alpha=.3)
ax[1].semilogx(f[m], np.degrees(ph)); ax[1].axhline(-180, color='k', ls=':'); ax[1].set_ylabel('phase (deg)'); ax[1].grid(True, which='both', alpha=.3)
ax[2].semilogx(f[m], tau * 1000); ax[2].set_ylabel('retard equivalent (ms)'); ax[2].set_xlabel('Hz'); ax[2].grid(True, which='both', alpha=.3)
fig.savefig(os.path.join(OUT, 'e11_bode.png'), dpi=75)
