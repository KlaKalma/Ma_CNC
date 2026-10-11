#!/usr/bin/env python3
"""e12 -- banc comparatif de lois de crans, SANS MAIN.
Pour chaque loi : repos (3 s), lachers (+-0,3 et +-0,45 largeur) et
traversee de 4 crans dans chaque sens par la MAIN VIRTUELLE (mode 7).
Indicateurs :
  repos   u_rms (mV), v_rms (rad/s)          -> vibration propre
  lacher  ecart final (deg), depassement      -> precision, rebond
          oscille (oui/non)                   -> stabilite
  tourner vib_u (mV rms > 40 Hz)              -> vibration en tournant
          vib_a (mrad rms > 40 Hz)            -> ce que la main sent
          crans (nb de changements de cran)   -> 8 attendus
Usage : e12_comparer.py [noms de lois...] (voir LOIS)"""
import sys, os, time
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from lab import *
from scipy.signal import butter, sosfiltfilt

DEG = np.pi / 180
LOIS = {
  'ancien': dict(w=27*DEG, kd=1.6, dz=1.0*DEG, snap=1.1, d=0.20, nv=4, cog=0),
  'A':      dict(w=27*DEG, kd=1.6, dz=0.5*DEG, snap=1.1, d=0.045, nv=2, cog=0),
  'B':      dict(w=27*DEG, kd=1.6, dz=0.5*DEG, snap=1.1, d=0.045, nv=2, cog=1),
  'C':      dict(w=27*DEG, kd=2.0, dz=0.5*DEG, snap=1.1, d=0.045, nv=2, cog=1),
  'fin':    dict(w=12*DEG, kd=1.0, dz=0.3*DEG, snap=1.1, d=0.045, nv=2, cog=1),
  'fin0':   dict(w=12*DEG, kd=1.0, dz=0.3*DEG, snap=1.1, d=0.045, nv=2, cog=0),
}
COMMUN = dict(tau=0, vz=0, nbc=0, kmur=0, umax=2.2, vmax=60)
HP = butter(4, 40, 'highpass', fs=2000, output='sos')

def hf(x):
    return sosfiltfilt(HP, x - np.mean(x))

def essai(L, nom, P):
    w = P['w']
    L.set(mode=0, **COMMUN, **P); time.sleep(0.3)
    L.zero(); L.set(mode=6); time.sleep(1.0)
    res = {}
    r = L.rec(3.0)
    res['repos_u'] = r['u'].std() * 1000
    res['repos_v'] = np.sqrt(np.mean(vitesse(r, 2)**2))
    centre = np.median(r['ang'])            # centre pose du cran 0 (absolu)
    # lachers
    ecarts, osc, dep = [], 0, []
    for frac in (0.3, -0.3, 0.45, -0.45):
        L.set(mode=5, k=25, d=0.03, cible=round(frac * w, 4), vr=1.0); time.sleep(0.6)
        L.set(**{'d': P['d']})
        r = L.rec(1.2, s1n=60, s1m=6)
        rel = r['ang'][60:] - centre
        fin = np.mean(rel[-300:])
        e = fin - np.round(fin / w) * w
        ecarts.append(abs(np.degrees(e)))
        if np.sqrt(np.mean(vitesse(r, 2)[-300:]**2)) > 0.5: osc += 1
        L.set(mode=6); time.sleep(0.4)
    res['lacher_ecart'] = np.mean(ecarts); res['lacher_oscille'] = osc
    # traversee par la main virtuelle : 4 crans aller, 4 retour
    vib_u, vib_a, crans = [], [], 0
    L.set(mode=6); time.sleep(0.3)
    for cible in (4 * w, 0.0):
        L.set(kh=5, vr=1.5, cible=round(cible, 4))
        r = L.rec(4 * w / 1.5 + 0.6, s1n=1, s1m=7)
        m = slice(100, -100)
        vib_u.append(hf(r['u'])[m].std() * 1000)
        vib_a.append(hf(r['ang'])[m].std() * 1000)
        crans += int(((r['fl'] & FL_CRAN) != 0).sum())
        sauve(r, f'e12_{nom}_trav{int(cible>0)}')
        L.set(mode=6); time.sleep(0.5)
    res['tourner_vib_u'] = np.mean(vib_u); res['tourner_vib_a'] = np.mean(vib_a)
    res['crans'] = crans
    L.set(mode=0)
    return res

noms = sys.argv[1:] or list(LOIS)
with Lab() as L:
    print(f"{'loi':8s} {'repos_u':>8s} {'repos_v':>8s} {'ecart':>7s} {'oscille':>7s} {'vib_u':>7s} {'vib_a':>7s} {'crans':>5s}")
    for nom in noms:
        r = essai(L, nom, LOIS[nom])
        print(f"{nom:8s} {r['repos_u']:8.1f} {r['repos_v']:8.3f} {r['lacher_ecart']:7.2f} {r['lacher_oscille']:4d}/4 "
              f"{r['tourner_vib_u']:7.0f} {r['tourner_vib_a']:7.2f} {r['crans']:5d}", flush=True)
