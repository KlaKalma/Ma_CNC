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
PROFILS = {
  'ancien': ('ancienne loi (D trop fort) : POUR COMPARER, elle vibre',
             dict(w=27*DEG, kd=1.6, dz=1.0*DEG, snap=1.1, d=0.20, nv=4, cog=0)),
  'A':  ('27 deg, 1,6 V, amortissement corrige, cogging NON compense',
         dict(w=27*DEG, kd=1.6, dz=0.5*DEG, snap=1.1, d=0.045, nv=2, cog=0)),
  'B':  ('27 deg, 1,6 V, amortissement corrige, cogging compense (demarrage)',
         dict(w=27*DEG, kd=1.6, dz=0.5*DEG, snap=1.1, d=0.045, nv=2, cog=1)),
  'C':  ('27 deg, 2,0 V (plus ferme), cogging compense',
         dict(w=27*DEG, kd=2.0, dz=0.5*DEG, snap=1.1, d=0.045, nv=2, cog=1)),
  'fin': ('12 deg (30 crans/tour), 1,0 V, cogging compense',
          dict(w=12*DEG, kd=1.0, dz=0.3*DEG, snap=1.1, d=0.045, nv=2, cog=1)),
  'libre': ('aucun cran : roue libre, juste le frottement du moteur',
            dict(w=27*DEG, kd=0.0, dz=0.0, snap=1.1, d=0.0, nv=2, cog=1)),
}
if len(sys.argv) < 2:
    for k, (txt, _) in PROFILS.items():
        print(f'  {k:7s} {txt}')
    sys.exit(0)
txt, p = PROFILS[sys.argv[1]]
p = dict(p)
for a in sys.argv[2:]:
    k, v = a.split('='); p[k] = float(v)
L = Lab()
L.set(mode=0); L.set(**{k: v for k, v in COMMUN.items() if k != 'mode'}, **p)
L.zero(); L.set(mode=6)
L.close()
print(f'profil {sys.argv[1]} : {txt}')
