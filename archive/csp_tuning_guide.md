# Guide de Tuning CSP pour LC10E

## Diagnostic Initial

### Symptômes typiques et causes

| Symptôme | Cause probable | Solution |
|----------|----------------|----------|
| Erreur de suivi constante | Feedforward désactivé | Activer P08-19, P08-21 |
| Vibrations haute fréquence | Gain vitesse trop élevé | Réduire P08-00 |
| Vibrations basse fréquence | Gain position trop élevé | Réduire P08-02 |
| Oscillations à l'arrêt | Integral time trop court | Augmenter P08-01 |
| Réponse molle | Gains trop faibles | Augmenter progressivement |

## Procédure de Tuning Étape par Étape

### Étape 1: Mesurer l'erreur actuelle

```bash
# Démarrer LinuxCNC puis observer l'erreur de suivi
halcmd show pin joint.0.f-error
halcmd show pin joint.1.f-error

# Ou en continu
watch -n 0.5 "halcmd getp joint.0.f-error"
```

### Étape 2: Régler le ratio d'inertie (P08-15)

Le ratio d'inertie doit correspondre à: `Inertie_charge / Inertie_moteur`

Pour un ballscrew 5mm avec courroie ou direct drive:
- Direct drive typique: **1-3**
- Avec réducteur: **0.5-1**

```bash
# Accéder aux paramètres via SDO (LinuxCNC doit être arrêté)
sudo /usr/local/etherlab/bin/ethercat upload -p0 0x2008 1  # Lire P08-15 actuel
```

### Étape 3: Activer le Velocity Feedforward (CRITIQUE pour CSP)

En mode CSP, LinuxCNC envoie une position toutes les 1ms. Le drive doit anticiper:

**Paramètres recommandés:**
- **P08-19** (Velocity feedforward gain): **70-100%** (actuellement 0!)
- **P08-18** (Filter time): **0.5-1.0 ms**

### Étape 4: Ajuster les gains de boucle

**Ordre de réglage: Interne → Externe** (Vitesse avant Position)

#### Boucle de vitesse (P08-00, P08-01):
1. Réduire P08-00 à 30 (plus conservateur)
2. Si vibrations: augmenter P08-01 (temps intégral)
3. Augmenter P08-00 progressivement jusqu'à vibrations, puis reculer de 20%

#### Boucle de position (P08-02):
1. Commencer à 30
2. Augmenter jusqu'à oscillations, reculer de 30%

### Étape 5: Torque Feedforward (optionnel mais aide)

- **P08-21**: 0-50% (commencer à 20%)
- **P08-20**: 0.5 ms (filtre)

## Valeurs de Départ Recommandées

```
# CONSERVATIVE - pour éliminer les vibrations d'abord
P08-00 = 30      # Speed loop gain (était 50)
P08-01 = 40      # Speed integral time (était 31.83)
P08-02 = 30      # Position loop gain (était 40)
P08-15 = 2       # Inertia ratio (était 1)
P08-18 = 0.5     # Velocity FF filter
P08-19 = 80      # Velocity feedforward gain (était 0!)
P08-20 = 0.5     # Torque FF filter  
P08-21 = 20      # Torque feedforward gain (était 0)

# FERROR dans INI (réduire après tuning)
FERROR = 5.0     # 5mm max (était 500!)
MIN_FERROR = 0.5 # 0.5mm
```

## Commandes SDO pour modifier les paramètres

```bash
# ATTENTION: LinuxCNC doit être ARRÊTÉ

# P08-00: Speed loop gain (0x2008:01 ou via mapping interne)
# Les paramètres P08-xx sont souvent accessibles via:
sudo /usr/local/etherlab/bin/ethercat download -p0 -t int16 0x2008 1 30

# Vérifier la valeur
sudo /usr/local/etherlab/bin/ethercat upload -p0 0x2008 1
```

## Test de Validation

### Test 1: Mouvement lent
```gcode
G1 X100 F100
G1 X0 F100
```
Observer l'erreur de suivi - devrait être < 0.5mm

### Test 2: Mouvement rapide
```gcode
G1 X100 F3000
G1 X0 F3000
```
Erreur acceptable: < 2mm pendant le mouvement

### Test 3: Inversion de direction
```gcode
G1 X50 F1000
G1 X-50 F1000
```
Pas de vibrations à l'inversion

## Notes Importantes

1. **Sauvegarder les paramètres actuels avant modification**
2. **Un seul paramètre à la fois**
3. **Tester après chaque changement**
4. **Les paramètres P08-xx sont dans le drive, pas dans LinuxCNC**

## Diagnostic Avancé avec HAL

```bash
# Créer un fichier de log pour analyser
halcmd loadusr halscope

# Signaux à observer:
# - joint.0.motor-pos-cmd (commande)
# - joint.0.motor-pos-fb (feedback)
# - joint.0.f-error (erreur)
```
