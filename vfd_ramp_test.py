#!/usr/bin/env python3
"""
vfd_ramp_test.py — Test de montée en fréquence VFD
Rampe : 1 Hz → 200 Hz en 20 secondes, puis arrêt.

Prérequis :
  - LinuxCNC lancé avec la config Ma_CNC
  - Machine allumée (F2) — relâche aussi les freins EL2024

Correspondance Hz ↔ RPM (basée sur setp aout-0-scale 6000.0) :
  Moteur 2 pôles : 200 Hz × 60 / 2 = 6000 RPM → 10 V
  Si moteur 4 pôles : 200 Hz × 60 / 4 = 3000 RPM — ajuster HZ_TO_RPM ci-dessous
"""

import linuxcnc
import time
import sys

# ── Paramètres ─────────────────────────────────────────────────────────────
START_HZ   = 1       # Hz de départ
END_HZ     = 200     # Hz de fin (max moteur = 1200 Hz = 18000 RPM)
RAMP_SEC   = 20.0    # durée totale en secondes
HZ_TO_RPM  = 15.0    # 1 Hz = 15 RPM  (moteur 8 pôles, 4 paires — 400 Hz = 6000 RPM)
# Scale HAL : setp lcec.0.6.aout-0-scale 18000.0  → S18000 = 10V = 1200 Hz
# ───────────────────────────────────────────────────────────────────────────

STEPS    = END_HZ - START_HZ + 1        # 200 steps
DWELL    = RAMP_SEC / STEPS             # 0.1 s par step


def check_machine_state(stat):
    stat.poll()
    if stat.task_state != linuxcnc.STATE_ON:
        print("ERREUR : Machine éteinte. Allume la machine (F2) avant de lancer le test.")
        sys.exit(1)


def main():
    c = linuxcnc.command()
    s = linuxcnc.stat()

    check_machine_state(s)

    print(f"Rampe VFD : {START_HZ} Hz → {END_HZ} Hz en {RAMP_SEC:.0f}s")
    print(f"Pas : 1 Hz toutes les {DWELL*1000:.0f} ms")
    print(f"Moteur : {START_HZ * HZ_TO_RPM:.0f} RPM → {END_HZ * HZ_TO_RPM:.0f} RPM")
    print("─" * 40)
    print("Ctrl+C pour arrêt d'urgence\n")
    time.sleep(1)

    # Passer en mode MDI si ce n'est pas déjà fait
    if s.task_mode not in (linuxcnc.MODE_MDI, linuxcnc.MODE_MANUAL):
        c.mode(linuxcnc.MODE_MDI)
        c.wait_complete()

    try:
        for hz in range(START_HZ, END_HZ + 1):
            rpm = hz * HZ_TO_RPM
            c.spindle(linuxcnc.SPINDLE_FORWARD, rpm)

            # Barre de progression
            pct    = (hz - START_HZ) / (END_HZ - START_HZ) * 100
            bar_w  = 30
            filled = int(bar_w * pct / 100)
            bar    = "█" * filled + "░" * (bar_w - filled)
            print(f"\r[{bar}] {hz:3d} Hz  {rpm:6.0f} RPM  {pct:5.1f}%", end="", flush=True)

            time.sleep(DWELL)

        print("\n\nVitesse maximale atteinte — maintien 3 s...")
        time.sleep(3)

    except KeyboardInterrupt:
        print("\n\nArrêt d'urgence (Ctrl+C) !")

    finally:
        print("Arrêt broche...")
        c.spindle(linuxcnc.SPINDLE_OFF)
        print("Arrêt OK.")


if __name__ == "__main__":
    main()
