# Agent Claude — Projet LinuxCNC CNC Ma_CNC

## Vue d'ensemble

Machine CNC multi-axes avec LinuxCNC + EtherCAT, actuellement configurée mono-axe X.

- **Plateforme** : LinuxCNC (AXIS display + PyVCP) sur Raspberry Pi
- **Bus temps-réel** : EtherCAT @ 500 µs (2 kHz)
- **Variateur** : Lichuan LC10E (CIA402 générique, opmode 9 CSV)
- **Encodeur** : 17-bit absolu — vis 5 mm/tr → échelle **26214.4 counts/mm**
- **Mode de contrôle** : CSV — PID position dans LinuxCNC, boucle vitesse dans le variateur

## Topologie EtherCAT complète

| Pos | Esclave | Type | HAL préfixe | État |
|-----|---------|------|-------------|------|
| 0 | Lichuan LC10E | Generic CIA402 | `lcec.0.0.*` | Configuré |
| 1 | Beckhoff EK1100 | Bus coupler | *(pas de pins HAL)* | Présent |
| 2 | Beckhoff EL2024 #1 | 4×DO 24V 2A | `lcec.0.2.*` | Configuré |
| 3 | Beckhoff EL2024 #2 | 4×DO 24V 2A | `lcec.0.3.*` | À configurer |
| 4 | Beckhoff EL2024 #3 | 4×DO 24V 2A | `lcec.0.4.*` | À configurer |
| 5 | Beckhoff EL2008 #1 | 8×DO 24V 0.5A | `lcec.0.5.*` | Câblé VFD (non cfg) |
| 6 | Beckhoff EL2008 #2 | 8×DO 24V 0.5A | `lcec.0.6.*` | À configurer |
| 7 | Beckhoff EL4002 | 2×AO ±10V | `lcec.0.7.*` | Câblé VFD (non cfg) |
| 8 | Beckhoff EL1008 #1 | 8×DI 24V | `lcec.0.8.*` | À configurer |
| 9 | Beckhoff EL1008 #2 | 8×DI 24V | `lcec.0.9.*` | À configurer |
| 10 | Beckhoff EL1008 #3 | 8×DI 24V | `lcec.0.10.*` | Câblé VFD (non cfg) |

Voir `docs/beckhoff_io_mapping.md` pour le détail canal par canal.

## Fichiers clés

| Fichier | Rôle |
|---------|------|
| `LC10E.ini` | Config principale (axes, période servo 500µs, display AXIS) |
| `LC10E.hal` | HAL principal : chargement composants, routage signaux |
| `pid_tuning.hal` | Gains PID auto-optimisés — ne pas éditer à la main |
| `ethercat-conf.xml` | Topologie EtherCAT + mapping PDO (WIP : seulement pos 0+2) |
| `cia402.comp` | Composant HAL machine d'état CIA402 |
| `pyvcp_panel.xml` | Interface PyVCP gauche d'AXIS |
| `pyvcp_panel.hal` | Connexions HAL vers PyVCP (chargé en POSTGUI) |
| `docs/beckhoff_io_mapping.md` | **Mapping complet I/O Beckhoff** |
| `LC10E_parameters.csv` | Paramètres du variateur LC10E |

## Chaîne HAL (thread servo, 500 µs)

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

## VFD Broche — câblage Beckhoff

**Commandes** EL2008 #1 (pos 5, `lcec.0.5.*`) → bornes X du VFD (correspondance directe : output-N → XN) :
- output-1 → X1 Forward
- output-2 → X2 Reverse
- output-3 → X3 Stop
- output-4 → X4 Jog Forward
- output-5 → X5 Jog Reverse / Vitesse fixe 1
- output-6 → X6 Reset défaut
- output-7 → X7 Enable
- output-8 → X8 External Fault

**Retours** VFD → EL1008 #3 (pos 10, `lcec.0.10.*`) :
- input-1 ← Y1 Fault
- input-2 ← Y2 Run

**Référence analogique** EL4002 (pos 7, `lcec.0.7.*`) :
- channel-1 → VFD AVI (0-10V = consigne vitesse broche)
- channel-2 → Référence fixe 100% (`setp lcec.0.7.channel-2 32767`)

## Sécurités prévues (EL1008 #1, #2)

- E-stop (câblage NC : HIGH=OK, LOW=déclenché)
- Palpeur pièce
- Capteur outil dans broche / longueur outil
- Pressostat air
- Fins de course : sur les variateurs drives (pas sur Beckhoff)
