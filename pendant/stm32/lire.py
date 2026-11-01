#!/usr/bin/env python3
"""Lit et ecrit les variables du firmware par OpenOCD, sans arreter la puce.

Usage : lire.py <elf> <nb_lectures> <pause_ms> <var1,var2,...> [var=valeur ...]

Les valeurs sont des mots de 32 bits : reinterpreter les variables signees.
"""
import os
import subprocess
import sys

pk = os.path.join(os.path.expanduser("~"), ".platformio", "packages")
openocd = os.path.join(pk, "tool-openocd", "bin", "openocd")
scripts = os.path.join(pk, "tool-openocd", "openocd", "scripts")
nm = os.path.join(pk, "toolchain-gccarmnoneeabi", "bin", "arm-none-eabi-nm")

elf, n, pause, noms = sys.argv[1], int(sys.argv[2]), int(sys.argv[3]), sys.argv[4].split(",")
ecritures = dict(a.split("=") for a in sys.argv[5:])

adresses = {}
for ligne in subprocess.run([nm, elf], capture_output=True, text=True).stdout.splitlines():
    champs = ligne.split()
    if len(champs) == 3:
        adresses[champs[2]] = champs[0]

cmd = ["init"]
cmd += [f"mww 0x{adresses[k]} {v}" for k, v in ecritures.items()]
for i in range(n):
    valeurs = " ".join(f"{k}=[read_memory 0x{adresses[k]} 32 1]" for k in noms)
    cmd.append(f'echo "S {valeurs}"')
    if i + 1 < n:
        cmd.append(f"sleep {pause}")
cmd.append("exit")

r = subprocess.run([openocd, "-s", scripts, "-f", "interface/stlink.cfg", "-c", "transport select swd",
                    "-f", "target/stm32g4x.cfg", "-c", "; ".join(cmd)], capture_output=True, text=True)
sortie = r.stdout + r.stderr
lignes = [l[2:] for l in sortie.splitlines() if l.startswith("S ")]
if not lignes:
    print(sortie[-1500:])
for l in lignes:
    print(" ".join(f"{k}={int(v, 0)}" for k, v in (x.split("=") for x in l.split())))
