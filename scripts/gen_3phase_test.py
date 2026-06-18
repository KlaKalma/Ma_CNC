#!/usr/bin/env python3
"""
gen_3phase_test.py — Génère 3phase_test_v2.ngc
Ratio broche : 8 pôles → 4 paires → 1 Hz = 15 RPM
               1200 Hz = 18000 RPM = 10V (scale HAL = 18000)

Phase 1 : broche à 1 Hz VFD (15 RPM) pendant 0.5s — servos statiques
Phase 2 : rampe linéaire 15 → 1000 RPM en 5s          — servos statiques
Phase 3 : broche sin 1 Hz entre 1000-3000 RPM
          + servos triphasés (120°) à 2 Hz, amplitude 3 mm
"""
import math

# ── Paramètres broche ────────────────────────────────────────────────────
HZ_TO_RPM   = 15.0    # 8 pôles, 4 paires : RPM = Hz × 15
MAX_RPM     = 18000.0 # 1200 Hz × 15 = 10 V AVI

# Phase 1
P1_HZ       = 1.0
P1_RPM      = P1_HZ * HZ_TO_RPM   # = 15 RPM
P1_DUR      = 0.5                  # secondes

# Phase 2 : rampe
P2_RPM_END  = 1000.0
P2_STEPS    = 100
P2_DT       = 0.05                 # 50 ms × 100 pas = 5 s

# Phase 3 : broche sinusoïdale
P3_SP_CTR   = 2000.0               # centre [RPM]
P3_SP_AMP   = 1000.0               # amplitude → 1000-3000 RPM
P3_SP_FREQ  = 1.0                  # 1 Hz

# Phase 3 : servos triphasés (déphasage 120° entre X, Y, Z)
P3_SV_AMP   = 3.0                  # amplitude [mm]
P3_SV_FREQ  = 2.0                  # 2 Hz
P3_PH_X     = 0.0                  # déphasage X [degrés]
P3_PH_Y     = 120.0                # déphasage Y
P3_PH_Z     = 240.0                # déphasage Z
P3_DT       = 0.025                # 25 ms par point
P3_DUR      = 5.0                  # durée Phase 3

OUTPUT = "/home/cnc/linuxcnc/nc_files/3phase_test_v2.ngc"

# ── Feed rate Phase 3 ────────────────────────────────────────────────────
# Vitesse vecteur triphasé (magnitude constante) :
# |v| = A × ω × √(N/2)  avec N=3 axes
omega_sv    = 2 * math.pi * P3_SV_FREQ
v_xyz       = P3_SV_AMP * omega_sv * math.sqrt(3 / 2)
feedrate_p3 = int(v_xyz * 60) + 10   # mm/min, marge +10

# Feed rate Phase 1 (1 Hz triphasé, pour référence)
v_xyz_p1    = P3_SV_AMP * 2 * math.pi * 1.0 * math.sqrt(3 / 2)
feedrate_p1 = int(v_xyz_p1 * 60) + 10

# ── Génération ───────────────────────────────────────────────────────────
n_p3 = int(P3_DUR / P3_DT)

lines = []
lines += [
    "%",
    f"(3phase_test_v2.ngc — Test broche 8 pôles + servos triphasés 2 Hz)",
    f"(Ratio vérif : 1 Hz VFD = {HZ_TO_RPM:.0f} RPM  |  1200 Hz = {MAX_RPM:.0f} RPM = 10V)",
    f"(Scale HAL   : setp lcec.0.6.aout-0-scale 18000.0  [S18000 = 10V])",
    f"(Phase 1     : broche {P1_HZ:.0f} Hz VFD = {P1_RPM:.0f} RPM, {P1_DUR:.1f}s, servos {0},{0},{0})",
    f"(Phase 2     : rampe {P1_RPM:.0f} -> {P2_RPM_END:.0f} RPM en {P2_STEPS*P2_DT:.0f}s, servos statiques)",
    f"(Phase 3     : broche sin {P3_SP_FREQ:.0f}Hz [{P3_SP_CTR-P3_SP_AMP:.0f}-{P3_SP_CTR+P3_SP_AMP:.0f} RPM])",
    f"(             + servos triphasés {P3_SV_FREQ:.0f}Hz {P3_SV_AMP:.0f}mm  F{feedrate_p3}  [{n_p3} pts])",
    "",
    "G21 G90",
    "G64 P0.5",
    "G92 X0 Y0 Z0",
    "",
]

# ── Phase 1 ──────────────────────────────────────────────────────────────
lines += [
    f"(Phase 1 : M3 S{P1_RPM:.0f}  [{P1_HZ:.0f} Hz VFD]  G4 P{P1_DUR:.1f})",
    f"M3 S{P1_RPM:.0f}",
    f"G4 P{P1_DUR:.1f}",
    "",
]

# ── Phase 2 : rampe ──────────────────────────────────────────────────────
lines.append(f"(Phase 2 : rampe {P1_RPM:.0f} -> {P2_RPM_END:.0f} RPM  {P2_STEPS} pas x {int(P2_DT*1000)} ms)")
for i in range(P2_STEPS):
    rpm = P1_RPM + (i + 1) * (P2_RPM_END - P1_RPM) / P2_STEPS
    lines.append(f"S{rpm:.0f}")
    lines.append(f"G4 P{P2_DT:.2f}")
lines += [f"S{P2_RPM_END:.0f}", f"G4 P0.30", ""]

# ── Phase 3 : positionnement initial ────────────────────────────────────
x0 = P3_SV_AMP * math.sin(math.radians(P3_PH_X))
y0 = P3_SV_AMP * math.sin(math.radians(P3_PH_Y))
z0 = P3_SV_AMP * math.sin(math.radians(P3_PH_Z))
lines += [
    f"(Position initiale Phase 3 : X={x0:.4f} Y={y0:.4f} Z={z0:.4f})",
    f"G0 X{x0:.4f} Y{y0:.4f} Z{z0:.4f}",
    f"G4 P0.2",
    "",
    f"(Phase 3 : {n_p3} points — broche sin {P3_SP_FREQ:.0f}Hz + servos triphasés {P3_SV_FREQ:.0f}Hz)",
]

for i in range(n_p3):
    t = i * P3_DT

    # Broche
    ang_sp = 360.0 * P3_SP_FREQ * t
    rpm    = P3_SP_CTR + P3_SP_AMP * math.sin(math.radians(ang_sp))

    # Servos
    ang_sv = 360.0 * P3_SV_FREQ * t
    x = P3_SV_AMP * math.sin(math.radians(ang_sv + P3_PH_X))
    y = P3_SV_AMP * math.sin(math.radians(ang_sv + P3_PH_Y))
    z = P3_SV_AMP * math.sin(math.radians(ang_sv + P3_PH_Z))

    lines.append(f"G1 X{x:.4f} Y{y:.4f} Z{z:.4f} F{feedrate_p3} S{rpm:.0f}")

lines += [
    "",
    "(Fin de test)",
    "M5",
    "G0 X0 Y0 Z0",
    "M2",
    "%",
]

with open(OUTPUT, "w") as f:
    f.write("\n".join(lines) + "\n")

print(f"Fichier : {OUTPUT}")
print(f"Points Phase 3 : {n_p3}  ({n_p3 * P3_DT:.1f}s)")
print(f"Feed rate P3   : F{feedrate_p3} mm/min  ({v_xyz:.2f} mm/s)")
print(f"Phase 1        : S{P1_RPM:.0f} ({P1_HZ:.0f} Hz VFD)  G4 P{P1_DUR:.1f}")
print(f"Ratio broche   : 1 Hz = {HZ_TO_RPM:.0f} RPM  |  10V = {MAX_RPM:.0f} RPM = 1200 Hz ✓")
