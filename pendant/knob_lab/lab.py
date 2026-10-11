#!/usr/bin/env python3
"""lab.py -- hote du banc de mesure knob_lab.

Module (pour les scripts d'essai) :
    from lab import Lab
    with Lab() as L:
        L.set(mode=5, k=10, d=0.1)
        r = L.rec(2.0)            # dict de tableaux numpy, un point par echantillon
        L.report(r, 'essai.png', 'titre')

Ligne de commande :
    ./lab.py rec 3 mode=1 -o repos      enregistre 3 s, rapport repos.png + repos.npz
    ./lab.py set mode=0                 regle et sort
    ./lab.py show fichier.npz           refait le rapport d'un enregistrement

Le capteur arrive BRUT (mot I2C h<<8|l) : le deroulement, la vitesse et
toutes les statistiques sont calcules ici, independamment du firmware.
"""
import sys, time, threading, struct, os
import numpy as np
import serial

LSB = 2 * np.pi / 16384
PORT = os.environ.get('KNOB_PORT', '/dev/ttyACM0')
BAUD = 2000000
HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, 'mesures')

FL_ERR, FL_REJ, FL_EN, FL_DIAG, FL_INJ, FL_SAT, FL_VCUT, FL_CRAN = (
    1, 2, 4, 8, 16, 32, 64, 128)


class Lab:
    def __init__(self, port=PORT):
        self.s = serial.Serial(port, BAUD, timeout=0.05)
        self.buf = bytearray()
        self.frames = []          # liste de bytes de 10 (charge utile)
        self.txt = []
        self.lock = threading.Lock()
        self.recording = False
        self.badsum = 0
        self.alive = True
        self.th = threading.Thread(target=self._lit, daemon=True)
        self.th.start()
        time.sleep(0.1)

    def __enter__(self):
        return self

    def __exit__(self, *a):
        try:
            self.cmd('stream 0')
            self.cmd('mode 0')
        finally:
            self.close()

    def close(self):
        self.alive = False
        time.sleep(0.1)
        self.s.close()

    # ---------------------------------------------------------------- lecture
    def _lit(self):
        while self.alive:
            try:
                d = self.s.read(4096)
            except Exception:
                return
            if not d:
                continue
            self.buf += d
            self._decoupe()

    def _decoupe(self):
        b = self.buf
        i = 0
        n = len(b)
        while i < n:
            c = b[i]
            if c == 0xA5:
                if i + 12 > n:
                    break
                x = 0
                for k in range(1, 11):
                    x ^= b[k + i]
                if x == b[i + 11]:
                    if self.recording:
                        self.frames.append(bytes(b[i + 1:i + 11]))
                    i += 12
                    continue
                self.badsum += 1
                i += 1
            elif c == 0x5A:
                if i + 2 > n:
                    break
                ln = b[i + 1]
                part = b[i + 2:min(n, i + 2 + ln)]
                if ln == 0 or any(v < 0x20 or v > 0x7E for v in part):
                    self.badsum += 1
                    i += 1
                    continue
                if i + 3 + ln > n:
                    break
                t = b[i + 2:i + 2 + ln]
                # un 0x5A au milieu d'une trame de donnees ferait attendre
                # jusqu'a 255 octets : le texte est ASCII imprimable, sinon
                # ce n'est pas un debut de trame texte
                if ln == 0 or any(v < 0x20 or v > 0x7E for v in t):
                    self.badsum += 1
                    i += 1
                    continue
                x = 0
                for v in t:
                    x ^= v
                if x == b[i + 2 + ln]:
                    with self.lock:
                        self.txt.append(t.decode('ascii', 'replace'))
                    i += 3 + ln
                    continue
                self.badsum += 1
                i += 1
            else:
                i += 1
        del b[:i]

    # --------------------------------------------------------------- commande
    def cmd(self, line, attente=0.3, motif=None):
        with self.lock:
            self.txt.clear()
        # UN SEUL write() par commande. Mesure (e06) : un write() par octet,
        # pendant que la carte streame, FIGE le pont USB de l'ESP32 dans le
        # sens carte -> PC (3 fois sur 10, avec ou sans moteur) ; en bloc,
        # 0 sur 10. Les pertes de caractere se rattrapent par set() qui
        # retente tant que l'accuse ne revient pas.
        self.s.write((line + '\n').encode())
        t0 = time.time()
        while time.time() - t0 < attente:
            with self.lock:
                if motif and any(motif in t for t in self.txt):
                    break
            time.sleep(0.005)
        with self.lock:
            return list(self.txt)

    def set(self, **kw):
        for k, v in kw.items():
            k = 'if' if k == 'if_' else k
            for essai in range(4):
                r = self.cmd(f'{k} {v}', motif=f'#= {k} ')
                if any(t.startswith(f'#= {k} ') for t in r):
                    break
            else:
                raise RuntimeError(f'pas d accuse pour {k}={v}: {r}')

    def params(self):
        r = self.cmd('?', attente=1.0, motif='#P fin')
        p = {}
        for t in r:
            if t.startswith('#P ') and not t.startswith('#P fin'):
                _, k, v = t.split()
                p[k] = float(v)
        return p

    def go(self):
        self.cmd('go', motif='# go')

    def zero(self):
        self.cmd('z', motif='# zero')

    # ---------------------------------------------------------- enregistrement
    def rec(self, duree, nom=None, **seq):
        """Enregistre 'duree' secondes. UNE seule ecriture ('rec N'), puis
        lecture seule : le firmware s'arrete tout seul. Ecrire pendant le flux
        fige le pont USB de la R4 (e07). Les changements de mode pendant
        l'enregistrement se programment AVANT : s1n/s1m, s2n/s2m."""
        if seq:
            self.set(**seq)
        p = self.params()
        ts = p.get('per', 500) * 1e-6
        n = int(round(duree / ts))
        with self.lock:
            self.txt.clear()
        self.frames = []
        self.recording = True
        for essai in range(4):
            self.s.write(f'rec {n}\n'.encode())
            t1 = time.time() + 0.15
            parti = False
            while time.time() < t1 and not parti:
                time.sleep(0.005)
                with self.lock:
                    parti = any(t.startswith('# rec ') and 'fin' not in t
                                for t in self.txt)
            if parti:
                break
        else:
            raise RuntimeError('rec : pas d accuse')
        t1 = time.time() + duree + 2.0
        fini = False
        while time.time() < t1 and not fini:
            time.sleep(0.02)
            with self.lock:
                fini = any('rec fin' in t for t in self.txt)
        time.sleep(0.05)
        self.recording = False
        if not fini:
            raise RuntimeError(f'rec : pas de fin ({len(self.frames)} trames)')
        if seq:
            self.set(**{k: 0 for k in seq if k.endswith('n')})
        r = decode(self.frames, p)
        r['badsum'] = self.badsum
        if nom:
            sauve(r, nom)
        return r

    def vie(self, duree):
        """Attend en envoyant un octet de vie toutes les 0,3 s : sans lui, le
        firmware coupe le pont des modes d'essai (parametre 'lien')."""
        t1 = time.time() + duree
        while True:
            r = t1 - time.time()
            if r <= 0:
                return
            time.sleep(min(0.3, r))
            self.s.write(b'\n')

    def report(self, r, fichier, titre=''):
        return report(r, fichier, titre)


def usb_reset(attente=8.0):
    """Debloque le pont USB de la R4 sans toucher a la carte : desactive puis
    reactive le port du Pi, ce qui force une reenumeration de l'ESP32. Le RA4M1
    n'est PAS redemarre (verifie : il garde ses parametres). Mesure le
    2026-10-11 : c'est le sens carte -> PC du pont qui cale, pas le RA4M1."""
    import subprocess
    p = '/sys/bus/usb/devices/1-0:1.0/usb1-port1/disable'
    for v in ('1', '0'):
        subprocess.run(['sudo', '-n', 'tee', p], input=v.encode(),
                       stdout=subprocess.DEVNULL, check=True)
        time.sleep(2.0)
    t1 = time.time() + attente
    while time.time() < t1 and not os.path.exists(PORT):
        time.sleep(0.2)
    time.sleep(1.0)


def relance(L=None, essais=4):
    """Rend une liaison vivante, quitte a reinitialiser le port USB."""
    if L is not None:
        try:
            if vivante(L):
                return L
            L.close()
        except Exception:
            pass
    for _ in range(essais):
        usb_reset()
        L = Lab()
        if vivante(L):
            return L
        L.close()
    raise RuntimeError('carte injoignable apres reinitialisations USB')


def vivante(L, essais=3):
    for _ in range(essais):
        r = L.cmd('?', attente=0.5, motif='#P fin')
        if any('fin' in x for x in r):
            return True
    return False


# ======================================================================
def decode(frames, p):
    a = np.frombuffer(b''.join(frames), dtype=np.uint8).reshape(-1, 10)
    seq = a[:, 0].astype(np.int32)
    mot = (a[:, 1].astype(np.int32) << 8) | a[:, 2]
    u = ((a[:, 3].astype(np.int16) << 8) | a[:, 4]).astype(np.int16) / 1000.0
    fl = a[:, 5].astype(np.int32)
    dt = a[:, 6].astype(np.float64) * 4
    occ = a[:, 7].astype(np.float64) * 4
    vfw = ((a[:, 8].astype(np.int16) << 8) | a[:, 9]).astype(np.int16) / 100.0
    raw = mot >> 2
    err = (fl & FL_ERR) != 0
    # deroulement sur le brut, lectures en erreur = valeur precedente
    rr = raw.astype(np.int64).copy()
    for i in np.nonzero(err)[0]:
        rr[i] = rr[i - 1] if i else rr[i]
    d = np.diff(rr)
    d = (d + 8192) % 16384 - 8192
    acc = np.concatenate([[0], np.cumsum(d)]) + rr[0]
    pts = p.get('per', 500) * 1e-6 * max(1, p.get('dec', 1))
    gaps = (np.diff(seq) % 256) - 1
    return dict(seq=seq, mot=mot, raw=raw, acc=acc, ang=acc * LSB, u=u,
                fl=fl, dt=dt, occ=occ, vfw=vfw, err=err, ts=pts,
                t=np.arange(len(seq)) * pts, perdues=int(gaps.sum()),
                p=p)


def sauve(r, nom):
    os.makedirs(OUT, exist_ok=True)
    f = os.path.join(OUT, nom + '.npz')
    q = {k: v for k, v in r.items() if k != 'p'}
    q['pk'] = np.array(list(r['p'].keys()))
    q['pv'] = np.array(list(r['p'].values()))
    np.savez_compressed(f, **q)
    return f


def charge(f):
    z = np.load(f)
    r = {k: z[k] for k in z.files if k not in ('pk', 'pv')}
    r['p'] = dict(zip(z['pk'], z['pv']))
    for k in ('ts', 'perdues', 'badsum'):
        if k in r:
            r[k] = r[k].item()
    return r


# ======================================================================
def vitesse(r, n=4):
    """Vitesse non causale (difference centree sur 2n echantillons)."""
    a = r['ang']
    v = np.zeros_like(a)
    v[n:-n] = (a[2 * n:] - a[:-2 * n]) / (2 * n * r['ts'])
    return v


def spectre(x, ts, nseg=None):
    from scipy.signal import welch
    x = np.asarray(x, float)
    nseg = nseg or min(len(x), 4096)
    f, P = welch(x - x.mean(), fs=1 / ts, nperseg=nseg)
    return f, np.sqrt(P)          # densite d'amplitude, unite/sqrt(Hz)


def stats(r):
    a, u = r['ang'], r['u']
    d = np.diff(r['acc'])
    s = dict(
        n=len(a), duree=len(a) * r['ts'], perdues=r['perdues'],
        badsum=r.get('badsum', 0),
        err_i2c=int(r['err'].sum()),
        rejets=int(((r['fl'] & FL_REJ) != 0).sum()),
        diag=int(((r['fl'] & FL_DIAG) != 0).sum()),
        sat=float(((r['fl'] & FL_SAT) != 0).mean()),
        vcut=int(((r['fl'] & FL_VCUT) != 0).sum()),
        crans=int(((r['fl'] & FL_CRAN) != 0).sum()),
        ang_pp_lsb=int(r['acc'].max() - r['acc'].min()),
        ang_std_lsb=float(r['acc'].std()),
        saut_max_lsb=int(np.abs(d).max()) if len(d) else 0,
        sauts_gt5=int((np.abs(d) > 5).sum()),
        sauts_gt20=int((np.abs(d) > 20).sum()),
        u_moy=float(u.mean()), u_rms_ac=float(u.std()),
        u_pp=float(u.max() - u.min()),
        dt_moy=float(r['dt'][1:].mean()), dt_max=float(r['dt'][1:].max()),
        occ_moy=float(r['occ'].mean()), occ_max=float(r['occ'].max()),
        bas2=np.bincount(r['mot'] & 3, minlength=4).tolist(),
    )
    v = vitesse(r)
    s['v_rms'] = float(np.sqrt(np.mean(v ** 2)))
    # frequence dominante de l'angle et du couple
    if len(a) > 256:
        for nom, x in (('ang', a), ('u', u)):
            f, A = spectre(x, r['ts'])
            m = f > 2
            k = np.argmax(A[m])
            s[f'fpic_{nom}'] = float(f[m][k])
    return s


def texte_stats(s):
    lignes = []
    for k, v in s.items():
        if isinstance(v, float):
            v = f'{v:.4g}'
        lignes.append(f'{k}={v}')
    return '  '.join(lignes)


def report(r, fichier, titre=''):
    import matplotlib
    matplotlib.use('Agg')
    import matplotlib.pyplot as plt
    s = stats(r)
    t = r['t']
    v = vitesse(r)
    fig, ax = plt.subplots(3, 2, figsize=(15, 10))
    ax = ax.ravel()
    a0 = r['ang'][0]
    ax[0].plot(t, (r['ang'] - a0) * 1000, lw=0.6)
    ax[0].set_ylabel('angle - a0 (mrad)')
    e = np.nonzero(r['err'])[0]
    if len(e):
        ax[0].plot(t[e], (r['ang'][e] - a0) * 1000, 'rx', ms=4, label='err I2C')
        ax[0].legend()
    ax[1].plot(t, r['u'] * 1000, lw=0.6, label='u applique')
    ax[1].set_ylabel('u (mV)')
    ax[2].plot(t, v, lw=0.6, label='vitesse hote (centree)')
    ax[2].plot(t, r['vfw'], lw=0.5, alpha=0.7, label='vitesse firmware')
    ax[2].set_ylabel('rad/s'); ax[2].legend(fontsize=8)
    d = np.diff(r['acc'])
    ax[3].plot(t[1:], d, lw=0.5)
    ax[3].set_ylabel('saut par echantillon (LSB)')
    for k, (x, lab) in enumerate(((r['ang'], 'angle (rad/sqrtHz)'),
                                  (r['u'], 'u (V/sqrtHz)'))):
        if len(x) > 256:
            f, A = spectre(x, r['ts'])
            ax[4 + k].loglog(f[1:], A[1:], lw=0.7)
            ax[4 + k].set_xlabel('Hz'); ax[4 + k].set_ylabel(lab)
            ax[4 + k].grid(True, which='both', alpha=0.3)
    for k in range(4):
        ax[k].set_xlabel('s'); ax[k].grid(alpha=0.3)
    p = r['p']
    pt = ' '.join(f'{k}={p[k]:g}' for k in p
                  if k in ('mode', 'uq', 'k', 'd', 'nv', 'tau', 'thr', 'mag',
                           'dlt', 'kd', 'w', 'dz', 'snap', 'it', 'ia', 'if',
                           'vz', 'sens', 'zea'))
    st = texte_stats(s)
    fig.suptitle(f'{titre}\n{pt}\n' + '\n'.join(
        st[i:i + 200] for i in range(0, len(st), 200)), fontsize=8)
    fig.tight_layout()
    os.makedirs(OUT, exist_ok=True)
    f = fichier if os.path.isabs(fichier) else os.path.join(OUT, fichier)
    fig.savefig(f, dpi=80)
    plt.close(fig)
    return s


# ======================================================================
def _kv(args):
    kw = {}
    for a in args:
        k, v = a.split('=')
        kw[k] = v
    return kw


if __name__ == '__main__':
    if len(sys.argv) < 2:
        print(__doc__); sys.exit(0)
    c = sys.argv[1]
    if c == 'show':
        r = charge(sys.argv[2])
        s = report(r, sys.argv[2].replace('.npz', '.png'))
        print(texte_stats(s))
        sys.exit(0)
    args = sys.argv[2:]
    nom = 'essai'
    if '-o' in args:
        i = args.index('-o'); nom = args[i + 1]; args = args[:i] + args[i + 2:]
    L = Lab()
    try:
        if c == 'set':
            L.set(**_kv(args))
        elif c == 'rec':
            dur = float(args[0])
            L.set(**_kv(args[1:]))
            time.sleep(0.2)
            r = L.rec(dur, nom=nom)
            s = report(r, nom + '.png', nom)
            print(texte_stats(s))
        elif c == 'params':
            for k, v in L.params().items():
                print(k, v)
    finally:
        if c != 'set':
            L.cmd('stream 0'); L.cmd('mode 0')
        L.close()
