#!/usr/bin/env python3
"""e09 -- carte du cogging et du frottement en fonction de l'angle, sans main.
Ressort raide (mode 5) dont la consigne avance lentement (vr) sur un tour
complet, aller puis retour. En quasi-statique :
    aller  : u = +f + Ke*vr - c(a)       retour : u = -f - Ke*vr - c(a)
donc  c(a) = -(u_aller + u_retour)/2   et   f + Ke*vr = (u_aller - u_retour)/2
c(a) = couple de cogging (V), positif = pousse vers les angles croissants."""
import sys, os, time
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from lab import *

K, D, VR = 30.0, 0.04, 0.4
TOUR = 2 * np.pi
with Lab() as L:
    L.zero()
    L.set(mode=5, k=K, d=D, vr=VR, cible=0, umax=2.2)
    time.sleep(0.5)
    L.set(cible=round(TOUR + 0.3, 4)); ra = L.rec((TOUR + 0.3) / VR + 0.5, nom='e09_aller')
    L.set(cible=-0.3);                 rr = L.rec((TOUR + 0.6) / VR + 0.5, nom='e09_retour')
    L.set(cible=0); time.sleep(1.5)
    L.set(mode=0)

NB = 1024
bins = np.linspace(0, TOUR, NB + 1); cen = 0.5 * (bins[1:] + bins[:-1])
def carte(r, sens):
    a = (r['ang'] - r['ang'][0] + ANG0[0]) % TOUR
    v = vitesse(r, 5)
    m = (np.abs(v - sens * VR) < 0.6 * VR) & ~r['err']     # en mouvement regulier
    idx = np.digitize(a[m], bins) - 1
    s = np.bincount(idx, r['u'][m], NB); c = np.bincount(idx, None, NB)
    return np.where(c > 0, s / np.maximum(c, 1), np.nan), m.mean()
ANG0 = [ra['raw'][0] * LSB]
ua, fa = carte(ra, +1)
ANG0 = [rr['raw'][0] * LSB]
ub, fb = carte(rr, -1)
cog = -(ua + ub) / 2
fr = (ua - ub) / 2
ok = ~np.isnan(cog)
print(f'echantillons en mouvement regulier : aller {fa*100:.0f} %  retour {fb*100:.0f} %')
print(f'cogging : crete-a-crete {np.nanmax(cog)-np.nanmin(cog):.3f} V  rms {np.nanstd(cog):.3f} V')
print(f'frottement + Ke*vr : moyen {np.nanmean(fr):.3f} V  min {np.nanmin(fr):.3f}  max {np.nanmax(fr):.3f}')
# spectre spatial du cogging : periodes par tour
c0 = np.where(ok, cog - np.nanmean(cog), 0)
F = np.abs(np.fft.rfft(c0)) * 2 / ok.sum()
top = np.argsort(F[1:200])[::-1][:6] + 1
print('harmoniques dominants (periodes/tour : amplitude V) :',
      ', '.join(f'{k}:{F[k]:.3f}' for k in top))
np.savez(os.path.join(OUT, 'e09_carte.npz'), cen=cen, cog=cog, fr=fr, ua=ua, ub=ub)
import matplotlib; matplotlib.use('Agg'); import matplotlib.pyplot as plt
fig, ax = plt.subplots(3, 1, figsize=(14, 10))
ax[0].plot(np.degrees(cen), ua, lw=.6, label='u aller'); ax[0].plot(np.degrees(cen), ub, lw=.6, label='u retour')
ax[0].legend(); ax[0].set_ylabel('V'); ax[0].grid(alpha=.3)
ax[1].plot(np.degrees(cen), cog, lw=.7); ax[1].set_ylabel('cogging c(a) (V)'); ax[1].grid(alpha=.3)
ax[1].set_xlabel('angle capteur (deg)')
ax[2].stem(np.arange(1, 200), F[1:200], markerfmt=' ', basefmt=' '); ax[2].set_xlabel('periodes par tour')
ax[2].set_ylabel('amplitude (V)'); ax[2].grid(alpha=.3)
fig.tight_layout(); fig.savefig(os.path.join(OUT, 'e09_cogging.png'), dpi=75)
