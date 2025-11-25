#!/usr/bin/env python3
"""
LC10E CSP Tuning Advisor - Haute Précision
Cible: 20µm (0.02mm) d'erreur max
"""

import subprocess
import time
import sys
from collections import deque

# Configuration - CIBLE 20µm
TARGET_ERROR_UM = 20  # Cible en µm (0.02mm)
HISTORY_SIZE = 500
x_error_history = deque(maxlen=HISTORY_SIZE)
y_error_history = deque(maxlen=HISTORY_SIZE)

def run_hal(cmd):
    """Execute halcmd"""
    try:
        result = subprocess.run(f"halcmd {cmd}", shell=True, 
                                capture_output=True, text=True, timeout=1)
        return result.stdout.strip()
    except:
        return None

def get_pin(pin):
    """Get HAL pin value"""
    result = run_hal(f"getp {pin}")
    try:
        return float(result)
    except:
        return None

def get_bit(pin):
    """Get HAL bit pin"""
    result = run_hal(f"getp {pin}")
    return result == "TRUE"

def analyze_errors(errors):
    """Analyze error pattern"""
    if not errors or len(errors) < 10:
        return None
    
    errors_list = list(errors)
    avg = sum(errors_list) / len(errors_list)
    max_err = max(abs(e) for e in errors_list)
    min_err = min(errors_list)
    max_pos = max(errors_list)
    
    # Detect oscillation (sign changes)
    sign_changes = sum(1 for i in range(1, len(errors_list)) 
                       if errors_list[i] * errors_list[i-1] < 0)
    oscillation_rate = sign_changes / len(errors_list) * 100
    
    # Detect drift (constant offset)
    drift = avg
    
    return {
        'avg': avg,
        'max': max_err,
        'max_pos': max_pos,
        'min': min_err,
        'oscillation_rate': oscillation_rate,
        'sign_changes': sign_changes
    }

def get_recommendations(x_analysis, y_analysis, is_moving, velocity):
    """Recommandations pour cible 20µm (0.02mm)"""
    recs = []
    
    if not x_analysis and not y_analysis:
        return ["En attente de données..."]
    
    for axis, analysis in [('X', x_analysis), ('Y', y_analysis)]:
        if not analysis:
            continue
            
        max_err_um = analysis['max'] * 1000  # Convertir mm -> µm
        osc_rate = analysis['oscillation_rate']
        avg_um = analysis['avg'] * 1000
        
        # === ERREUR PENDANT MOUVEMENT ===
        if is_moving:
            if max_err_um > 500:  # > 500µm = critique
                recs.append(f"🔴 {axis}: CRITIQUE {max_err_um:.0f}µm (cible: {TARGET_ERROR_UM}µm)")
                recs.append(f"    → P08-19 (Vel FF) = 90-100% OBLIGATOIRE")
                recs.append(f"    → Vérifier P08-15 (Inertia ratio) = 2-3")
                recs.append(f"    → Réduire MAX_ACCELERATION dans INI")
            elif max_err_um > 100:  # > 100µm
                recs.append(f"🟠 {axis}: Erreur {max_err_um:.0f}µm (cible: {TARGET_ERROR_UM}µm)")
                recs.append(f"    → Augmenter P08-19 (Vel FF) à 95%")
                recs.append(f"    → Augmenter P08-02 (Pos Kp) +20%")
            elif max_err_um > 50:  # > 50µm
                recs.append(f"🟡 {axis}: Erreur {max_err_um:.0f}µm (cible: {TARGET_ERROR_UM}µm)")
                recs.append(f"    → Ajuster P08-19 (Vel FF) +5%")
                recs.append(f"    → Ajouter P08-21 (Torque FF) ~25%")
            elif max_err_um > TARGET_ERROR_UM:  # > 20µm
                recs.append(f"⚡ {axis}: Erreur {max_err_um:.1f}µm (cible: {TARGET_ERROR_UM}µm)")
                recs.append(f"    → Ajustement fin P08-19/P08-21 (+2-5%)")
        
        # === OSCILLATIONS (vibrations) ===
        if osc_rate > 40:
            recs.append(f"🔄 {axis}: VIBRATIONS! ({osc_rate:.0f}%)")
            recs.append(f"    → RÉDUIRE P08-00 (Speed Kp) -20%")
            recs.append(f"    → AUGMENTER P08-01 (Speed Ti) +30%")
        elif osc_rate > 25 and is_moving:
            recs.append(f"〰️  {axis}: Oscillations ({osc_rate:.0f}%)")
            recs.append(f"    → Réduire P08-00 (Speed Kp) -10%")
        
        # === ERREUR STATIQUE ===
        if not is_moving and abs(avg_um) > 5:  # > 5µm au repos
            recs.append(f"📍 {axis}: Erreur statique {avg_um:.1f}µm")
            recs.append(f"    → Augmenter P08-02 (Position Kp)")
    
    # === BILAN ===
    if not recs:
        if is_moving:
            max_um = max((x_analysis['max'] if x_analysis else 0),
                        (y_analysis['max'] if y_analysis else 0)) * 1000
            if max_um <= TARGET_ERROR_UM:
                recs.append(f"🌟 PARFAIT! {max_um:.1f}µm ≤ {TARGET_ERROR_UM}µm")
            else:
                recs.append(f"Erreur: {max_um:.1f}µm")
        else:
            recs.append("✅ Position stable")
    
    return recs

def print_header():
    print("\033[2J\033[H")  # Clear screen
    print("=" * 70)
    print(f"  LC10E TUNING ADVISOR - Cible: {TARGET_ERROR_UM}µm (0.02mm)")
    print("=" * 70)
    print("  Ctrl+C pour quitter")
    print("-" * 70)

def main():
    print_header()
    
    # Check LinuxCNC running
    if not get_bit("iocontrol.0.emc-enable-in"):
        print("\n⚠️  LinuxCNC ne semble pas actif!")
        print("   Démarrez LinuxCNC et activez la machine.")
        sys.exit(1)
    
    last_x_cmd = 0
    last_y_cmd = 0
    sample_count = 0
    
    try:
        while True:
            # Read current values
            x_err = get_pin("joint.0.f-error")
            y_err = get_pin("joint.1.f-error")
            x_cmd = get_pin("joint.0.motor-pos-cmd")
            y_cmd = get_pin("joint.1.motor-pos-cmd")
            x_enabled = get_bit("cia402.0.stat-op-enabled")
            y_enabled = get_bit("cia402.1.stat-op-enabled")
            
            # Store history
            if x_err is not None:
                x_error_history.append(x_err)
            if y_err is not None:
                y_error_history.append(y_err)
            
            # Detect movement
            velocity_x = abs(x_cmd - last_x_cmd) * 100 if x_cmd else 0  # mm/s approx
            velocity_y = abs(y_cmd - last_y_cmd) * 100 if y_cmd else 0
            is_moving = velocity_x > 0.1 or velocity_y > 0.1
            velocity = max(velocity_x, velocity_y)
            
            last_x_cmd = x_cmd if x_cmd else 0
            last_y_cmd = y_cmd if y_cmd else 0
            
            # Analyze every 50 samples (0.5s)
            sample_count += 1
            if sample_count >= 50:
                sample_count = 0
                
                print_header()
                
                # Status
                print(f"\n📊 ÉTAT SYSTÈME:")
                print(f"   Drive X: {'✅ Activé' if x_enabled else '⭕ Désactivé'}")
                print(f"   Drive Y: {'✅ Activé' if y_enabled else '⭕ Désactivé'}")
                print(f"   Mouvement: {'🔄 En cours' if is_moving else '⏸️  Arrêté'}", end="")
                if is_moving:
                    print(f" ({velocity:.1f} mm/s)")
                else:
                    print()
                
                # Current errors - AFFICHAGE EN µm
                print(f"\n📏 ERREURS DE SUIVI:")
                if x_err is not None:
                    x_um = x_err * 1000
                    x_ok = "✓" if abs(x_um) <= TARGET_ERROR_UM else "✗"
                    x_bar = "█" * min(int(abs(x_um) / 2), 30)
                    print(f"   X: {x_ok} {x_um:+8.1f} µm  |{x_bar}")
                else:
                    print(f"   X: N/A")
                if y_err is not None:
                    y_um = y_err * 1000
                    y_ok = "✓" if abs(y_um) <= TARGET_ERROR_UM else "✗"
                    y_bar = "█" * min(int(abs(y_um) / 2), 30)
                    print(f"   Y: {y_ok} {y_um:+8.1f} µm  |{y_bar}")
                else:
                    print(f"   Y: N/A")
                
                # Analysis
                x_analysis = analyze_errors(x_error_history)
                y_analysis = analyze_errors(y_error_history)
                
                if x_analysis or y_analysis:
                    print(f"\n📈 STATS (5s):  [cible ≤{TARGET_ERROR_UM}µm]")
                    if x_analysis:
                        x_status = "✓" if x_analysis['max']*1000 <= TARGET_ERROR_UM else "✗"
                        print(f"   X: {x_status} max={x_analysis['max']*1000:6.1f}µm  moy={x_analysis['avg']*1000:+6.1f}µm  osc={x_analysis['oscillation_rate']:2.0f}%")
                    if y_analysis:
                        y_status = "✓" if y_analysis['max']*1000 <= TARGET_ERROR_UM else "✗"
                        print(f"   Y: {y_status} max={y_analysis['max']*1000:6.1f}µm  moy={y_analysis['avg']*1000:+6.1f}µm  osc={y_analysis['oscillation_rate']:2.0f}%")
                
                # Recommendations
                recs = get_recommendations(x_analysis, y_analysis, is_moving, velocity)
                print(f"\n💡 RECOMMANDATIONS:")
                for rec in recs:
                    print(f"   {rec}")
                
                # Parameter hints
                print(f"\n📝 PARAMÈTRES CLÉS:")
                print(f"   P08-19 Vel FF:    ↑ réduit erreur (mettre 80-100%)")
                print(f"   P08-00 Speed Kp:  ↓ si vibrations")
                print(f"   P08-01 Speed Ti:  ↑ si oscillations")
                print(f"   P08-02 Pos Kp:    ↑ réduit erreur statique")
                print(f"   P08-21 Torque FF: 20-40% aide inertie")
                
                print("\n" + "-" * 70)
                print(f"  🎯 OBJECTIF: Erreur max ≤ {TARGET_ERROR_UM}µm pendant mouvement")
            
            time.sleep(0.01)  # 100Hz sampling
            
    except KeyboardInterrupt:
        print("\n\n✅ Monitoring terminé.")
        
        # Final summary
        x_analysis = analyze_errors(x_error_history)
        y_analysis = analyze_errors(y_error_history)
        
        if x_analysis or y_analysis:
            print("\n📊 RÉSUMÉ FINAL:")
            if x_analysis:
                status = "✓" if x_analysis['max']*1000 <= TARGET_ERROR_UM else "✗"
                print(f"   X: {status} max={x_analysis['max']*1000:.1f}µm")
            if y_analysis:
                status = "✓" if y_analysis['max']*1000 <= TARGET_ERROR_UM else "✗"
                print(f"   Y: {status} max={y_analysis['max']*1000:.1f}µm")

if __name__ == "__main__":
    main()
