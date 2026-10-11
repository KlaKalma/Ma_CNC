#!/usr/bin/env python3
"""e04 -- quel facteur fige la sortie USB du pont ESP32 ?
Cycles 'coup' : vecteur a +90 deg electriques (couple max, mode 4) pendant
80 ms, puis arret 300 ms. Trois variantes :
  1 flux + frein (mode 1 : phases a 50 %, freinage par recuperation)
  2 sans flux + frein
  3 flux + roue libre (mode 0 : pont coupe, aucune recuperation)
Apres chaque cycle, '?' verifie la liaison ; en cas de silence, le port USB
est reinitialise et l'essai continue."""
import sys, os, time
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from lab import *

N = int(sys.argv[1]) if len(sys.argv) > 1 else 20
MAG = float(sys.argv[2]) if len(sys.argv) > 2 else 0.6
DLT = float(sys.argv[3]) if len(sys.argv) > 3 else 0.5   # rad, sens du couple
VARIANTES = [('flux+frein', 1, 1), ('sans flux+frein', 0, 1), ('flux+roue libre', 1, 0)]

L = Lab()
bilan = {}
for nom, flux, arret in VARIANTES:
    gels = 0; vmax = 0
    L.set(mode=arret, mag=MAG, dlt=DLT, stream=0)
    for i in range(N):
        L.frames = []; L.recording = bool(flux)
        L.set(stream=flux)
        try:
            L.set(mode=4); time.sleep(0.08); L.set(mode=arret); time.sleep(0.3)
            L.set(stream=0)
            ok = vivante(L)
        except RuntimeError:
            ok = False
        L.recording = False
        if flux and ok and len(L.frames) > 50:
            v = vitesse(decode(L.frames, {'per': 500}))
            vmax = max(vmax, np.abs(v).max())
        if not ok:
            gels += 1
            print(f'  {nom}: GEL au cycle {i}', flush=True)
            L.close(); usb_reset(); L = Lab()
            L.set(mode=arret, mag=MAG, dlt=DLT, stream=0)
    bilan[nom] = (gels, vmax)
    print(f'{nom:18s} gels={gels}/{N}  vmax={vmax:.1f} rad/s', flush=True)
L.set(mode=0); L.close()
