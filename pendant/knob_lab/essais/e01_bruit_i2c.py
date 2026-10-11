#!/usr/bin/env python3
"""e01 -- taux d'erreur I2C selon l'etat du pont et l'horloge du bus.
Rotor au repos, aucune main. Pour chaque cas : erreurs signalees, mots
dont les bits reserves sont non nuls, et sauts > 20 LSB qui PASSENT
quand meme (ceux-la sont les dangereux : invisibles pour le firmware)."""
import sys, os, time
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from lab import *

CAS = [
    ('pont coupe',      dict(mode=0)),
    ('Uq=0',            dict(mode=1)),
    ('vecteur 0.5 V',   dict(mode=3, mag=0.5, ang=0)),
    ('vecteur 1.5 V',   dict(mode=3, mag=1.5, ang=0)),
]
with Lab() as L:
    L.set(rbit=0, thr=0)
    for khz in (400, 1000):
        L.set(i2c=khz)
        for nom, kw in CAS:
            L.set(**kw); time.sleep(0.3)
            r = L.rec(2.0)
            m = r['mot']; e = r['err']
            bad_bits = (~e) & ((m & 3) != 0)
            d = np.abs(np.diff(r['acc']))
            ok = ~(e[1:] | e[:-1] | bad_bits[1:] | bad_bits[:-1])
            print(f'{khz:5d} kHz  {nom:14s} err={e.mean()*100:5.1f}%  '
                  f'bits_res={bad_bits.mean()*100:5.1f}%  '
                  f'sauts>20 non signales={int((d[ok] > 20).sum()):4d}  '
                  f'occ={r["occ"].mean():4.0f}us  n={len(m)}', flush=True)
        L.set(mode=0)
