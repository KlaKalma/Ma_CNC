#!/usr/bin/env python3
"""gen_cogging.py -- fabrique cogging.h a partir des cartes e09 (moyenne).
Table de 512 points en mV, indexee par le brut capteur (raw14 >> 5).
c(a) > 0 = le cogging pousse vers les angles croissants ; le firmware
ajoute -cog * c(a)."""
import sys, os, numpy as np
HERE = os.path.dirname(os.path.abspath(__file__))
cartes = sys.argv[1:] or [os.path.join(HERE, 'mesures', f) for f in ('e09_carte_1.npz', 'e09_carte.npz')]
cs = [np.load(f)['cog'] for f in cartes]
c = np.nanmean(np.vstack(cs), axis=0)
# trous eventuels : interpolation circulaire
n = len(c); x = np.arange(n); ok = ~np.isnan(c)
c = np.interp(x, x[ok], c[ok], period=n)
c = c - c.mean()                                   # le cogging n'a pas de moyenne
c = c.reshape(512, -1).mean(axis=1)                # 1024 -> 512
# lissage leger circulaire (3 points) : retire le bruit sans emousser les pics
c = (np.roll(c, 1) + 2 * c + np.roll(c, -1)) / 4
mv = np.round(c * 1000).astype(int)
lignes = ', '.join(str(v) for v in mv)
with open(os.path.join(HERE, 'cogging.h'), 'w') as f:
    f.write('/* cogging.h -- GENERE par gen_cogging.py a partir de : %s\n'
            ' * c(a) en mV, 512 points sur un tour, indexe par raw14 >> 5.\n'
            ' * c > 0 : le cogging pousse vers les angles croissants. */\n'
            '#pragma once\n#include <stdint.h>\n'
            'static const int16_t COGGING_MV[512] = {\n%s\n};\n'
            % (', '.join(os.path.basename(x) for x in cartes), lignes))
print(f'cogging.h : crete-a-crete {mv.max()-mv.min()} mV, rms {mv.std():.1f} mV')
