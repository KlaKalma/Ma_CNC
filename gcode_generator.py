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
    base_speeds = [500, 800, 1000, 1200, 1500]
    
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
