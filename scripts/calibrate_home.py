#!/usr/bin/env python3
"""
calibrate_home.py — Calibration de l'origine machine (encodeur absolu LC10E) — X / Y / Z

POURQUOI
  Les encodeurs LC10E sont absolus (multi-tours + batterie) : la position survit à
  l'extinction. On calibre UNE SEULE FOIS l'offset encodeur ↔ zéro machine de chaque
  axe ; LinuxCNC le réapplique à chaque démarrage (HOME_ABSOLUTE_ENCODER=1 dans l'INI).
  À refaire uniquement si la batterie d'un encodeur meurt ou s'il est reset.

PROCÉDURE (les 3 axes d'un coup)
  1. LinuxCNC lancé, machine ON (F2), Home All fait.
  2. Amener CHAQUE axe à calibrer sur son repère physique connu et répétable
     (typiquement sa fin de course, approchée toujours du même sens/vitesse).
  3. Lancer ce script depuis un terminal :
         cd /home/cnc/linuxcnc/configs/Ma_CNC/scripts
         python3 calibrate_home.py
     Pour chaque axe : saisir la coordonnée machine voulue à ce point, ou laisser
     VIDE pour ignorer cet axe (utile tant que Y/Z ne sont pas équipés).
  4. Le script écrit HOME_OFFSET dans ../LC10E.ini (sauvegarde .bak).
  5. Redémarrer LinuxCNC, Home All, revenir sur chaque repère : le DRO doit afficher
     la valeur demandée. Si l'erreur est ~doublée (mauvais signe), relancer --flip.

OPTIONS
  --axes xyz   axes considérés (ex. 'x', 'xz').        défaut: xyz
  --flip       convention de signe inverse.
  --write      écrit sans demander confirmation.

⚠️ Y et Z doivent AUSSI avoir HOME_ABSOLUTE_ENCODER=1 dans leur section [JOINT_n]
   pour se comporter en absolu. Sinon HOME_OFFSET ne sert qu'au home immédiat.

Aucune dépendance externe : lit la position via halcmd (LinuxCNC doit tourner).
"""
import subprocess, sys, re, shutil, os, argparse

HERE = os.path.dirname(os.path.abspath(__file__))
INI  = os.path.normpath(os.path.join(HERE, "..", "LC10E.ini"))

AXES = [  # (lettre, n° joint, section INI)
    ("X", 0, "JOINT_0"),
    ("Y", 1, "JOINT_1"),
    ("Z", 2, "JOINT_2"),
]


def getp(pin):
    out = subprocess.check_output(["halcmd", "-s", "getp", pin], text=True).strip()
    return float(out.split()[-1])


def parse_offset(lines, section):
    in_sec = False
    for l in lines:
        s = l.strip()
        if s.startswith("[") and s.endswith("]"):
            in_sec = (s == "[" + section + "]")
        elif in_sec and re.match(r"\s*HOME_OFFSET\s*=", l):
            return float(re.split(r"[=#]", l)[1].strip())
    return 0.0


def apply_offsets(lines, changes):
    """Remplace (ou insère) HOME_OFFSET dans chaque section de `changes` (dict sec->val)."""
    out, cur, written = [], None, set()

    def flush(sec):
        if sec in changes and sec not in written:
            out.append("HOME_OFFSET = %.6f\n" % changes[sec]); written.add(sec)

    for l in lines:
        s = l.strip()
        if s.startswith("[") and s.endswith("]"):
            flush(cur)                      # section précédente terminée → insère si attendu
            cur = s[1:-1]
        if cur in changes and re.match(r"\s*HOME_OFFSET\s*=", l):
            if cur not in written:
                out.append("HOME_OFFSET = %.6f\n" % changes[cur]); written.add(cur)
            continue                        # supprime l'ancienne ligne
        out.append(l)
    flush(cur)                              # dernière section du fichier
    missing = [s for s in changes if s not in written]
    return out, missing


def main():
    ap = argparse.ArgumentParser(description="Calibration origine machine X/Y/Z (encodeur absolu)")
    ap.add_argument("--axes", default="xyz", help="axes à calibrer, ex. 'x', 'xz' (défaut xyz)")
    ap.add_argument("--flip", action="store_true", help="inverse le sens de correction")
    ap.add_argument("--write", action="store_true", help="écrit sans confirmation")
    args = ap.parse_args()
    sel = args.axes.lower()

    lines = open(INI).read().splitlines(keepends=True)
    changes = {}

    print("Amène chaque axe sur son repère physique AVANT de saisir sa valeur.\n")
    for letter, n, section in AXES:
        if letter.lower() not in sel:
            continue
        try:
            R = getp("joint.%d.motor-pos-fb" % n)
            D = getp("joint.%d.pos-fb" % n)
        except Exception as e:
            print("[%s] lecture impossible (LinuxCNC lancé ?) : %s — ignoré.\n" % (letter, e))
            continue
        print("=== Axe %s (joint %d) ===" % (letter, n))
        print("  encodeur brut : %+.4f mm   |   machine actuelle : %+.4f mm" % (R, D))
        try:
            t = input("  Coordonnée machine voulue ici [vide = ignorer] : ").strip()
        except EOFError:
            t = ""
        if t == "":
            print("  -> ignoré\n"); continue
        M = float(t)
        delta = -(D - M) if args.flip else (D - M)
        old = parse_offset(lines, section)
        new = old + delta
        changes[section] = new
        print("  HOME_OFFSET %+.6f -> %+.6f  (cible %.4f mm)\n" % (old, new, M))

    if not changes:
        print("Aucun axe calibré, rien à écrire.")
        return

    print("Résumé des écritures :")
    for letter, n, section in AXES:
        if section in changes:
            print("  [%s] %s : HOME_OFFSET = %.6f" % (letter, section, changes[section]))

    if not args.write:
        ans = input("\nÉcrire dans LC10E.ini ? [o/N] ").strip().lower()
        if ans not in ("o", "oui", "y", "yes"):
            print("Abandon — rien écrit.")
            return

    shutil.copy(INI, INI + ".bak")
    out, missing = apply_offsets(lines, changes)
    if missing:
        sys.exit("ERREUR: sections introuvables dans l'INI : %s" % ", ".join(missing))
    with open(INI, "w") as f:
        f.writelines(out)

    print("\n[OK] écrit (sauvegarde %s.bak)." % INI)
    print("--> Redémarre LinuxCNC, Home All, vérifie le DRO au repère de chaque axe.")
    print("    Si une valeur est ~doublée (mauvais signe) : relance avec --flip.")


if __name__ == "__main__":
    main()
