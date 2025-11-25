#!/usr/bin/env python3
"""
Diagnostic complet de l'erreur de suivi LC10E
Analyse les causes possibles:
1. Délai EtherCAT (1-2 cycles)
2. Gains servo insuffisants
3. Feedforward mal configuré
4. Problèmes de timing
"""

import subprocess
import time
import sys

def hal_get(pin):
    """Get HAL pin value"""
    try:
        result = subprocess.run(['halcmd', 'getp', pin], 
                              capture_output=True, text=True, timeout=1)
        val = result.stdout.strip()
        if val == 'TRUE':
            return 1.0
        elif val == 'FALSE':
            return 0.0
        return float(val)
    except:
        return None

def hal_show_sig(sig):
    """Show HAL signal"""
    try:
        result = subprocess.run(['halcmd', 'show', 'sig', sig], 
                              capture_output=True, text=True, timeout=1)
        return result.stdout.strip()
    except:
        return None

def ethercat_read(slave, index, subindex, dtype):
    """Read SDO from drive"""
    try:
        result = subprocess.run([
            'sudo', '/usr/local/etherlab/bin/ethercat', 'upload',
            f'-p{slave}', f'0x{index:04x}', str(subindex), '--type', dtype
        ], capture_output=True, text=True, timeout=2)
        if result.returncode == 0:
            # Parse hex value
            parts = result.stdout.strip().split()
            if len(parts) >= 2:
                return int(parts[1])
        return None
    except:
        return None

print("=" * 70)
print("  DIAGNOSTIC ERREUR DE SUIVI LC10E")
print("=" * 70)

# 1. Vérifier si système actif
enabled = hal_get('cia402.0.stat-op-enabled')
if enabled is None:
    print("\n❌ LinuxCNC n'est pas actif. Lancez-le d'abord.")
    sys.exit(1)

print(f"\n📊 ÉTAT SYSTÈME:")
print(f"   Drive X activé: {'✓' if enabled else '✗'}")

# 2. Position et erreur actuelles
pos_cmd = hal_get('joint.0.motor-pos-cmd')
pos_fb = hal_get('joint.0.motor-pos-fb')
f_error = hal_get('joint.0.f-error')
vel_cmd = hal_get('joint.0.vel-cmd')
vel_fb = hal_get('cia402.0.velocity-fb')

print(f"\n📏 POSITION & ERREUR:")
print(f"   Position cmd:  {pos_cmd:.4f} mm")
print(f"   Position fb:   {pos_fb:.4f} mm")
print(f"   Following err: {f_error*1000:.1f} µm")
print(f"   Velocity cmd:  {vel_cmd:.2f} mm/s")
print(f"   Velocity fb:   {vel_fb:.2f} mm/s" if vel_fb else "   Velocity fb:   N/A")

# 3. Vérifier le velocity offset
vel_offset = hal_get('lcec.0.0.velocity-offset')
print(f"\n🔄 VELOCITY FEEDFORWARD:")
print(f"   Velocity offset envoyé: {vel_offset} counts/s")
if vel_cmd and vel_offset:
    expected = vel_cmd * 26214.4
    print(f"   Attendu (vel_cmd × scale): {expected:.0f} counts/s")
    if abs(vel_offset - expected) < 100:
        print(f"   ✓ Feedforward HAL OK")
    else:
        print(f"   ⚠ Feedforward HAL différent!")

# 4. Analyser le timing
tmax = hal_get('servo-thread.tmax')
period = 1000000  # 1ms en ns
print(f"\n⏱️  TIMING:")
print(f"   Servo period: {period/1000:.0f} µs")
print(f"   Servo tmax:   {tmax/1000:.1f} µs ({tmax/period*100:.1f}% du cycle)")
if tmax > period * 0.8:
    print(f"   ⚠ ATTENTION: Temps d'exécution proche de la limite!")

# 5. Calculer le délai EtherCAT théorique
print(f"\n📡 DÉLAI ETHERCAT THÉORIQUE:")
print(f"   Délai typique: 2 cycles = 2ms")
if vel_cmd:
    delay_error = abs(vel_cmd) * 0.002  # 2ms delay
    print(f"   Erreur due au délai @ {vel_cmd:.1f}mm/s: {delay_error*1000:.1f} µm")
    if abs(f_error) > delay_error * 2:
        print(f"   ⚠ Erreur observée >> délai théorique: autre cause probable!")

# 6. Lire paramètres drive via SDO
print(f"\n🔧 PARAMÈTRES DRIVE (SDO):")

# Position window time
pwt = ethercat_read(0, 0x6068, 0, 'uint16')
if pwt:
    print(f"   0x6068 Position window time: {pwt} ms")
    
# Position window  
pw = ethercat_read(0, 0x6067, 0, 'uint32')
if pw:
    print(f"   0x6067 Position window: {pw} counts ({pw/26214.4*1000:.1f} µm)")

# Following error window
few = ethercat_read(0, 0x6065, 0, 'uint32')
if few:
    print(f"   0x6065 Following error window: {few} counts ({few/26214.4:.1f} mm)")

# 7. Vérifier l'asymétrie
print(f"\n🔬 ANALYSE:")

if f_error and vel_cmd:
    # Erreur/vitesse ratio
    if abs(vel_cmd) > 0.1:
        ratio = abs(f_error) / abs(vel_cmd) * 1000  # µm per mm/s
        print(f"   Ratio erreur/vitesse: {ratio:.1f} µm/(mm/s)")
        
        # Interpréter
        if ratio < 5:
            print(f"   → Excellent! Feedforward efficace")
        elif ratio < 20:
            print(f"   → Bon, mais peut être amélioré")
        elif ratio < 50:
            print(f"   → Feedforward partiellement efficace ou gain Kp bas")
        else:
            print(f"   → Problème majeur: feedforward inactif ou mauvais gains")
            
        # Suggestions
        print(f"\n💡 SUGGESTIONS:")
        if ratio > 20:
            print(f"   1. Vérifier P05-19 = 2 (utiliser 0x60B1)")
            print(f"   2. Augmenter P08-02 (Kp position) de 120 → 200-300")
            print(f"   3. Vérifier P08-19 (feedforward gain) = 95-100%")
        if ratio > 50:
            print(f"   4. ⚠ Possible que 0x60B1 ne soit pas utilisé par le drive")
            print(f"   5. Essayer CSV mode au lieu de CSP")

print(f"\n" + "=" * 70)
print("  Lancez un mouvement X pour voir l'erreur en temps réel")
print("=" * 70)

# Mode monitoring
print("\n🔄 Mode monitoring (Ctrl+C pour quitter)...")
try:
    max_err = 0
    samples = []
    while True:
        f_err = hal_get('joint.0.f-error')
        v_cmd = hal_get('joint.0.vel-cmd')
        v_fb = hal_get('cia402.0.velocity-fb')
        v_off = hal_get('lcec.0.0.velocity-offset')
        
        if f_err is not None:
            err_um = f_err * 1000
            if abs(err_um) > abs(max_err):
                max_err = err_um
            
            # Direction indicator
            if v_cmd and abs(v_cmd) > 0.5:
                direction = "→" if v_cmd > 0 else "←"
                samples.append((v_cmd, err_um))
            else:
                direction = "·"
            
            print(f"\r{direction} Err: {err_um:+7.1f}µm | Max: {max_err:+7.1f}µm | "
                  f"Vel: {v_cmd:+6.1f}mm/s | VelOff: {v_off:+10.0f}    ", end='')
        
        time.sleep(0.05)
        
except KeyboardInterrupt:
    print("\n\n📊 RÉSUMÉ:")
    print(f"   Erreur max observée: {max_err:.1f} µm")
    
    # Analyser asymétrie
    if samples:
        pos_errs = [e for v, e in samples if v > 1]
        neg_errs = [e for v, e in samples if v < -1]
        if pos_errs and neg_errs:
            avg_pos = sum(pos_errs) / len(pos_errs)
            avg_neg = sum(neg_errs) / len(neg_errs)
            print(f"   Erreur moy direction +: {avg_pos:+.1f} µm")
            print(f"   Erreur moy direction -: {avg_neg:+.1f} µm")
            if abs(avg_pos - avg_neg) > 50:
                print(f"   ⚠ ASYMÉTRIE DÉTECTÉE: problème feedforward ou backlash")
