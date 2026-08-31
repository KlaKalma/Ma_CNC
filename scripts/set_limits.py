#!/usr/bin/env python3
"""
set_limits.py — Regler les SOFT LIMITS d'un axe a la main, par jog — X / Y / Z

POURQUOI
  Pas encore de capteurs de fin de course installes -> pas de touch-off possible.
  Mais la machine a DEJA un repere machine sans switch (X = encodeur absolu,
  Y/Z = home immediat). On peut donc jogger jusqu'a un bout de course, capturer
  la position machine courante, et l'ecrire comme MIN_LIMIT ou MAX_LIMIT.
  LinuxCNC refusera alors tout mouvement (jog ou G-code) au-dela, une fois l'axe
  home. Les limites sont lues dans l'INI au demarrage -> REDEMARRAGE necessaire.

PROCEDURE (par axe, une fois)
  1. LinuxCNC lance, machine ON (F2), Home All fait.
  2. ATTENTION : tant que les limites valent +/-10000, RIEN ne te protege pendant
     cette exploration. Jogge LENTEMENT (jog incremental 1 mm / 0.1 mm pres du bout,
     ou continu a faible %) jusqu'a la position extreme que tu veux AUTORISER.
  3. Lance depuis un terminal :
         cd /home/cnc/linuxcnc/configs/Ma_CNC/scripts
         python3 set_limits.py z max        # capture le bout + de Z
     puis jogge a l'autre extremite et :
         python3 set_limits.py z min        # capture le bout - de Z
  4. Le script ecrit MIN_LIMIT / MAX_LIMIT dans ../LC10E.ini (sections [AXIS_Z]
     ET [JOINT_2]), sauvegarde .bak.
  5. REDEMARRE LinuxCNC, Home All. Les soft limits sont actives.

MARGE DE SECURITE
  --margin N  retire N mm vers l'interieur (max -> pos-N, min -> pos+N).
  Recommande 1 a 3 mm pour ne pas coller le bout mecanique (depassement de
  poursuite, arret non instantane). Defaut 0 (limite exactement a la position lue).

AUTRES
  set_limits.py show            affiche positions courantes + limites actuelles
  set_limits.py z max --value 250   force une valeur au lieu de lire la position
  --write                       ecrit sans demander confirmation

Aucune dependance externe : lit la position via halcmd (LinuxCNC doit tourner).
"""
import subprocess, sys, re, shutil, os, argparse

HERE = os.path.dirname(os.path.abspath(__file__))
INI  = os.path.normpath(os.path.join(HERE, "..", "LC10E.ini"))

AXES = {  # lettre -> (n_joint, section AXIS, section JOINT)
    "x": (0, "AXIS_X", "JOINT_0"),
    "y": (1, "AXIS_Y", "JOINT_1"),
    "z": (2, "AXIS_Z", "JOINT_2"),
}
KEY = {"min": "MIN_LIMIT", "max": "MAX_LIMIT"}


def getp(pin):
    out = subprocess.check_output(["halcmd", "-s", "getp", pin], text=True).strip()
    return float(out.split()[-1])


def read_ini_val(lines, section, key):
    in_sec = False
    for l in lines:
        s = l.strip()
        if s.startswith("[") and s.endswith("]"):
            in_sec = (s == "[" + section + "]")
        elif in_sec and re.match(r"\s*%s\s*=" % key, l):
            return float(re.split(r"[=#]", l)[1].strip())
    return None


def apply_changes(lines, changes):
    """changes: dict (section, key) -> value. Remplace la ligne existante dans la
    bonne section ; insere en tete de section si absente."""
    out, cur, done = [], None, set()

    def flush(sec):
        for (s, k), v in changes.items():
            if s == sec and (s, k) not in done:
                out.append("%s = %.6f\n" % (k, v)); done.add((s, k))

    for l in lines:
        s = l.strip()
        if s.startswith("[") and s.endswith("]"):
            cur = s[1:-1]
            out.append(l)
            continue
        # remplace une ligne cle existante dans la section courante
        replaced = False
        for (sec, k), v in changes.items():
            if sec == cur and (sec, k) not in done and re.match(r"\s*%s\s*=" % k, l):
                out.append("%s = %.6f\n" % (k, v)); done.add((sec, k)); replaced = True
                break
        if not replaced:
            out.append(l)
    # sections presentes mais cle absente : on ne peut plus inserer proprement ici,
    # donc on signale ce qui manque (toutes nos sections ont deja les cles).
    missing = [(s, k) for (s, k) in changes if (s, k) not in done]
    return out, missing


def cmd_show(lines):
    print("Axe |   pos machine |     MIN_LIMIT |     MAX_LIMIT")
    print("----+---------------+---------------+--------------")
    for letter, (n, axsec, jsec) in AXES.items():
        try:
            pos = "%+.3f" % getp("joint.%d.pos-fb" % n)
        except Exception:
            pos = "   (halcmd?)"
        mn = read_ini_val(lines, jsec, "MIN_LIMIT")
        mx = read_ini_val(lines, jsec, "MAX_LIMIT")
        print("  %s | %13s | %13s | %13s" % (
            letter.upper(), pos,
            "%+.3f" % mn if mn is not None else "?",
            "%+.3f" % mx if mx is not None else "?"))


def main():
    ap = argparse.ArgumentParser(description="Regler les soft limits d'un axe par jog")
    ap.add_argument("axis", help="x | y | z   (ou 'show' pour afficher l'etat)")
    ap.add_argument("end", nargs="?", help="min | max")
    ap.add_argument("--margin", type=float, default=0.0,
                    help="marge de securite en mm, retiree vers l'interieur (defaut 0)")
    ap.add_argument("--value", type=float, default=None,
                    help="valeur imposee (mm) au lieu de lire la position courante")
    ap.add_argument("--write", action="store_true", help="ecrit sans confirmation")
    args = ap.parse_args()

    lines = open(INI).read().splitlines(keepends=True)

    if args.axis.lower() == "show":
        cmd_show(lines)
        return

    axis = args.axis.lower()
    if axis not in AXES:
        sys.exit("Axe inconnu : %r (attendu x, y, z ou 'show')" % args.axis)
    if args.end is None or args.end.lower() not in KEY:
        sys.exit("Precise le bout : 'min' ou 'max'  (ex: set_limits.py %s max)" % axis)
    end = args.end.lower()
    n, axsec, jsec = AXES[axis]
    key = KEY[end]

    if args.value is not None:
        pos = args.value
        src = "valeur imposee"
    else:
        try:
            pos = getp("joint.%d.pos-fb" % n)
        except Exception as e:
            sys.exit("Lecture position impossible (LinuxCNC lance ?) : %s" % e)
        src = "position machine courante"

    # marge : rentre vers l'interieur de la course
    limit = pos - args.margin if end == "max" else pos + args.margin

    old = read_ini_val(lines, jsec, key)
    print("Axe %s  |  %s = %.4f mm  (%s)" % (axis.upper(), src, pos, src))
    if args.margin:
        print("  marge %.3f mm -> limite ramenee a %.4f mm" % (args.margin, limit))
    print("  %s : %s -> %.4f mm  (sections [%s] et [%s])" % (
        key, "%.4f" % old if old is not None else "?", limit, axsec, jsec))

    if not args.write:
        ans = input("\nEcrire dans LC10E.ini ? [o/N] ").strip().lower()
        if ans not in ("o", "oui", "y", "yes"):
            print("Abandon — rien ecrit.")
            return

    changes = {(axsec, key): limit, (jsec, key): limit}
    shutil.copy(INI, INI + ".bak")
    out, missing = apply_changes(lines, changes)
    if missing:
        sys.exit("ERREUR: cles/sections introuvables : %s" %
                 ", ".join("%s/%s" % (s, k) for s, k in missing))
    with open(INI, "w") as f:
        f.writelines(out)

    print("\n[OK] ecrit (sauvegarde %s.bak)." % INI)
    print("--> REDEMARRE LinuxCNC puis Home All pour activer la nouvelle limite.")


if __name__ == "__main__":
    main()
