#!/usr/bin/env python3
"""e03 -- qu'est-ce qui fige la carte ? Moteur en rotation lente (mode 2,
couple constant, coupe a vtest), A : sans flux, B : avec flux 2 kHz.
Une question '?' toutes les 0,3 s mesure si la liaison vit. Au premier
silence, la sonde SWD regarde l'interieur du RA4M1."""
import sys, os, time, subprocess
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from lab import *

UQ = float(sys.argv[1]) if len(sys.argv) > 1 else 0.3
ICI = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

def sonde():
    print('--- SONDE SWD ---', flush=True)
    r = subprocess.run([os.path.join(ICI, 'sonde.sh')], env=dict(os.environ, SUDO='sudo -n'),
                       capture_output=True, text=True)
    print(r.stdout[-1200:], r.stderr[-400:], flush=True)

L = Lab()
try:
    for phase, flux in (('A sans flux', 0), ('B avec flux', 1)):
        L.frames = []; L.recording = bool(flux)
        L.set(stream=flux, mode=2, uq=UQ)
        t0 = time.time(); a0 = None; muet = 0
        while time.time() - t0 < 10:
            r = L.cmd('?', attente=0.4, motif='#P fin')
            ok = any('fin' in x for x in r)
            reset = [x for x in r if 'reset' in x or 'pret' in x]
            if reset: print('  !!', reset, flush=True)
            muet = 0 if ok else muet + 1
            if muet >= 3:
                print(f'{phase}: CARTE MUETTE apres {time.time()-t0:.1f} s', flush=True)
                sonde(); raise SystemExit(1)
        n = len(L.frames)
        L.set(stream=0, mode=0); L.recording = False
        extra = ''
        if flux:
            r = decode(L.frames, {'per': 500})
            v = vitesse(r)
            extra = f'trames={n} perdues={r["perdues"]} v_moy={np.mean(np.abs(v)):.1f} rad/s err={r["err"].mean()*100:.1f}%'
        print(f'{phase}: OK 10 s  {extra}', flush=True)
        time.sleep(1)
finally:
    try: L.cmd('mode 0'); L.close()
    except Exception: pass
