#!/usr/bin/env python3
"""e10 -- comportement d'une loi de crans, SANS MAIN.
  repos  : volant laisse au centre d'un cran, 5 s. Mesure la vibration propre.
  lacher : un ressort raide (mode 5) amene le volant a une fraction de la
           largeur du cran, puis le FIRMWARE bascule en mode 6 (s1n/s1m) :
           on enregistre comment il se pose (depassement, temps, cycle limite).
Usage : e10_crans.py nom k=v ...   (parametres du mode 6, ex. w=0.471 kd=1.6)"""
import sys, os, time
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from lab import *

nom = sys.argv[1] if len(sys.argv) > 1 else 'crans'
P = dict(w=0.4712, kd=1.6, dz=0.0175, snap=1.1, d=0.2, nv=4, tau=0, vz=0, nbc=0)
for a in sys.argv[2:]:
    k, v = a.split('='); P[k] = float(v)
W = P['w']

def bilan_repos(r):
    v = vitesse(r, 2); u = r['u']
    f, A = spectre(u, r['ts']); m = f > 3
    return dict(u_rms=u.std(), u_moy=u.mean(), ang_pp=int(np.ptp(r['acc'])),
                v_rms=np.sqrt(np.mean(v**2)), f_u=f[m][np.argmax(A[m])],
                err=r['err'].mean() * 100, crans=int(((r['fl'] & FL_CRAN) != 0).sum()))

def bilan_lacher(r, i0, a_cible):
    a = r['ang'] - r['ang'][0]; v = vitesse(r, 2)
    a = a[i0:] + (a_cible - a[i0]) if False else r['ang'][i0:]
    rel = r['ang'][i0:] - ZERO
    ts = r['ts']
    fin = rel[-int(0.3 / ts):]
    k_c = np.round(rel / W)                       # cran le plus proche
    ecart = rel - np.round(fin.mean() / W) * W     # par rapport au cran final
    # temps pour rester dans +-1 deg du centre final
    hors = np.nonzero(np.abs(ecart) > np.radians(1.0))[0]
    t_pose = (hors[-1] + 1) * ts if len(hors) else 0.0
    # nombre de traversees du centre final (oscillation)
    zc = np.sum(np.diff(np.sign(ecart[np.abs(ecart) > np.radians(0.2)])) != 0)
    vv = v[i0:]
    return dict(cran_final=int(np.round(fin.mean() / W)), t_pose=t_pose,
                depass=np.degrees(np.max(-np.sign(ecart[0]) * ecart)),
                traversees=int(zc), ecart_final=np.degrees(fin.mean() - np.round(fin.mean() / W) * W),
                v_rms_fin=np.sqrt(np.mean(vv[-int(0.3/ts):]**2)), u_rms_fin=r['u'][-int(0.3/ts):].std())

with Lab() as L:
    L.set(**{k: v for k, v in P.items()})
    L.set(mode=0); time.sleep(0.3)
    # zero au repos, puis mode 6 : le volant se pose au centre du cran 0
    L.zero()
    ZERO = None
    L.set(mode=6); time.sleep(1.0)
    r = L.rec(5.0, nom=f'e10_{nom}_repos')
    ZERO = r['ang'][0] - 0.0
    # angle absolu du zero : on le reconstitue comme le centre de cran 0 pose
    ZERO = np.median(r['ang'])
    b = bilan_repos(r)
    print(f'[{nom}] REPOS 5 s : ' + '  '.join(f'{k}={v:.4g}' if isinstance(v, float) else f'{k}={v}' for k, v in b.items()), flush=True)
    L.report(r, f'e10_{nom}_repos.png', f'{nom} repos')
    # lachers
    res = []
    for frac in [float(x) for x in os.environ.get('FRACS', '0.15,0.3,0.45,-0.15,-0.3,-0.45').split(',')]:
        L.set(mode=5, k=25, cible=round(frac * W, 4), vr=1.0)
        time.sleep(0.8)
        n0 = 100
        r = L.rec(1.5, s1n=n0, s1m=6)
        L.set(**{k: v for k, v in P.items() if k == 'd'})
        b = bilan_lacher(r, n0, frac * W)
        res.append((frac, b))
        print(f'[{nom}] LACHER {frac:+.2f}*w : ' + '  '.join(f'{k}={v:.3g}' if isinstance(v, float) else f'{k}={v}' for k, v in b.items()), flush=True)
        sauve(r, f'e10_{nom}_lacher{frac:+.2f}')
        L.set(mode=6); time.sleep(0.3)
    L.set(mode=0)
