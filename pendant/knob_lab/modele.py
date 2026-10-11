#!/usr/bin/env python3
"""modele.py -- modele du volant identifie sur la reponse en frequence (e11),
et analyse de stabilite d'une loi u = -K*angle - D*v_est, en temps discret.

    P(s) = exp(-s*Td) / ( s * (J s + Ke) * (1 + s*Te) )      (u en V -> rad)

Le correcteur echantillonne a T : u[n] = -K a[n] - D (a[n]-a[n-nv])/(nv T)."""
import os, sys
import numpy as np
from scipy.optimize import least_squares

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, 'mesures')
KE = 0.0224          # V.s/rad, mesure par echelons (e08), recoupe e11 a 20 Hz

def P(w, J, Te, Td, Ke=KE):
    s = 1j * w
    return np.exp(-s * Td) / (s * (J * s + Ke) * (1 + s * Te))

def ajuste(fichier=os.path.join(OUT, 'e11_bode.npz')):
    z = np.load(fichier); f, H = z['f'], z['H']
    w = 2 * np.pi * f
    def res(x):
        J, Te, Td = x
        e = np.log(P(w, J, Te, Td) / H)
        return np.concatenate([e.real, e.imag])
    x = least_squares(res, [1.6e-4, 3e-4, 1.2e-3], bounds=([1e-5, 0, 0], [1e-2, 5e-3, 5e-3])).x
    return dict(J=x[0], Te=x[1], Td=x[2]), f, H

def C(w, K, D, nv, T):
    z = np.exp(1j * w * T)
    return K + D * (1 - z ** (-nv)) / (nv * T)

def marges(K, D, nv=4, T=0.0005, J=1.6e-4, Te=3e-4, Td=1.2e-3):
    """Marges sur la boucle L = P*C. Rend (marge de gain, marge de phase en deg,
    f de coupure Hz, f a -180 Hz). Marge de gain < 1 => instable."""
    w = 2 * np.pi * np.logspace(-0.5, np.log10(0.5 / T), 6000)
    L = P(w, J, Te, Td) * C(w, K, D, nv, T)
    mag = np.abs(L); ph = np.unwrap(np.angle(L))
    # passage de la phase par -180 (premier, en partant du bas)
    i180 = np.nonzero(np.diff(np.sign(ph + np.pi)))[0]
    mg = 1 / mag[i180[0]] if len(i180) else np.inf
    f180 = w[i180[0]] / 2 / np.pi if len(i180) else np.nan
    ic = np.nonzero(np.diff(np.sign(mag - 1)))[0]
    pm = np.degrees(ph[ic[-1]] + np.pi) if len(ic) else np.nan
    fc = w[ic[-1]] / 2 / np.pi if len(ic) else np.nan
    return mg, pm, fc, f180

if __name__ == '__main__':
    m, f, H = ajuste()
    w = 2 * np.pi * f
    err = np.abs(np.log(P(w, **m) / H))
    print(f"modele : J={m['J']:.3e} V.s2/rad  Te={m['Te']*1e3:.2f} ms  Td={m['Td']*1e3:.2f} ms  "
          f"(Ke={KE})  erreur rms {np.sqrt(np.mean(err**2))*100:.1f} %")
    print('\n  K(V/rad)   D     nv  marge_gain  marge_phase  f_coupure  f_-180')
    for K, D, nv in ((6.6, 0.2, 4), (6.6, 0.1, 4), (6.6, 0.05, 4), (6.6, 0.02, 4), (6.6, 0.0, 4),
                     (15.7, 0.19, 4), (15.7, 0.04, 4), (15.7, 0.0, 4), (30, 0.0, 4), (60, 0.0, 4), (100, 0.0, 4)):
        mg, pm, fc, f180 = marges(K, D, nv, **m)
        print(f'  {K:6.1f}  {D:5.2f}  {nv:3d}   {mg:8.2f}    {pm:8.1f}     {fc:6.1f}   {f180:6.1f}')
