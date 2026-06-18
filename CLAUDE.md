# Agent Claude — Projet LinuxCNC CNC Ma_CNC

## Vue d'ensemble

Machine CNC multi-axes avec LinuxCNC + EtherCAT, actuellement configurée mono-axe X.

- **Plateforme** : LinuxCNC (AXIS display + PyVCP) sur Raspberry Pi
- **Bus temps-réel** : EtherCAT @ 1 ms (1 kHz) — voir note ci-dessous
- **Variateur** : Lichuan LC10E (CIA402 générique, opmode 9 CSV)
- **Encodeur** : 17-bit absolu — vis 5 mm/tr → échelle **26214.4 counts/mm**
- **Mode de contrôle** : CSV — PID position dans LinuxCNC, boucle vitesse dans le variateur

## Topologie EtherCAT complète

Vérifiée avec `ethercat slaves` le 2026-06-17. VID Beckhoff=0x00000002, VID Lichuan=0x00000766.

| Pos | Esclave | PID | HAL préfixe | État |
|-----|---------|-----|-------------|------|
| 0 | Beckhoff EK1100 | 0x044c2c52 | *(pas de pins HAL)* | OP — Bus coupler |
| 1 | Beckhoff EL2024 #1 | 0x07e83052 | `lcec.0.1.*` | Configuré — Freins X/Y/Z |
| 2 | Beckhoff EL2024 #2 | 0x07e83052 | `lcec.0.2.*` | À configurer |
| 3 | Beckhoff EL2024 #3 | 0x07e83052 | `lcec.0.3.*` | À configurer |
| 4 | Beckhoff EL2008 #1 | 0x07d83052 | `lcec.0.4.*` | Configuré — VFD X1-X8 |
| 5 | Beckhoff EL2008 #2 | 0x07d83052 | `lcec.0.5.*` | À configurer |
| 6 | Beckhoff EL4002 | 0x0fa23052 | `lcec.0.6.*` | Configuré — AVI vitesse broche |
| 7 | Beckhoff EL1008 #1 | 0x03f03052 | `lcec.0.7.*` | Configuré — VFD Y1(Fault)/Y2(Run) |
| 8 | Beckhoff EL1008 #2 | 0x03f03052 | `lcec.0.8.*` | À configurer — Sécurités |
| 9 | Beckhoff EL1008 #3 | 0x03f03052 | `lcec.0.9.*` | À configurer — Palpeur/outil |
| 10 | Lichuan LC10E X | 0x00000402 | `lcec.0.10.*` | Configuré — Joint 0 |
| 11 | Lichuan LC10E Y | 0x00000402 | `lcec.0.11.*` | Configuré — Joint 1 |
| 12 | Lichuan LC10E Z | 0x00000402 | `lcec.0.12.*` | Configuré — Joint 2 |

Voir `docs/beckhoff_io_mapping.md` pour le détail canal par canal.

## Fichiers clés

| Fichier | Rôle |
|---------|------|
| `LC10E.ini` | Config principale (axes, période servo 1ms, display AXIS) |
| `LC10E.hal` | HAL principal : chargement composants, routage signaux |
| `pid_tuning.hal` | Gains PID auto-optimisés — ne pas éditer à la main |
| `spindle.hal` | Contrôle broche VFD : EL2008 #1 (pos 7) + EL4002 (pos 9) + EL1008 #3 (pos 12) |
| `ethercat-conf.xml` | Topologie EtherCAT + mapping PDO pour tous les slaves actifs |
| `cia402.comp` | Composant HAL machine d'état CIA402 |
| `pyvcp_panel.xml` | Interface PyVCP gauche d'AXIS |
| `pyvcp_panel.hal` | Connexions HAL vers PyVCP (chargé en POSTGUI) |
| `docs/beckhoff_io_mapping.md` | **Mapping complet I/O Beckhoff** |
| `LC10E_parameters.csv` | Paramètres du variateur LC10E |

## Période EtherCAT / servo : 1 ms (NE PAS repasser à 500 µs)

Symptôme si trop rapide : les servos Y/Z (slaves 11, 12, fin de chaîne) s'arrêtent
en plein G-code **sans erreur LinuxCNC**, et le jog remarche après. Cause : perte de
synchro DC (flag `E` sur `ethercat slaves`, `Failed to receive DC sync check datagram`
dans dmesg sur 0-11/0-12, datagrammes `UNMATCHED`/`SKIPPED`).

Le commit `2fd9ec8` tournait à 500 µs **mais avec 2 esclaves seulement**. Avec 13
esclaves + 3 drives DC, le master du Pi ne tient plus le cycle 500 µs. Toutes les
configs de référence (forum Lichuan, omrg5, el8_original 4-servos, generic-complex)
tournent à **1 ms**. Deux valeurs à garder synchronisées :
- `ethercat-conf.xml` : `appTimePeriod="1000000"`
- `LC10E.ini` : `SERVO_PERIOD = 1000000`

## Chaîne HAL (thread servo, 1 ms)

```
lcec.read-all
  → cia402.0.read-all          (décode statusword, scale counts → mm)
  → motion-command-handler
  → motion-controller          (génère position-cmd)
  → pid.x.do-pid-calcs         (erreur pos → commande vit mm/s)
  → cia402.0.write-all         (scale vit → counts/s, génère controlword)
  → lcec.write-all
```

## Signaux HAL principaux (axe X)

| Signal | Source | Destination | Unité |
|--------|--------|-------------|-------|
| `x-pos-cmd` | `joint.0.motor-pos-cmd` | `pid.x.command` | mm |
| `x-pos-fb` | `cia402.0.pos-fb` | `pid.x.feedback`, `joint.0.motor-pos-fb` | mm |
| `x-vel-cmd` | `pid.x.output` | `cia402.0.velocity-cmd` | mm/s |
| `x-enable` | `joint.0.amp-enable-out` | pid, cia402, EL2024 canaux 1-4 | bit |
| `x-fault` | `cia402.0.drv-fault` | `joint.0.amp-fault-in` | bit |

## Diagnostic d'erreurs

En cas d'erreur au démarrage de LinuxCNC, toujours vérifier :
```
cat /tmp/linuxcnc.report
```
Le rapport contient les messages de debug HAL/PyVCP et la trace d'arrêt. Les erreurs PyVCP (ex: `Duplicate pin name`) apparaissent dans la section "Debug file information".

## Décision critique : mode CSV

Le LC10E a un bug firmware en mode CSP (dérive de position). Solution : CSV (opmode=9).
Erreur de poursuite RMS obtenue : ~13 µm. Ne pas passer en CSP sans investigation.

## PID Axe X (auto-optimisé, ne pas toucher manuellement)

```
Pgain=57.5  Igain=51.6  Dgain=0.00135  FF1=0.998  FF2=0.00040  maxoutput=500 mm/s
```

## Moteur broche

- **Pôles** : 8 pôles
- **Ratio Hz/RPM** : 1 Hz = 15 RPM  (formule : Hz × 120/8)
- **Fréquence nominale** : 400 Hz = 6000 RPM
- **Fréquence max** : 1200 Hz = 18000 RPM
- **AVI 10V** = 1200 Hz (max VFD)
- **Scale EL4002** : `setp lcec.0.6.aout-0-scale 18000.0`  (18000 RPM = 10V = 1200 Hz)

### Paramètre VFD pour affichage RPM correct

Le VFD doit connaître le moteur pour convertir Hz → RPM affiché.
Chercher le paramètre "Speed/Frequency Ratio" ou "Motor Rated Speed/Frequency" :

| Paramètre VFD          | Valeur à entrer |
|------------------------|-----------------|
| Fréquence nominale     | **400 Hz**      |
| Vitesse nominale       | **6000 RPM**    |
| Nombre de pôles        | **8**           |
| Speed/Freq ratio       | **15 RPM/Hz**   |

→ Le VFD calculera : RPM_affiché = Hz_sortie × 15

## VFD Broche — câblage Beckhoff

**Commandes** EL2008 #1 (pos 4, `lcec.0.4.*`) → bornes X du VFD (dout-N, 0-indexé) :
- dout-0 → X1 Forward
- dout-1 → X2 Reverse
- dout-2 → X3 Stop
- dout-3 → X4 Jog Forward
- dout-4 → X5 Jog Reverse / Vitesse fixe 1
- dout-5 → X6 Reset défaut
- dout-6 → X7 Enable
- dout-7 → X8 External Fault

**Retours** VFD → EL1008 #1 (pos 7, `lcec.0.7.*`) :
- din-0 ← Y1 Fault
- din-1 ← Y2 Run

**Référence analogique** EL4002 (pos 6, `lcec.0.6.*`) :
- aout-0-value → VFD AVI (float RPM, 0-10V via scale=18000)  ← 18000 RPM = 10V = 1200 Hz
- aout-1-value → Référence fixe 100% (`setp lcec.0.6.aout-1-value 1.0`)

## Sécurités prévues (EL1008 #1, #2)

- E-stop (câblage NC : HIGH=OK, LOW=déclenché)
- Palpeur pièce
- Capteur outil dans broche / longueur outil
- Pressostat air
- Fins de course : sur les variateurs drives (pas sur Beckhoff)
