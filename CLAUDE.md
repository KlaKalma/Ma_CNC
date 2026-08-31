# Agent Claude — Projet LinuxCNC CNC Ma_CNC

## Vue d'ensemble

Machine CNC multi-axes avec LinuxCNC + EtherCAT, actuellement configurée mono-axe X.

- **Plateforme** : LinuxCNC (AXIS display + PyVCP) sur Raspberry Pi
- **Bus temps-réel** : EtherCAT @ 1 ms (1 kHz) — voir note ci-dessous
- **Variateur** : Lichuan LC10E (CIA402 générique, opmode 9 CSV)
- **Encodeur** : 17-bit absolu — vis 5 mm/tr → échelle **26214.4 counts/mm**
- **Mode de contrôle** : CSV — PID position dans LinuxCNC, boucle vitesse dans le variateur

## Topologie EtherCAT complète

Vérifiée avec `ethercat slaves` le 2026-07-07 (après ajout de l'EL2088 en pos.6 →
aval décalé +1). VID Beckhoff=0x00000002, VID Lichuan=0x00000766.

| Pos | Esclave | PID | HAL préfixe | État |
|-----|---------|-----|-------------|------|
| 0 | Beckhoff EK1100 | 0x044c2c52 | *(pas de pins HAL)* | OP — Bus coupler |
| 1 | Beckhoff EL2024 #1 | 0x07e83052 | `lcec.0.1.*` | Configuré — Freins X/Y/Z |
| 2 | Beckhoff EL2024 #2 | 0x07e83052 | `lcec.0.2.*` | À configurer |
| 3 | Beckhoff EL2024 #3 | 0x07e83052 | `lcec.0.3.*` | À configurer |
| 4 | Beckhoff EL2008 #1 | 0x07d83052 | `lcec.0.4.*` | **Spare** (ancien module VFD, PNP) |
| 5 | Beckhoff EL2008 #2 | 0x07d83052 | `lcec.0.5.*` | À configurer |
| 6 | Beckhoff **EL2088** | 0x08283052 | `lcec.0.6.*` | Configuré — **VFD X1-X8 (NPN/ground)** |
| 7 | Beckhoff EL4002 | 0x0fa23052 | `lcec.0.7.*` | Configuré — AVI vitesse broche |
| 8 | Beckhoff EL1008 #1 | 0x03f03052 | `lcec.0.8.*` | Configuré — VFD Y1(Fault)/Y2(Run) |
| 9 | Beckhoff EL1008 #2 | 0x03f03052 | `lcec.0.9.*` | À configurer — Sécurités |
| 10 | Beckhoff EL1008 #3 | 0x03f03052 | `lcec.0.10.*` | À configurer — Palpeur/outil |
| 11 | Lichuan LC10E X | 0x00000402 | `lcec.0.11.*` | Configuré — Joint 0 |
| 12 | Lichuan LC10E Y | 0x00000402 | `lcec.0.12.*` | Configuré — Joint 1 |
| 13 | Lichuan LC10E Z | 0x00000402 | `lcec.0.13.*` | Configuré — Joint 2 |

Voir `docs/beckhoff_io_mapping.md` pour le détail canal par canal.

## Fichiers clés

| Fichier | Rôle |
|---------|------|
| `LC10E.ini` | Config principale (axes, période servo 1ms, display AXIS) |
| `LC10E.hal` | HAL principal : chargement composants, routage signaux |
| `pid_tuning.hal` | Gains PID auto-optimisés — ne pas éditer à la main |
| `spindle.hal` | Contrôle broche VFD : EL2088 (pos 6) + EL4002 (pos 7) + EL1008 #1 (pos 8) |
| `ethercat-conf.xml` | Topologie EtherCAT + mapping PDO pour tous les slaves actifs |
| `cia402.comp` | Composant HAL machine d'état CIA402 |
| `pyvcp_panel.xml` | Interface PyVCP gauche d'AXIS |
| `pyvcp_panel.hal` | Connexions HAL vers PyVCP (chargé en POSTGUI) |
| `docs/beckhoff_io_mapping.md` | **Mapping complet I/O Beckhoff** |
| `LC10E_parameters.csv` | Paramètres du variateur LC10E |

## Période EtherCAT / servo : 1 ms (NE PAS repasser à 500 µs)

Symptôme si trop rapide : les servos Y/Z (slaves 12, 13, fin de chaîne) s'arrêtent
en plein G-code **sans erreur LinuxCNC**, et le jog remarche après. Cause : perte de
synchro DC (flag `E` sur `ethercat slaves`, `Failed to receive DC sync check datagram`
dans dmesg sur 0-12/0-13, datagrammes `UNMATCHED`/`SKIPPED`).

Le commit `2fd9ec8` tournait à 500 µs **mais avec 2 esclaves seulement**. Avec 14
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

**Commandes** EL2088 (pos 6, `lcec.0.6.*`) → bornes X du VFD (dout-N, 0-indexé).
Module NPN/ground installé le 2026-07-07 (voir section NPN vs PNP ci-dessous).
Mapping recâblé le 2026-06-21 (paramètres VFD P91-P98) :
- dout-0 → X1 Forward      (P91) → `spindle.0.forward`
- dout-1 → X2 Reverse      (P92) → `spindle.0.reverse`
- dout-2 → X3 Stop         (P93) → bouton PyVCP
- dout-3 → X4 Jog Forward  (P94) → non connecté
- dout-4 → X5 Reset défaut (P95) → bouton PyVCP
- dout-5 → X6 External Fault (P96) → non connecté
- dout-6 → X7 First Multi Speed (P97) → **NON FONCTIONNELLE (erreur VFD)** — ne pas piloter
- dout-7 → X8 Jog Reverse  (P98) → **NON PRÉSENTE** — non connecté

⚠️ Pas de borne « Enable » dans ce câblage : Forward/Reverse sont les commandes run
(la broche tourne quand X1 ou X2 est actif). L'ancien `spindle.0.on → dout-6` a été
retiré car il déclenchait X7 (multi-speed) en erreur à chaque M3.

**Retours** VFD → EL1008 #1 (pos 8, `lcec.0.8.*`) :
- din-0 ← Y1 Fault
- din-1 ← Y2 Run

**Référence analogique** EL4002 (pos 7, `lcec.0.7.*`) :
- aout-0-value → VFD AVI (float RPM, 0-10V via scale=18000)  ← 18000 RPM = 10V = 1200 Hz
- aout-1-value → Référence fixe 100% (`setp lcec.0.7.aout-1-value 1.0`)

## ⚠️ CRITIQUE — NPN vs PNP : entrées VFD ↔ sorties Beckhoff

**Constat (vérifié sur la machine le 2026-06-21) :** les entrées numériques X1-X8 du
VFD **FC300 sont en NPN (sinking, actives à 0V)** : elles ont un pull-up interne à
+24V et s'activent quand on les **tire à 0V (COM)**. Le FC300 **n'a PAS de cavalier
NPN/PNP** (confirmé manuel p.11 + recherches web) — c'est NPN figé.

**Le problème :** l'**EL2008 est une sortie sourcing (PNP)** — elle ne sait que
*sortir +24V* ou se mettre en OFF (haute impédance). Elle ne peut **jamais tirer à 0V**.
Donc elle ne peut **pas** activer une entrée NPN. Symptôme : envoyer +24V sur X1 ne
fait rien ; relier X1↔COM (0V) à la main démarre la broche. **Aucune inversion HAL ne
corrige ça — c'est physique.**

**Solution appliquée le 2026-07-07 : EL2088 installé en pos.6 (`lcec.0.6.*`).**

Le module a été **ajouté** au rail (pas substitué à l'EL2008) → l'EL2008 pos.4 reste en
place comme spare PNP, et tous les slaves en aval (EL4002, EL1008, drives LC10E X/Y/Z)
ont **décalé de +1** : EL4002 6→7, EL1008 7→8, drives 10/11/12 → **11/12/13**. Le VFD a
été recâblé sur l'EL2088 avec le même mapping de canaux qu'avant (dout-0=Forward, etc.).

| | EL2008 (pos.4, spare) | **EL2088 (pos.6, VFD)** |
|---|---|---|
| Type | sourcing / PNP (+24V) | **ground switching / NPN (0V)** |
| Canaux / I / U | 8 / 0.5A / 24V | 8 / 0.5A / 24V |
| Pins lcec | `lcec.0.4.dout-0..7` | `lcec.0.6.dout-0..7` |

- **Support lcec** : `type="EL2088"` (natif, PID 0x08283052), déclaré dans
  `ethercat-conf.xml` pos.6. **Zéro inversion HAL** (logique `dout-N ON = fonction
  active` conservée — l'EL2088 inverse l'électrique : ON = tire à 0V = active l'entrée NPN).
- ⚠️ **Câblage obligatoire** : `dout-N` → X_n du VFD, et **0V de l'EL2088 relié au COM
  du VFD** (sinon le retour de courant NPN n'a pas de chemin → rien ne s'active).

**Règle générale — quel module pour quoi :**
- **Sorties vers entrées NPN d'un VFD/appareil** (FC300 X1-X8) → **EL2088** (ground/NPN).
- **Sorties vers actionneurs qui veulent +24V** (freins EL2024, relais) → **sourcing/PNP** OK.
- **Analogique** (EL4002 → AVI) : indifférent au NPN/PNP, besoin seulement d'une **masse
  commune** (0V EL4002 ↔ COM VFD). C'est pour ça que l'AVI marchait déjà.
- **Entrées lisant les retours VFD** (EL1008 ← Y1/Y2) : Y1/Y2 sont des **contacts secs
  relais** du VFD → câbler +24V à travers le contact vers l'entrée EL1008 (polarité libre).

## Sécurités prévues (EL1008 #1, #2)

- E-stop (câblage NC : HIGH=OK, LOW=déclenché)
- Palpeur pièce
- Capteur outil dans broche / longueur outil
- Pressostat air
- Fins de course : **sur les variateurs LC10E** (pas sur Beckhoff) — ✅ implémenté sur X,
  voir section ci-dessous.

## Fins de course — capteurs de proximité sur les drives LC10E

Capteurs de proximité **PNP 3 fils** (brun=+24V, bleu=0V, noir=signal), un à chaque
extrémité de course, **câblés sur le connecteur de contrôle CN2 du drive** concerné.
Implémenté et validé sur X (slave idx 11) le 2026-06-21 ; recette identique pour Y/Z.

### Câblage CN2 (par drive)

| Élément | Borne CN2 |
|---------|-----------|
| Capteur extrémité **+X**, fil noir | **DI0 = broche 5** |
| Capteur extrémité **−X**, fil noir | **DI1 = broche 4** |
| Bruns (les 2) → **+24V** | broche 7 ou 9 |
| Bleus (les 2) → **0V** | broche 8 ou 10 |
| **Cavalier DICOM (broche 6) → 0V** | broche 8/10 (obligatoire pour PNP) |

### Paramètres drive — À RÉGLER AU PANNEAU (DI0..DI4 ↔ P03.02..P03.10)

⚠️ **Régler ces valeurs AU PANNEAU du drive**, pas par SDO : les écritures SDO ne
persistent **pas** en EEPROM (vérifié — P03.03 écrit par SDO est revenu à 0 après
power-cycle). Au panneau, ça persiste.

- Fonctions (déjà aux valeurs d'usine de ce modèle) :
  `P03.02=14` (DI0→P-OT, forward), `P03.04=15` (DI1→N-OT, reverse), `P03.06=31` (DI2→origine).
- **Logique = 1 (actif HAUT) — LE point clé : `P03.03=1`, `P03.05=1`, `P03.07=1`.**
  Nos capteurs se comportent en **NO** (sortie +24V quand le métal/la butée est détecté).
  Actif haut ⇒ l'overtravel se déclenche **uniquement au contact**, pas au repos.
  (Au départ on croyait NC/actif-bas=0 ; faux. Avec logique 0, l'erreur Er.950/952
  apparaissait au repos et disparaissait au métal — comportement inversé.)
- `P02.07=1` : overtravel = arrêt vitesse nulle + verrouillage position.
- Er.950 (P-OT) / Er.952 (N-OT) sont des **warnings réinitialisables**, pas des défauts durs.
- ⚠️ Fail-safe : NO + actif haut ⇒ un **fil coupé = limite NON détectée**. Pour le
  fail-safe (fil coupé→arrêt), il faudrait des capteurs **NC + logique 0**.

### Lecture dans LinuxCNC (par drive)

État des entrées remonté via l'objet CIA402 **`0x60FD`** (déjà dans le TxPDO d'usine).
Bits : `bit0`=N-OT, `bit1`=P-OT, `bit2`=origine ; `bits20..24`=DI0..DI4 bruts.

1. `ethercat-conf.xml` — dans le TxPDO `<pdo idx="1a00">` du slave, ajouter une
   `pdoEntry idx="60FD" halType="complex"` avec des `complexEntry` (cf. le bloc du
   slave idx 11). Crée les pins `lcec.0.<idx>.din-{not,pot,home,di0,di1}`.
2. `LC10E.hal` — relier les bits de fonction aux limites du joint :
   ```
   net <axe>-pos-lim lcec.0.<idx>.din-pot => joint.<n>.pos-lim-sw-in
   net <axe>-neg-lim lcec.0.<idx>.din-not => joint.<n>.neg-lim-sw-in
   ```
   (X : idx 11 / joint 0 — fait. Y : idx 12 / joint 1. Z : idx 13 / joint 2.)
3. Redémarrer LinuxCNC. Pas d'inversion HAL nécessaire : avec logique drive=1,
   `din-pot/din-not` valent 1 **uniquement** quand la butée est touchée.

## Homing — encodeur absolu (axe X)

L'encodeur LC10E est **absolu multi-tours + batterie** (P02-01=1) : la position est
conservée à l'extinction. On exploite ça pour ne **plus refaire le homing à chaque
démarrage**. Implémenté sur X le 2026-06-21 (mode CSV + `cia402.comp`).

- `LC10E.ini [JOINT_0]` : `HOME_ABSOLUTE_ENCODER = 1`, `HOME_SEARCH_VEL = 0`,
  `HOME_LATCH_VEL = 0`, `HOME_OFFSET = <calibré>`. → Au démarrage, LinuxCNC se réfère
  directement à la position absolue du drive, sans mouvement. **Home All devient
  instantané et ne re-zérote PAS** (il restaure la position calibrée).
- `LC10E.hal` : pour le mode absolu quotidien, l'ancien câblage homing assisté drive de X
  est **désactivé** (commenté) — `joint.0.home-sw-in`/`index-enable` non reliés à
  `cia402.0.stat-homed/home`. Il est **réactivé uniquement en config calibration** (cf.
  `calib_homing.hal`).
- Coordonnée machine : `HOME_OFFSET` (mm) dans l'INI pour un zéro non situé au repère ;
  le drive garde `607Ch=0` (le repère = origine drive).

### Calibration de l'origine — homing interne du drive (rare, ou après mort batterie)

**Méthode retenue (machine ON).** `HOME_ABSOLUTE_ENCODER` (quotidien) et `HOME_USE_INDEX`
(homing piloté drive) étant exclusifs sur un joint, le `HOME_USE_INDEX` est isolé dans une
**config dédiée** :

- `LC10E-calib.ini` : copie de `LC10E.ini` où `[JOINT_0]` passe en `HOME_USE_INDEX=TRUE`
  (+ `HOME_LATCH_VEL≠0`, sans `HOME_ABSOLUTE_ENCODER`) et qui charge `calib_homing.hal`.
- `calib_homing.hal` : réactive `net x-home-index joint.0.index-enable <=> cia402.0.home`.
- Bouton **« Home X »** du panneau PyVCP → `halui.joint.0.home` ; LED jaune = homing en
  cours (`cia402.0.stat-homing`).
- Drive AU PANNEAU (persiste) : `6098h=1` (réf. fin de course − + Z), `60E6h=0`, `607Ch=0`,
  `6099h:01/:02` vitesses lentes, `609Ah` accel, `2005h:24` timeout.

Procédure : lancer `linuxcnc LC10E-calib.ini`, F2, **« Home X »** → le drive va tamponner
la fin de course − + Z et fige l'origine (stockée dans l'encodeur absolu). Quitter,
relancer `LC10E.ini` (normal) → l'absolu adopte la nouvelle origine. **Ne pas faire
« Home All »** (Y/Z pas équipés).

⚠️ **À valider sur machine** (d'abord à vide, vitesses lentes) : opmode 6 en mouvement
(bug CSP) ; conflit possible entre N-OT (overtravel sur DI1) et l'usage de la même fin de
course comme repère de homing ; sens du homing. `/tmp/linuxcnc.report` si démarrage échoue.

**Repli côté LinuxCNC** : `scripts/calibrate_home.py` (halcmd, stdlib, X/Y/Z) — jog manuel
sur le repère puis calcule/écrit `HOME_OFFSET` dans l'INI. Ou méthode drive `6098=35`
(position courante, **sans mouvement**).

⚠️ Vaut pour X uniquement ; Y/Z restent en home immédiat tant qu'ils ne sont pas équipés
(capteurs −limit + params panneau). `HOME_ABSOLUTE_ENCODER`/`HOME_USE_INDEX` = features
LinuxCNC 2.9.
