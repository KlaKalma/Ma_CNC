#!/usr/bin/env python3
"""profil.py -- change le comportement du volant a chaud.

    ./profil.py            liste les profils
    ./profil.py B          charge le profil B
    ./profil.py B kd=1.2   profil B avec une retouche

Les reglages restent en memoire de la carte jusqu'a coupure (au demarrage :
profil B, defini dans knob_lab.ino)."""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from lab import Lab
DEG = 3.141592653589793 / 180
COMMUN = dict(mode=6, tau=0, vz=0, nbc=0, kmur=0, umax=2.2, vmax=60, it=0, stream=0)
BASE = dict(w=27*DEG, dz=0.5*DEG, snap=1.1, d=0.045, nv=2, cog=1, clic=0, clicms=1.0)
def B(kd, **kw):
    q = dict(BASE, kd=kd); q.update(kw); return q
PROFILS = {
  'ancien': ('ANC', 'ancienne loi (D trop fort) : POUR COMPARER, elle vibre',
             dict(w=27*DEG, kd=1.6, dz=1.0*DEG, snap=1.1, d=0.20, nv=4, cog=0, clic=0)),
  'A':   ('A',   '27 deg, 1,6 V, cogging NON compense', B(1.6, cog=0)),
  'B':   ('B',   '27 deg, 1,6 V, cogging compense (demarrage)', B(1.6)),
  'C':   ('C',   '27 deg, 2,0 V (plus ferme)', B(2.0)),
  'B12': ('12',  '27 deg, 1,2 V (plus doux)', B(1.2)),
  'B09': ('9',   '27 deg, 0,9 V (doux)', B(0.9)),
  'B12c':('12C', '27 deg, 1,2 V + impulsion 0,8 V x 1 ms', B(1.2, clic=0.8)),
  'B09c':('9C',  '27 deg, 0,9 V + impulsion 0,8 V x 1 ms', B(0.9, clic=0.8)),
  'B09C':('9CC', '27 deg, 0,9 V + impulsion 1,5 V x 1 ms', B(0.9, clic=1.5)),
  'fin': ('FIN', '12 deg (30 crans/tour), 0,7 V', B(0.7, w=12*DEG, dz=0.3*DEG)),
  'libre': ('LIB', 'aucun cran : roue libre, juste le frottement du moteur', B(0.0, d=0.0)),
}
if __name__ == '__main__' and len(sys.argv) < 2:
    for k, (_, txt, _) in PROFILS.items():
        print(f'  {k:7s} {txt}')
    sys.exit(0)
def charge(L, nom, retouches=None):
    """Charge un profil SANS couper le pont : on passe du mode 6 au mode 6,
    donc pas d'a-coup ; le zero reste la ou il est."""
    aff, txt, p = PROFILS[nom]
    p = dict(p); p.update(retouches or {})
    L.set(**{k: v for k, v in COMMUN.items() if k != 'mode'}, **p)
    L.set(mode=6)
    L.cmd(f'aff {aff}', motif='#= aff')     # nom du profil sur la matrice LED
    return txt

if __name__ == '__main__':
    r = {}
    for a in sys.argv[2:]:
        k, v = a.split('='); r[k] = float(v)
    L = Lab()
    txt = charge(L, sys.argv[1], r)
    L.close()
    print(f'profil {sys.argv[1]} : {txt}')
