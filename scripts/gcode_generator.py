#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Générateur de G-code à partir de fonctions mathématiques
Usage: python3 gcode_generator.py
"""

import math
import sys

def generate_gcode_from_function(func_str, x_start, x_end, num_points, feedrate, output_file):
    """
    Génère un fichier G-code à partir d'une fonction mathématique
    
    Args:
        func_str: Expression mathématique (ex: "50*sin(x)", "x**2", "exp(-x/10)*cos(x)")
        x_start: Valeur de départ de x
        x_end: Valeur de fin de x
        num_points: Nombre de points à calculer
        feedrate: Vitesse en mm/min (ex: 3000)
        output_file: Nom du fichier de sortie
    """
    
    # En-tête du fichier G-code
    gcode = ["%"]
    gcode.append(f"(G-code généré à partir de: y = {func_str})")
    gcode.append(f"(X range: {x_start} to {x_end}, Points: {num_points})")
    gcode.append(f"(Feedrate: F{feedrate} = {feedrate/60:.1f} mm/s)")
    gcode.append("")
    gcode.append("G21 (unités en mm)")
    gcode.append("G90 (mode absolu)")
    gcode.append("G64 P0.1 (path blending pour courbe lisse)")
    gcode.append("")
    gcode.append("(Retour à l'origine)")
    gcode.append("G0 X0")
    gcode.append("G4 P1.0")
    gcode.append("")
    gcode.append(f"F{feedrate}")
    gcode.append("")
    
    # Calcul des points
    x_step = (x_end - x_start) / (num_points - 1)
    
    print(f"\nGénération de {num_points} points...")
    print(f"Fonction: y = {func_str}")
    print(f"Range X: {x_start} à {x_end}")
    print(f"Feedrate: F{feedrate} ({feedrate/60:.1f} mm/s)\n")
    
    for i in range(num_points):
        x = x_start + i * x_step
        
        # Évaluation de la fonction
        # Variables disponibles: x, sin, cos, tan, exp, log, sqrt, abs, etc.
        try:
            y = eval(func_str, {
                "x": x,
                "sin": math.sin,
                "cos": math.cos,
                "tan": math.tan,
                "asin": math.asin,
                "acos": math.acos,
                "atan": math.atan,
                "sinh": math.sinh,
                "cosh": math.cosh,
                "tanh": math.tanh,
                "exp": math.exp,
                "log": math.log,
                "log10": math.log10,
                "sqrt": math.sqrt,
                "abs": abs,
                "pi": math.pi,
                "e": math.e,
                "__builtins__": {}  # Sécurité
            })
            
            gcode.append(f"G1 X{x:.3f}")
            
            if i % 10 == 0:  # Afficher progression
                print(f"Point {i+1}/{num_points}: X={x:.3f}")
                
        except Exception as e:
            print(f"ERREUR au point {i}: x={x}, erreur={e}")
            sys.exit(1)
    
    # Fin du programme
    gcode.append("")
    gcode.append("G4 P1.0")
    gcode.append("(Retour origine)")
    gcode.append("G0 X0")
    gcode.append("M2")
    gcode.append("%")
    
    # Écriture du fichier
    with open(output_file, 'w') as f:
        f.write('\n'.join(gcode))
    
    print(f"\n✓ Fichier généré: {output_file}")
    print(f"✓ {len(gcode)} lignes de G-code")


def generate_progressive_sine_test(output_file="/home/cnc/linuxcnc/nc_files/function-test.ngc"):
    """
    Génère un test automatique avec profil de vitesse sinusoïdal
    La vitesse varie en sinus pendant le mouvement linéaire sur X et Y
    """
    
    print("=" * 60)
    print("  GÉNÉRATION AUTOMATIQUE: PROFIL DE VITESSE SINUSOIDAL X-Y")
    print("=" * 60)
    print()
    
    # Paramètres
    x_start = 0
    x_end = 200  # Course de 200mm
    y_start = 0
    y_end = 200  # Course de 200mm (identique à X)
    num_segments = 100  # Plus de segments pour mouvement plus fluide
    
    # Vitesses de crête (mm/min) utilisées pendant la portion centrale à vitesse constante
    base_speeds = [2000, 3000, 4000, 5000]  # 33.3 à 100 mm/s
    
    # En-tête du fichier G-code
    gcode = ["%"]
    gcode.append("(G-code auto-genere: Profil vitesse sinusoidal X-Y)")
    gcode.append("(Vitesse varie en sinus pendant mouvement lineaire)")
    gcode.append("(Course: 200mm sur X et Y avec variation de vitesse)")
    gcode.append("")
    gcode.append("G21")
    gcode.append("G90")
    gcode.append("G64 P5.0")  # Path blending plus agressif pour mouvement fluide
    gcode.append("")
    gcode.append("(Retour a origine)")
    gcode.append("G0 X0 Y0")
    gcode.append("G4 P1.0")
    gcode.append("")
    
    x_step = (x_end - x_start) / num_segments
    y_step = (y_end - y_start) / num_segments
    ramp_fraction = 0.2  # 1/5 de la distance pour l'accélération/décélération

    def profile_speed(progress: float, top_speed_mmmin: float) -> float:
        """Calcule la vitesse (mm/min) en fonction de l'avancement (0-1)."""
        progress = max(0.0, min(1.0, progress))

        if progress <= ramp_fraction:
            phase = progress / ramp_fraction
            frac = math.sin((math.pi / 2) * phase)
        elif progress >= (1.0 - ramp_fraction):
            phase = (1.0 - progress) / ramp_fraction
            frac = math.sin((math.pi / 2) * phase)
        else:
            frac = 1.0

        # Éviter une vitesse nulle dans le G-code
        min_speed = max(top_speed_mmmin * 0.05, 60.0)
        return max(top_speed_mmmin * frac, min_speed)
    
    # Boucle sur chaque vitesse de base
    for test_idx, base_speed in enumerate(base_speeds):
        gcode.append(f"(Test {test_idx + 1} - Vitesse base: {base_speed} mm/min = {base_speed/60:.1f} mm/s)")
        gcode.append("")
        
        print(f"Génération test {test_idx + 1}/{len(base_speeds)}: vitesse base {base_speed} mm/min ({base_speed/60:.1f} mm/s)")
        
        # ALLER avec rampe sinusoïdale puis vitesse constante centrale
        gcode.append("(Aller - rampe sinus puis vitesse constante)")
        for i in range(num_segments + 1):
            x = x_start + i * x_step
            y = y_start + i * y_step

            progress = i / num_segments
            current_speed = profile_speed(progress, base_speed)
            feedrate = int(current_speed)

            if i == 0:
                gcode.append(f"F{feedrate} G1 X{x:.2f} Y{y:.2f}")
            else:
                gcode.append(f"F{feedrate}")
                gcode.append(f"G1 X{x:.2f} Y{y:.2f}")
        
        gcode.append("G4 P0.3")
        gcode.append("")
        
        # RETOUR avec profil identique (rampe sinus puis palier)
        gcode.append("(Retour - rampe sinus puis vitesse constante)")
        for i in range(num_segments + 1):
            x = x_end - i * x_step
            y = y_end - i * y_step

            progress = i / num_segments
            current_speed = profile_speed(progress, base_speed)
            feedrate = int(current_speed)

            if i == 0:
                gcode.append(f"F{feedrate} G1 X{x:.2f} Y{y:.2f}")
            else:
                gcode.append(f"F{feedrate}")
                gcode.append(f"G1 X{x:.2f} Y{y:.2f}")
        
        gcode.append("G4 P0.5")
        gcode.append("")
    
    # Fin du programme
    gcode.append("(Test termine)")
    gcode.append("G0 X0 Y0")
    gcode.append("M2")
    gcode.append("%")
    
    # Écriture du fichier
    with open(output_file, 'w') as f:
        f.write('\n'.join(gcode))
    
    print()
    print(f"✓ Fichier généré: {output_file}")
    print(f"✓ {len(gcode)} lignes de G-code")
    print(f"✓ {len(base_speeds)} tests avec profil de vitesse sinusoidal")
    print(f"✓ Demi-periode par trajet (0→π) - aller ET retour synchronises!")
    print(f"✓ X et Y se déplacent ensemble: 0→200mm")
    print("=" * 60)
    print("\n✓ TERMINÉ! La vitesse va osciller pendant le mouvement X-Y!")




def generate_3phase_sine_test(output_file="/home/cnc/linuxcnc/nc_files/3phase_sine_test.ngc",
                               with_spindle=True):
    """
    Sinusoïde triphasée X/Y/Z (+ broche optionnelle).
    Phase 1 (attrape-pas) : 1 Hz × 0.5 s  → machine entre en douceur
    Phase 2 (principale)  : 2 Hz × 5.0 s  → broche oscille 1000-3000 RPM

    with_spindle=True  : commande M3 + rampe + S oscillant.
        ATTENTION : avec spindle.0.at-speed câblé sur le retour VFD Y2 (Run),
        LinuxCNC ATTEND at-speed=true avant le 1er G1. Si la sortie VFD est
        coupée, Y2 reste à 0 → le programme se bloque sur le 1er G1 SANS erreur.
        N'utiliser with_spindle=True que si la sortie VFD est active.
    with_spindle=False : aucune commande broche → mouvement servo pur,
        fonctionne même VFD coupé. À utiliser pour valider les servos seuls.
    """

    # ── Paramètres axes ──────────────────────────────────────────────────
    A       = 2.0      # amplitude mm — réduit pour rester loin de MAX_ACCEL
    # A=2mm : pic accel = 2*(2pi*2)^2 = 315 mm/s² << MAX_ACCEL 500 mm/s²
    # A=3mm : pic accel = 473 mm/s²  → trop proche de 500, E-stop possible
    freq_1  = 1.0      # Hz — phase attrape-pas
    t_1     = 0.5      # s  — durée attrape-pas
    freq_2  = 2.0      # Hz — phase principale
    t_2     = 5.0      # s  — durée principale
    spc     = 50       # steps par cycle de freq_2 → dt=10ms, 100 seg/s (Pi tient la queue)

    # ── Paramètres broche ────────────────────────────────────────────────
    S_min   = 1000.0
    S_max   = 3000.0
    S_mid   = (S_max + S_min) / 2    # 2000 RPM
    S_amp   = (S_max - S_min) / 2    # ±1000 RPM
    phi_s   = 0.0                     # même phase que X

    # ── Déphasages triphasés ─────────────────────────────────────────────
    phi_x   = 0.0
    phi_y   = 2 * math.pi / 3
    phi_z   = 4 * math.pi / 3

    # ── Timestep commun (basé sur freq_2) ────────────────────────────────
    dt      = 1.0 / (freq_2 * spc)           # 5 ms
    steps_1 = int(round(t_1 / dt))           # 100 steps (0.5s à dt=5ms)
    steps_2 = int(round(t_2 * freq_2 * spc)) # 1000 steps

    # Vitesse combinée 3-phase (constante, formule analytique)
    feed_1  = int(A * 2 * math.pi * freq_1 * math.sqrt(1.5) * 60)  # mm/min
    feed_2  = int(A * 2 * math.pi * freq_2 * math.sqrt(1.5) * 60)  # mm/min

    # Position θ=0 de la sinusoïde
    x0 = A * math.sin(phi_x)    # = 0.000
    y0 = A * math.sin(phi_y)    # = +2.598
    z0 = A * math.sin(phi_z)    # = -2.598

    # ── Header ───────────────────────────────────────────────────────────
    spin_txt = "+ broche" if with_spindle else "- SERVOS SEULS sans broche"
    gcode = []
    gcode.append("%")
    gcode.append(f"(3phase_sine_test.ngc — Sinusoide triphasee X/Y/Z {spin_txt})")
    gcode.append(f"(Phase 1 — attrape-pas : {freq_1} Hz x {t_1}s)")
    gcode.append(f"(Phase 2 — principale  : {freq_2} Hz x {t_2}s)")
    if with_spindle:
        gcode.append(f"(Broche : rampe 0->{S_min:.0f} RPM puis sinus {S_min:.0f}-{S_max:.0f} RPM)")
        gcode.append("(REQUIERT la sortie VFD active — sinon blocage sur 1er G1 via at-speed)")
    else:
        gcode.append("(Aucune commande broche : marche meme VFD coupe)")
    gcode.append(f"(Amplitude axes : {A} mm  |  Prerequis : F2 + Home All)")
    gcode.append("")
    gcode.append("G21 G90")
    gcode.append("G64 P0.5")
    gcode.append("G92 X0 Y0 Z0")
    gcode.append("")

    # ── Rampe broche : 0 → 1000 RPM en 5 s ─────────────────────────────
    if with_spindle:
        ramp_n     = 100
        ramp_dwell = 0.05
        gcode.append(f"(Rampe broche : 0 -> {S_min:.0f} RPM en {ramp_n * ramp_dwell:.0f} s)")
        gcode.append(f"M3 S{S_min / ramp_n:.0f}")
        for i in range(1, ramp_n + 1):
            gcode.append(f"S{S_min * i / ramp_n:.0f}")
            gcode.append(f"G4 P{ramp_dwell}")
        gcode.append("G4 P0.3")
        gcode.append("")

    # ── Aller à la position θ=0 ──────────────────────────────────────────
    gcode.append("(Position initiale de la sinusoide)")
    gcode.append(f"G0 X{x0:.4f} Y{y0:.4f} Z{z0:.4f}")
    gcode.append("G4 P0.2")
    gcode.append("")

    # ── Phase 1 : attrape-pas à freq_1 ──────────────────────────────────
    p1_txt = f" — broche {S_min:.0f} RPM fixe" if with_spindle else ""
    gcode.append(f"(Phase 1 : {freq_1} Hz x {t_1}s{p1_txt})")
    theta = 0.0
    for _ in range(steps_1):
        theta += 2 * math.pi * freq_1 * dt
        x = A * math.sin(theta + phi_x)
        y = A * math.sin(theta + phi_y)
        z = A * math.sin(theta + phi_z)
        gcode.append(f"G1 X{x:.4f} Y{y:.4f} Z{z:.4f} F{feed_1}")
    gcode.append("G4 P0.1  (stabilisation avant phase 2)")
    gcode.append("")

    # ── Phase 2 : principale à freq_2, broche oscillante ────────────────
    s_txt = f" — broche {S_min:.0f}-{S_max:.0f} RPM" if with_spindle else ""
    gcode.append(f"(Phase 2 : {freq_2} Hz x {t_2}s{s_txt})")
    last_s = S_min
    for _ in range(steps_2):
        theta += 2 * math.pi * freq_2 * dt
        x = A * math.sin(theta + phi_x)
        y = A * math.sin(theta + phi_y)
        z = A * math.sin(theta + phi_z)

        if with_spindle:
            s = S_mid + S_amp * math.sin(theta + phi_s)
            # Mise à jour S seulement si variation > 20 RPM (évite 200 Hz d'updates VFD)
            if abs(s - last_s) >= 20:
                gcode.append(f"G1 X{x:.4f} Y{y:.4f} Z{z:.4f} F{feed_2} S{s:.0f}")
                last_s = s
            else:
                gcode.append(f"G1 X{x:.4f} Y{y:.4f} Z{z:.4f} F{feed_2}")
        else:
            gcode.append(f"G1 X{x:.4f} Y{y:.4f} Z{z:.4f} F{feed_2}")
    gcode.append("")

    # ── Fin ──────────────────────────────────────────────────────────────
    if with_spindle:
        gcode.append("M5")
    gcode.append("G0 X0 Y0 Z0")
    gcode.append("G92.1")
    gcode.append("M2")
    gcode.append("%")

    with open(output_file, 'w') as f:
        f.write('\n'.join(gcode))

    print(f"Fichier  : {output_file}")
    print(f"Phase 1  : {steps_1} pts  |  {feed_1} mm/min  |  pic axe {A*2*math.pi*freq_1:.1f} mm/s")
    print(f"Phase 2  : {steps_2} pts  |  {feed_2} mm/min  |  pic axe {A*2*math.pi*freq_2:.1f} mm/s")
    print(f"Total    : {t_1 + t_2:.1f}s de mouvement  +  5s rampe broche")


def generate_spindle_only_test(output_file="/home/cnc/linuxcnc/nc_files/3phase_broche_seule.ngc"):
    """
    Broche SEULE : rampe 0->1000 RPM puis sinus 1000-3000 RPM, AUCUN mouvement servo.
    Sert a tester l'at-speed broche isolement (la broche atteint-elle la vitesse,
    Y2 'Run' remonte-t-il ?). Tout en S + G4 dwells, aucun G1.
    """
    S_min, S_max = 1000.0, 3000.0
    S_mid, S_amp = (S_max + S_min) / 2, (S_max - S_min) / 2

    g = ["%"]
    g.append("(3phase_broche_seule.ngc — BROCHE SEULE, aucun mouvement servo)")
    g.append("(Rampe 0->1000 RPM puis sinus 1000-3000 RPM)")
    g.append("(But : verifier que la broche atteint sa vitesse / retour Y2 Run)")
    g.append("(Prerequis : F2 + sortie VFD active)")
    g.append("G21 G90")
    g.append("")

    # Rampe 0 -> 1000 RPM en 5 s
    ramp_n, ramp_dwell = 100, 0.05
    g.append(f"(Rampe : 0 -> {S_min:.0f} RPM en {ramp_n*ramp_dwell:.0f} s)")
    g.append(f"M3 S{S_min/ramp_n:.0f}")
    for i in range(1, ramp_n + 1):
        g.append(f"S{S_min*i/ramp_n:.0f}")
        g.append(f"G4 P{ramp_dwell}")
    g.append("G4 P0.5")
    g.append("")

    # Oscillation 1000-3000 RPM, periode 2.5 s, ~4 cycles (10 s)
    g.append(f"(Oscillation {S_min:.0f}-{S_max:.0f} RPM x4 cycles)")
    period, dwell = 2.5, 0.05
    n = int(round(4 * period / dwell))
    last_s = S_min
    for k in range(1, n + 1):
        t = k * dwell
        s = S_mid + S_amp * math.sin(2 * math.pi * t / period)
        if abs(s - last_s) >= 20:
            g.append(f"S{s:.0f}")
            last_s = s
        g.append(f"G4 P{dwell}")
    g.append("")

    g.append("M5")
    g.append("M2")
    g.append("%")

    with open(output_file, 'w') as f:
        f.write('\n'.join(g))
    print(f"Fichier : {output_file}  ({len(g)} lignes, aucun G1)")


def generate_sequential_3axis_test(output_file="/home/cnc/linuxcnc/nc_files/servo_seq_xyz.ngc"):
    """
    Test SEQUENTIEL : chaque axe testé seul, l'un apres l'autre (X -> Y -> Z),
    puis un petit mouvement combine XYZ pour tester la coordination.
    But : isoler quel axe s'arrete (X marche deja en solo).
    100% G1 pre-genere, aucun O-word, aucune commande broche.
    """
    A_tri  = 5.0    # amplitude triangle mm (bien visible, sans danger)
    n_tri  = 3      # nb d'allers-retours triangle
    F_tri  = 600    # mm/min = 10 mm/s
    A_sin  = 3.0    # amplitude sinus mm
    f_sin  = 1.0    # Hz
    cyc    = 2      # nb cycles sinus
    spc    = 20     # pts/cycle -> dt=50ms, 20 seg/s (tres safe)
    F_sin  = int(A_sin * 2 * math.pi * f_sin * 60)   # pic ~1131 mm/min

    axes = ["X", "Y", "Z"]

    g = ["%"]
    g.append("(servo_seq_xyz.ngc — Test SEQUENTIEL X puis Y puis Z, axe par axe)")
    g.append("(But : isoler quel axe s'arrete. Aucune commande broche.)")
    g.append("(Prerequis : F2 Machine ON + Home All)")
    g.append("G21 G90 G64 P0.5")
    g.append("G92 X0 Y0 Z0")
    g.append("")

    for ax in axes:
        g.append(f"(========== AXE {ax} ==========)")
        g.append(f"(--- {ax} : triangle +/-{A_tri:.0f}mm x{n_tri} a F{F_tri} ---)")
        for _ in range(n_tri):
            g.append(f"G1 {ax}{A_tri:.0f} F{F_tri}")
            g.append(f"G1 {ax}-{A_tri:.0f} F{F_tri}")
        g.append(f"G1 {ax}0 F{F_tri}")
        g.append("G4 P0.5")
        g.append(f"(--- {ax} : sinus {f_sin:.0f}Hz x{cyc} a F{F_sin} ---)")
        theta = 0.0
        dth   = 2 * math.pi / spc
        for _ in range(cyc * spc):
            theta += dth
            val = A_sin * math.sin(theta)
            g.append(f"G1 {ax}{val:.4f} F{F_sin}")
        g.append(f"G1 {ax}0 F{F_tri}")
        g.append("G4 P1.0")
        g.append("")

    # --- Coordination : petit triangle sur les 3 axes ensemble ---
    g.append("(========== COMBINE XYZ ==========)")
    g.append("(--- triangle simultane sur X Y Z ---)")
    for _ in range(n_tri):
        g.append(f"G1 X{A_tri:.0f} Y{A_tri:.0f} Z{A_tri:.0f} F{F_tri}")
        g.append(f"G1 X-{A_tri:.0f} Y-{A_tri:.0f} Z-{A_tri:.0f} F{F_tri}")
    g.append("G1 X0 Y0 Z0 F{}".format(F_tri))
    g.append("G4 P0.5")
    g.append("")

    g.append("M2")
    g.append("%")

    with open(output_file, 'w') as f:
        f.write('\n'.join(g))

    print(f"Fichier : {output_file}  ({len(g)} lignes)")
    print(f"Sequence: X solo -> Y solo -> Z solo -> XYZ combine")
    print(f"Triangle: +/-{A_tri:.0f}mm x{n_tri} @ F{F_tri}  |  Sinus: {A_sin:.0f}mm {f_sin:.0f}Hz x{cyc} @ F{F_sin}")


def generate_vfd_ramp_static(output_file="/home/cnc/linuxcnc/nc_files/vfd_ramp_test.ngc"):
    """Rampe VFD 1 Hz -> 200 Hz en 20 s — 100% G-code statique, aucun O-word."""
    hz_start  = 1
    hz_end    = 200
    ramp_sec  = 20.0
    hz_to_rpm = 15.0   # 8 pôles : 1 Hz = 15 RPM
    hold_sec  = 3.0

    steps = hz_end - hz_start + 1
    dwell = ramp_sec / steps  # 0.1 s

    lines = []
    lines.append("%")
    lines.append("(vfd_ramp_test.ngc — Rampe VFD statique, sans O-words)")
    lines.append(f"(Rampe : {hz_start} Hz -> {hz_end} Hz en {ramp_sec:.0f} s)")
    lines.append(f"(Moteur 8 poles : 1 Hz = {hz_to_rpm:.0f} RPM  |  scale EL4002 = 18000)")
    lines.append(f"(Plage : {hz_start*hz_to_rpm:.0f} RPM -> {hz_end*hz_to_rpm:.0f} RPM)")
    lines.append(f"(Prerequis : F2 Machine ON, broche forward enable sur VFD)")
    lines.append("")
    lines.append(f"M3 S{int(hz_start * hz_to_rpm)}   ({hz_start} Hz — Forward ON)")
    lines.append("G4 P0.5   (attente arme VFD)")
    lines.append("")

    for hz in range(hz_start + 1, hz_end + 1):
        lines.append(f"S{int(hz * hz_to_rpm)}   ({hz} Hz)")
        lines.append(f"G4 P{dwell:.3f}")

    lines.append("")
    lines.append(f"(Maintien {hz_end} Hz = {int(hz_end*hz_to_rpm)} RPM pendant {hold_sec:.0f} s)")
    lines.append(f"G4 P{hold_sec:.1f}")
    lines.append("M5")
    lines.append("M2")
    lines.append("%")

    with open(output_file, 'w') as f:
        f.write('\n'.join(lines))

    print(f"Fichier : {output_file}  ({len(lines)} lignes)")
    print(f"Rampe   : {steps} steps x {dwell*1000:.0f} ms = {ramp_sec:.0f} s")


def interactive_mode():
    """Mode interactif pour générer du G-code"""
    
    print("=" * 60)
    print("  GÉNÉRATEUR DE G-CODE À PARTIR DE FONCTIONS MATHÉMATIQUES")
    print("=" * 60)
    print()
    print("Exemples de fonctions:")
    print("  - Sinus simple:        50*sin(x)")
    print("  - Sinus amorti:        50*exp(-x/100)*sin(x)")
    print("  - Parabole:            x**2 / 10")
    print("  - Oscillations:        30*sin(x) + 20*cos(2*x)")
    print("  - Exponentielle:       exp(x/50)")
    print("  - Spirale logarithme:  x + 10*log(x+1)")
    print()
    print("Variables disponibles:")
    print("  x, sin, cos, tan, exp, log, sqrt, abs, pi, e, etc.")
    print()
    
    # Entrée de la fonction
    func = input("Entrez la fonction mathématique (ex: 50*sin(x)) [ENTRÉE pour auto]: ").strip()
    
    if not func:
        # Mode automatique
        print("\n→ Mode AUTOMATIQUE activé!")
        generate_progressive_sine_test()
        return
    
    # Mode manuel
    # Range X
    x_start = float(input("X début (ex: 0): ").strip() or "0")
    x_end = float(input("X fin (ex: 100): ").strip() or "100")
    
    # Nombre de points
    num_points = int(input("Nombre de points (ex: 100): ").strip() or "100")
    
    # Vitesse
    feedrate = int(input("Vitesse en mm/min (ex: 3000): ").strip() or "3000")
    
    # Nom du fichier
    default_name = "function-test.ngc"
    output = input(f"Nom du fichier de sortie ({default_name}): ").strip() or default_name
    
    # Ajouter le chemin vers nc_files si pas de chemin spécifié
    if "/" not in output:
        output = f"/home/cnc/linuxcnc/nc_files/{output}"
    
    print("\n" + "=" * 60)
    
    # Génération
    generate_gcode_from_function(func, x_start, x_end, num_points, feedrate, output)
    
    print("=" * 60)
    print("\n✓ TERMINÉ! Vous pouvez maintenant charger ce fichier dans LinuxCNC")


if __name__ == "__main__":
    if len(sys.argv) > 1:
        # Mode ligne de commande
        if len(sys.argv) < 6:
            print("Usage: python3 gcode_generator.py 'function' x_start x_end num_points feedrate_mm_min [output_file]")
            print("Exemple: python3 gcode_generator.py '50*sin(x)' 0 100 200 3000 test.ngc")
            sys.exit(1)
        
        func = sys.argv[1]
        x_start = float(sys.argv[2])
        x_end = float(sys.argv[3])
        num_points = int(sys.argv[4])
        feedrate = int(sys.argv[5])
        output = sys.argv[6] if len(sys.argv) > 6 else "/home/cnc/linuxcnc/nc_files/function-test.ngc"
        
        generate_gcode_from_function(func, x_start, x_end, num_points, feedrate, output)
    else:
        # Mode interactif
        interactive_mode()
