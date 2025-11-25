#!/usr/bin/env python3
"""
LC10E CSP Mode Tuning Assistant
Aide au réglage des paramètres servo pour mode CSP sans vibrations

Usage: python3 csp_tuner.py
"""

import subprocess
import sys
import time

ETHERCAT_BIN = "/usr/local/etherlab/bin/ethercat"

# Mapping des paramètres importants pour CSP
# Format: (index, subindex, nom, unité, min, max, défaut, description)
CSP_PARAMS = {
    'speed_kp': {
        'pcode': 'P08-00',
        'index': 0x2008, 'subindex': 0x01,
        'name': 'Speed Loop Gain',
        'unit': 'Hz', 'min': 0.1, 'max': 2000, 'default': 25,
        'desc': 'Gain proportionnel boucle vitesse - Augmenter = plus réactif mais risque vibrations'
    },
    'speed_ti': {
        'pcode': 'P08-01',
        'index': 0x2008, 'subindex': 0x02,
        'name': 'Speed Loop Integral Time',
        'unit': 'ms', 'min': 0.15, 'max': 512, 'default': 31.83,
        'desc': 'Temps intégral vitesse - Augmenter = stabilité, Diminuer = erreur statique réduite'
    },
    'pos_kp': {
        'pcode': 'P08-02',
        'name': 'Position Loop Gain',
        'unit': '1/s', 'min': 0, 'max': 2000, 'default': 40,
        'desc': 'Gain boucle position - Plus élevé = suivi plus serré'
    },
    'inertia_ratio': {
        'pcode': 'P08-15',
        'name': 'Load Inertia Ratio',
        'unit': 'x', 'min': 0, 'max': 120, 'default': 2,
        'desc': 'Ratio inertie charge/moteur - Crucial pour la stabilité'
    },
    'vel_ff_filter': {
        'pcode': 'P08-18',
        'name': 'Velocity FF Filter',
        'unit': 'ms', 'min': 0, 'max': 64, 'default': 0.5,
        'desc': 'Filtre feedforward vitesse'
    },
    'vel_ff_gain': {
        'pcode': 'P08-19',
        'name': 'Velocity Feedforward Gain',
        'unit': '%', 'min': 0, 'max': 100, 'default': 0,
        'desc': 'CRITIQUE en CSP! 70-100% recommandé pour réduire erreur de suivi'
    },
    'torque_ff_filter': {
        'pcode': 'P08-20',
        'name': 'Torque FF Filter',
        'unit': 'ms', 'min': 0, 'max': 64, 'default': 0.5,
        'desc': 'Filtre feedforward couple'
    },
    'torque_ff_gain': {
        'pcode': 'P08-21',
        'name': 'Torque Feedforward Gain',
        'unit': '%', 'min': 0, 'max': 200, 'default': 0,
        'desc': 'Feedforward couple - Aide avec charges inertielles'
    },
    'rigidity': {
        'pcode': 'P09-01',
        'name': 'Rigidity Grade',
        'unit': '', 'min': 0, 'max': 31, 'default': 12,
        'desc': 'Classe de rigidité - 0=souple, 31=rigide'
    },
}

# Profils de tuning pré-définis
TUNING_PROFILES = {
    'conservative': {
        'name': 'Conservateur (anti-vibration)',
        'desc': 'Gains bas, haute stabilité, erreur de suivi plus grande',
        'params': {
            'speed_kp': 25,
            'speed_ti': 50,
            'pos_kp': 25,
            'vel_ff_gain': 70,
            'torque_ff_gain': 10,
            'inertia_ratio': 2,
        }
    },
    'balanced': {
        'name': 'Équilibré',
        'desc': 'Bon compromis entre réactivité et stabilité',
        'params': {
            'speed_kp': 40,
            'speed_ti': 35,
            'pos_kp': 35,
            'vel_ff_gain': 85,
            'torque_ff_gain': 25,
            'inertia_ratio': 2,
        }
    },
    'aggressive': {
        'name': 'Agressif (haute performance)',
        'desc': 'Gains élevés, faible erreur de suivi, risque vibrations',
        'params': {
            'speed_kp': 60,
            'speed_ti': 25,
            'pos_kp': 50,
            'vel_ff_gain': 95,
            'torque_ff_gain': 40,
            'inertia_ratio': 2,
        }
    },
}


def run_cmd(cmd, capture=True):
    """Execute shell command"""
    try:
        result = subprocess.run(cmd, shell=True, capture_output=capture, text=True)
        return result.stdout.strip() if capture else result.returncode
    except Exception as e:
        return f"Error: {e}"


def check_ethercat_status():
    """Check if EtherCAT master is running and slaves are in OP"""
    output = run_cmd(f"sudo {ETHERCAT_BIN} slaves 2>/dev/null")
    if "OP" in output:
        return True, output
    return False, output


def check_linuxcnc_running():
    """Check if LinuxCNC is running"""
    output = run_cmd("pgrep -x linuxcnc")
    return bool(output)


def get_following_error(joint=0):
    """Get current following error from HAL"""
    output = run_cmd(f"halcmd getp joint.{joint}.f-error 2>/dev/null")
    try:
        return float(output)
    except:
        return None


def display_current_status():
    """Display current system status"""
    print("\n" + "="*60)
    print("ÉTAT DU SYSTÈME")
    print("="*60)
    
    # EtherCAT status
    ec_ok, ec_output = check_ethercat_status()
    print(f"\nEtherCAT Master: {'✓ Actif' if ec_ok else '✗ Inactif'}")
    if ec_ok:
        for line in ec_output.split('\n'):
            print(f"  {line}")
    
    # LinuxCNC status
    lc_running = check_linuxcnc_running()
    print(f"\nLinuxCNC: {'✓ En cours' if lc_running else '✗ Arrêté'}")
    
    if lc_running:
        # Get following errors
        for joint in range(2):
            ferror = get_following_error(joint)
            if ferror is not None:
                status = "✓" if abs(ferror) < 1.0 else "⚠" if abs(ferror) < 5.0 else "✗"
                print(f"  Joint {joint} F-Error: {status} {ferror:.3f} mm")
    
    print("="*60)


def display_param_info(key, param):
    """Display parameter information"""
    print(f"\n{param['pcode']}: {param['name']}")
    print(f"  Description: {param['desc']}")
    print(f"  Unité: {param['unit']}")
    print(f"  Plage: {param['min']} - {param['max']}")
    print(f"  Défaut: {param['default']}")


def display_menu():
    """Display main menu"""
    print("\n" + "="*60)
    print("LC10E CSP TUNING ASSISTANT")
    print("="*60)
    print("\n1. Afficher l'état du système")
    print("2. Afficher les paramètres CSP importants")
    print("3. Appliquer un profil de tuning")
    print("4. Modifier un paramètre individuel")
    print("5. Afficher les valeurs recommandées")
    print("6. Générer un G-code de test")
    print("7. Monitorer l'erreur de suivi en temps réel")
    print("0. Quitter")
    print("\n" + "-"*60)


def show_all_params():
    """Display all CSP parameters"""
    print("\n" + "="*60)
    print("PARAMÈTRES CSP IMPORTANTS")
    print("="*60)
    
    for key, param in CSP_PARAMS.items():
        print(f"\n{param['pcode']}: {param['name']}")
        print(f"  {param['desc']}")
        print(f"  Défaut: {param['default']} {param['unit']}")


def show_profiles():
    """Display and apply tuning profiles"""
    print("\n" + "="*60)
    print("PROFILS DE TUNING")
    print("="*60)
    
    for i, (key, profile) in enumerate(TUNING_PROFILES.items(), 1):
        print(f"\n{i}. {profile['name']}")
        print(f"   {profile['desc']}")
        print("   Paramètres:")
        for pkey, value in profile['params'].items():
            pinfo = CSP_PARAMS.get(pkey, {})
            print(f"     {pinfo.get('pcode', pkey)}: {value} {pinfo.get('unit', '')}")
    
    print("\n0. Retour")
    
    choice = input("\nChoisir un profil (ou 0 pour retour): ").strip()
    
    if choice == '0':
        return
    
    try:
        idx = int(choice) - 1
        profile_key = list(TUNING_PROFILES.keys())[idx]
        profile = TUNING_PROFILES[profile_key]
        
        print(f"\nProfil sélectionné: {profile['name']}")
        print("\n⚠️  ATTENTION: Pour modifier les paramètres du drive,")
        print("   LinuxCNC doit être ARRÊTÉ et vous devez utiliser")
        print("   le panneau du drive ou les commandes SDO.")
        print("\nValeurs à appliquer:")
        
        for pkey, value in profile['params'].items():
            pinfo = CSP_PARAMS.get(pkey, {})
            print(f"  {pinfo.get('pcode', pkey)} = {value}")
        
    except (ValueError, IndexError):
        print("Choix invalide")


def show_recommendations():
    """Show tuning recommendations based on symptoms"""
    print("\n" + "="*60)
    print("RECOMMANDATIONS DE TUNING")
    print("="*60)
    
    print("""
VOTRE CONFIGURATION ACTUELLE (d'après le CSV):
  P08-00 (Speed Kp)    = 50  (élevé)
  P08-01 (Speed Ti)    = 31.83 ms
  P08-02 (Position Kp) = 40
  P08-19 (Vel FF)      = 0   ⚠️ DÉSACTIVÉ!
  P08-21 (Torque FF)   = 0   ⚠️ DÉSACTIVÉ!
  P08-15 (Inertia)     = 1

PROBLÈME PRINCIPAL: Feedforward désactivé!
================================

En mode CSP (Cyclic Synchronous Position), LinuxCNC envoie
une nouvelle position cible toutes les 1ms. Sans feedforward,
le drive est toujours "en retard" sur la commande.

ACTIONS RECOMMANDÉES (dans cet ordre):
======================================

1. ACTIVER LE VELOCITY FEEDFORWARD (plus important!)
   P08-19 = 80  (commencer à 80%, ajuster entre 70-100%)
   
2. RÉDUIRE LE GAIN DE VITESSE (si vibrations)
   P08-00 = 35  (au lieu de 50)
   
3. ACTIVER LE TORQUE FEEDFORWARD (optionnel)
   P08-21 = 20  (aide avec l'inertie)
   
4. AJUSTER LE RATIO D'INERTIE
   P08-15 = 2  (valeur typique pour ballscrew direct)

5. RÉDUIRE FERROR DANS INI (après stabilisation)
   FERROR = 5.0  (au lieu de 500!)
   MIN_FERROR = 0.5

COMMENT MODIFIER LES PARAMÈTRES:
================================
Option A - Via le panneau du drive (recommandé)
Option B - Via SDO EtherCAT (LinuxCNC arrêté):

  # Exemple pour P08-19 (velocity feedforward)
  sudo /usr/local/etherlab/bin/ethercat download -p0 -t uint16 0x2008 19 80
  
  # Vérifier
  sudo /usr/local/etherlab/bin/ethercat upload -p0 0x2008 19
""")


def generate_test_gcode():
    """Generate test G-code for tuning validation"""
    gcode = """
; Test G-code pour validation du tuning CSP
; Fichier: tuning-test.ngc
;
; Ce programme teste:
; 1. Mouvements lents (erreur statique)
; 2. Mouvements rapides (erreur dynamique)
; 3. Inversions de direction (stabilité)

G21 ; mm
G90 ; Absolu
G17 ; Plan XY

; Position initiale
G0 X0 Y0

; === TEST 1: Mouvement lent ===
; Observer l'erreur de suivi - devrait être < 0.2mm
(MSG, Test 1: Mouvement lent F100)
G1 X50 F100
G1 X0 F100
G4 P1 ; Pause 1 seconde

; === TEST 2: Mouvement moyen ===
; Erreur acceptable: < 1mm
(MSG, Test 2: Mouvement moyen F500)
G1 X100 F500
G1 X0 F500
G4 P1

; === TEST 3: Mouvement rapide ===
; Erreur acceptable: < 2mm pendant le mouvement
(MSG, Test 3: Mouvement rapide F2000)
G1 X100 F2000
G1 X0 F2000
G4 P1

; === TEST 4: Mouvement diagonal ===
(MSG, Test 4: Diagonal XY)
G1 X50 Y50 F1000
G1 X0 Y0 F1000
G4 P1

; === TEST 5: Inversions rapides ===
; Ne devrait pas vibrer aux inversions
(MSG, Test 5: Inversions)
G1 X20 F2000
G1 X-20 F2000
G1 X20 F2000
G1 X-20 F2000
G1 X0 F2000
G4 P1

; === TEST 6: Carré ===
(MSG, Test 6: Carre 50x50)
G1 X50 F1500
G1 Y50 F1500
G1 X0 F1500
G1 Y0 F1500

(MSG, Test termine)
M2
"""
    
    filepath = "/home/cnc/linuxcnc/nc_files/tuning-test.ngc"
    try:
        with open(filepath, 'w') as f:
            f.write(gcode)
        print(f"\n✓ G-code de test créé: {filepath}")
        print("\nPour l'utiliser dans LinuxCNC:")
        print("  File → Open → tuning-test.ngc")
        print("\nPendant l'exécution, observer l'erreur de suivi avec:")
        print("  halcmd getp joint.0.f-error")
    except Exception as e:
        print(f"Erreur: {e}")


def monitor_ferror():
    """Monitor following error in real-time"""
    print("\n" + "="*60)
    print("MONITORING ERREUR DE SUIVI")
    print("="*60)
    print("Appuyez sur Ctrl+C pour arrêter\n")
    
    if not check_linuxcnc_running():
        print("⚠️  LinuxCNC n'est pas en cours d'exécution!")
        print("   Démarrez LinuxCNC pour voir les erreurs de suivi.")
        return
    
    try:
        while True:
            x_err = get_following_error(0)
            y_err = get_following_error(1)
            
            x_status = "✓" if x_err and abs(x_err) < 1.0 else "⚠" if x_err and abs(x_err) < 5.0 else "✗"
            y_status = "✓" if y_err and abs(y_err) < 1.0 else "⚠" if y_err and abs(y_err) < 5.0 else "✗"
            
            x_str = f"{x_err:+8.3f} mm" if x_err is not None else "N/A"
            y_str = f"{y_err:+8.3f} mm" if y_err is not None else "N/A"
            
            print(f"\rX: {x_status} {x_str}  |  Y: {y_status} {y_str}", end="", flush=True)
            time.sleep(0.1)
            
    except KeyboardInterrupt:
        print("\n\nMonitoring arrêté.")


def main():
    """Main function"""
    print("\n" + "="*60)
    print("  LC10E CSP TUNING ASSISTANT")
    print("  Pour servo drives Lichuan en mode CSP")
    print("="*60)
    
    while True:
        display_menu()
        choice = input("Choix: ").strip()
        
        if choice == '0':
            print("\nAu revoir!")
            break
        elif choice == '1':
            display_current_status()
        elif choice == '2':
            show_all_params()
        elif choice == '3':
            show_profiles()
        elif choice == '4':
            print("\nPour modifier les paramètres, utilisez le panneau du drive")
            print("ou les commandes SDO (voir option 5)")
        elif choice == '5':
            show_recommendations()
        elif choice == '6':
            generate_test_gcode()
        elif choice == '7':
            monitor_ferror()
        else:
            print("Choix invalide")
        
        input("\nAppuyez sur Entrée pour continuer...")


if __name__ == "__main__":
    main()
