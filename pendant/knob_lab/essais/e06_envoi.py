#!/usr/bin/env python3
"""e06 -- la facon d'ENVOYER les commandes fige-t-elle le pont USB ?
Memes cycles coup + frein avec flux. Variante 'bloc' : chaque commande en un
seul write(). Variante 'octets' : un write() par octet (comme Lab.cmd).
Variante 'octets sans moteur' : mode 1 seulement, aucun mouvement."""
import sys, os, time
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from lab import *

N = int(sys.argv[1]) if len(sys.argv) > 1 else 10
TOUTES = {'bloc': ('bloc', 0, 1), 'octets': ('octets', 1, 1),
          'octets0': ('octets sans moteur', 1, 0), 'bloc0': ('bloc sans moteur', 0, 0)}
DEC = int(os.environ.get('DEC', '1'))
VARIANTES = [TOUTES[k] for k in (sys.argv[2:] or ['bloc', 'octets', 'octets0'])]

def envoi(L, ligne, octets):
    if octets:
        for c in (ligne + '\n').encode():
            L.s.write(bytes([c])); L.s.flush(); time.sleep(0.0003)
    else:
        L.s.write((ligne + '\n').encode())

def cycle(L, octets, moteur):
    envoi(L, 'stream 1', octets); time.sleep(0.05)
    envoi(L, 'mode 4' if moteur else 'mode 1', octets); time.sleep(0.08)
    envoi(L, 'mode 1', octets); time.sleep(0.3)
    envoi(L, 'stream 0', octets); time.sleep(0.05)
    return vivante(L)

L = relance(Lab())
L.set(mode=1, mag=0.6, dlt=0.5, stream=0, lien=0, dec=DEC)
for nom, octets, moteur in VARIANTES:
    gels = 0
    for i in range(N):
        if not cycle(L, octets, moteur):
            gels += 1
            L = relance(L)
            L.set(mode=1, mag=0.6, dlt=0.5, stream=0, lien=0, dec=DEC)
    print(f'{nom:20s} dec={DEC} gels={gels}/{N}', flush=True)
L.set(mode=0, lien=1.5); L.close()
