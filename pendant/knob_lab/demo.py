#!/usr/bin/env python3
"""demo.py -- fait defiler les profils toutes les 10 s, nom sur la matrice.

    ./demo.py                     tous les profils doux/clic
    ./demo.py B B12 B09 B09c      une liste choisie
    ./demo.py -t 6 B B09          toutes les 6 s
Ctrl-C : arrete et laisse le volant sur le profil courant."""
import sys, os, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from lab import Lab
from profil import PROFILS, charge

args = sys.argv[1:]
periode = 10.0
if args[:1] == ['-t']:
    periode = float(args[1]); args = args[2:]
noms = args or ['ar', 'p100', 'menu']
L = Lab()
try:
    while True:
        for nom in noms:
            L.set(mode=0); L.zero()
            txt = charge(L, nom)
            print(f'{time.strftime("%H:%M:%S")}  [{PROFILS[nom][0]:>3s}] {nom:5s} {txt}', flush=True)
            time.sleep(periode)
except KeyboardInterrupt:
    print('\narret -- le volant reste sur le dernier profil')
finally:
    L.close()
