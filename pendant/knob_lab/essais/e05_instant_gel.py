#!/usr/bin/env python3
"""e05 -- a quel instant la sortie USB cale-t-elle ? Un cycle coup + frein
avec flux, horodatage hote de chaque paquet recu, et les dernieres trames."""
import sys, os, time
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from lab import *

def un_cycle(L, mag, arret):
    L.set(mode=arret, mag=mag, dlt=0.5, stream=0)
    L.frames = []; L.recording = True
    L.set(stream=1)
    t_cmd = {}
    t0 = time.time()
    L.s.write(b'mode 4\n'); t_cmd['m4'] = time.time() - t0
    time.sleep(0.08)
    L.s.write(b'mode %d\n' % arret); t_cmd['arret'] = time.time() - t0
    time.sleep(0.4)
    n = len(L.frames)
    try:
        L.set(stream=0)
    except RuntimeError:
        pass
    L.recording = False
    ok = vivante(L)
    return ok, n, t_cmd

for i in range(int(sys.argv[1]) if len(sys.argv) > 1 else 3):
    if not os.path.exists(PORT):
        time.sleep(3)
    L = Lab()
    if not vivante(L):
        print('carte muette au depart -> reset USB'); L.close(); usb_reset(); L = Lab()
    ok, n, tc = un_cycle(L, 0.6, 1)
    r = decode(L.frames, {'per': 500}) if L.frames else None
    print(f'cycle {i}: vivant={ok} trames={n} (attendu ~{int(0.48/0.0005)})', flush=True)
    if r is not None and len(r['u']):
        v = vitesse(r, 2)
        i4 = np.nonzero(r['u'] > 0)[0]
        print(f'   trames en mode 4: {len(i4)}  premiere={i4[0] if len(i4) else None} '
              f'derniere={i4[-1] if len(i4) else None}  total={len(r["u"])}  vmax={np.abs(v).max():.1f}')
        k = len(r['u'])
        for j in range(max(0, k - 6), k):
            print(f'   ech {j}: seq={r["seq"][j]} u={r["u"][j]:.2f} fl=0x{r["fl"][j]:02x} '
                  f'v={r["vfw"][j]:.1f} dt={r["dt"][j]:.0f} occ={r["occ"][j]:.0f}')
    if not ok:
        L.close(); usb_reset(); L = Lab()
    L.set(mode=0, stream=0); L.close()
