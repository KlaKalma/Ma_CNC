"""Lance un essai moteur et analyse le journal. Usage : essai.py <elf> <mv>"""
import os, subprocess, sys, time

pk = os.path.join(os.environ["USERPROFILE"], ".platformio", "packages")
ocd = os.path.join(pk, "tool-openocd", "bin", "openocd.exe")
scripts = os.path.join(pk, "tool-openocd", "openocd", "scripts")
nm = os.path.join(pk, "toolchain-gccarmnoneeabi", "bin", "arm-none-eabi-nm.exe")
elf, mv = sys.argv[1], int(sys.argv[2])

ad = {}
for l in subprocess.run([nm, elf], capture_output=True, text=True).stdout.splitlines():
    p = l.split()
    if len(p) == 3:
        ad[p[2]] = p[0]

def ocd_cmd(c):
    r = subprocess.run([ocd, "-s", scripts, "-f", "interface/stlink.cfg", "-c", "transport select swd",
                        "-f", "target/stm32g4x.cfg", "-c", "init; " + c + "; exit"], capture_output=True, text=True)
    return r.stdout + r.stderr

def lire(noms):
    c = 'echo "S ' + " ".join(f"{k}=[read_memory 0x{ad[k]} 32 1]" for k in noms) + '"'
    s = ocd_cmd(c)
    for l in s.splitlines():
        if l.startswith("S "):
            return {k: int(v, 0) for k, v in (x.split("=") for x in l[2:].split())}
    raise SystemExit("lecture impossible:\n" + s[-800:])

ETATS = ["etat_essai", "nb_essais", "cause_reset", "nb_lectures", "nb_crc_faux", "etat", "diag_haut",
         "tension_appliquee_mv", "nb_journal", "angle"]
av = lire(ETATS)
print("avant", av)
ocd_cmd(f"mww 0x{ad['nb_crc_faux']} 0; mww 0x{ad['nb_lectures']} 0; mww 0x{ad['essai_mv']} {mv}")
time.sleep(1.0)
pendant = lire(ETATS)
print("pendant", pendant)
time.sleep(9.0)
ap = lire(ETATS)
print("apres", ap)
if ap["nb_essais"] != av["nb_essais"] + 1:
    print("!!! nb_essais inattendu : redemarrage probable de la carte. cause_reset=0x%08x" % ap["cause_reset"])

n = ap["nb_journal"]
s = ocd_cmd(f'echo "J [read_memory 0x{ad["journal"]} 32 {n}]"') if n else ""
j = []
for l in s.splitlines():
    if l.startswith("J "):
        j = [int(x, 0) for x in l[2:].split()]
print("journal brut", j)
if j:
    u = [j[0]]
    for v in j[1:]:
        d = (v - u[-1] % 16384 + 8192) % 16384 - 8192
        u.append(u[-1] + d)
    print("deplie", u)
    pas = [u[i + 1] - u[i] for i in range(len(u) - 1)]
    print("pas", pas)
    mi = (len(u) - 1) // 2
    aller, retour = u[mi] - u[0], u[mi] - u[-1]
    print(f"aller {aller} pas = {aller * 360 / 16384:.1f} deg ; retour {retour} ; ecart a l'arrivee {u[-1] - u[0]}")
    print("attendu pour 2 tours electriques a 7 paires : +-4681 pas (102,9 deg)")
