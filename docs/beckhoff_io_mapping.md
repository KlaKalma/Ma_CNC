# Mapping I/O Beckhoff — CNC Ma_CNC

## Topologie EtherCAT

```
[PC EtherCAT Master]
        │
        ├─► [Pos 0]  LC10E Servo Drive X        CIA402, encodeur 17-bit
        ├─► [Pos 1]  LC10E Servo Drive Y        CIA402, encodeur 17-bit
        ├─► [Pos 2]  LC10E Servo Drive Z        CIA402, encodeur 17-bit
        │
        └─► [Pos 3]  EK1100 Bus Coupler ════════ début du bloc E-bus ═══════
                         │
                         ├─ [Pos  4]  EL2024 #1  — 4×DO 24V 2A   — Freins X/Y/Z + spare
                         ├─ [Pos  5]  EL2024 #2  — 4×DO 24V 2A   — À définir
                         ├─ [Pos  6]  EL2024 #3  — 4×DO 24V 2A   — Vannes / relais
                         ├─ [Pos  7]  EL2008 #1  — 8×DO 24V 0.5A — Commandes VFD
                         ├─ [Pos  8]  EL2008 #2  — 8×DO 24V 0.5A — Sorties diverses
                         ├─ [Pos  9]  EL4002      — 2×AO ±10V     — Vitesse broche
                         ├─ [Pos 10]  EL1008 #1  — 8×DI 24V       — Sécurités
                         ├─ [Pos 11]  EL1008 #2  — 8×DI 24V       — Palpeur / outil
                         └─ [Pos 12]  EL1008 #3  — 8×DI 24V       — Retours VFD
```

---

## EL2024 #1 — Position 4 — `lcec.0.4.*`

**Module** : Beckhoff EL2024 — 4 sorties digitales 24VDC, **2A** par canal  
**Usage** : Freins axes X, Y, Z — tous sur ce module  
**État** : Configuré dans `ethercat-conf.xml`

| Canal | Borne + | Borne - | PDO Index | HAL Pin | Net HAL | Fonction | État |
|-------|---------|---------|-----------|---------|---------|----------|------|
| 1 | 1 | 5 | 0x7000:01 | `lcec.0.4.output-1` | `x-enable` | Frein / Enable axe X | Câblé |
| 2 | 2 | 6 | 0x7010:01 | `lcec.0.4.output-2` | `y-enable` | Frein / Enable axe Y | Câblé |
| 3 | 3 | 7 | 0x7020:01 | `lcec.0.4.output-3` | `z-enable` | Frein / Enable axe Z | Câblé |
| 4 | 4 | 8 | 0x7030:01 | `lcec.0.4.output-4` | *(spare)*  | Réservé | À définir |

> **Logique** : HIGH (24V) = frein desserré / variateur activé · LOW = frein serré / arrêt

---

## EL2024 #2 — Position 5 — `lcec.0.5.*`

**Module** : Beckhoff EL2024 — 4 sorties digitales 24VDC, **2A** par canal  
**Usage prévu** : À définir (ex: enables auxiliaires, vannes haute puissance)  
**État** : À configurer

| Canal | Borne + | Borne - | PDO Index | HAL Pin | Fonction prévue | État |
|-------|---------|---------|-----------|---------|-----------------|------|
| 1 | 1 | 5 | 0x7000:01 | `lcec.0.5.output-1` | À définir | — |
| 2 | 2 | 6 | 0x7010:01 | `lcec.0.5.output-2` | À définir | — |
| 3 | 3 | 7 | 0x7020:01 | `lcec.0.5.output-3` | À définir | — |
| 4 | 4 | 8 | 0x7030:01 | `lcec.0.5.output-4` | À définir | — |

---

## EL2024 #3 — Position 4 — `lcec.0.4.*`

**Module** : Beckhoff EL2024 — 4 sorties digitales 24VDC, **2A** par canal  
**Usage prévu** : Vannes solénoïdes + relais de puissance  
**État** : À configurer

| Canal | Borne + | Borne - | PDO Index | HAL Pin | Fonction prévue | État |
|-------|---------|---------|-----------|---------|-----------------|------|
| 1 | 1 | 5 | 0x7000:01 | `lcec.0.4.output-1` | Vanne air soufflante | À définir |
| 2 | 2 | 6 | 0x7010:01 | `lcec.0.4.output-2` | Vanne refroidissement | À définir |
| 3 | 3 | 7 | 0x7020:01 | `lcec.0.4.output-3` | Relais puissance 1 | À définir |
| 4 | 4 | 8 | 0x7030:01 | `lcec.0.4.output-4` | Relais puissance 2 | À définir |

---

## EL2008 #1 — Position 4 — `lcec.0.4.*` → Commandes VFD broche

**Module** : Beckhoff EL2008 — 8 sorties digitales 24VDC, 0.5A par canal  
**Type natif** : `dout-N` (0-indexé) → borne X(N+1) du VFD  
**Mapping recâblé le 2026-06-21** (paramètres VFD P91-P98)  
**État** : Configuré — ⚠️ **module à remplacer par EL2088 (voir note NPN/PNP ci-dessous)**

> ### ⚠️ NPN/PNP — EL2008 incompatible avec le VFD FC300
> Les entrées X1-X8 du **FC300 sont NPN (actives à 0V)** : pull-up interne à +24V,
> s'activent en reliant X_n↔COM (0V). Le FC300 **n'a pas de cavalier NPN/PNP**.
> L'**EL2008 est sourcing/PNP** (sort +24V uniquement) → il ne peut **pas tirer à 0V**
> → il **n'active jamais** ces entrées. *Vérifié sur machine 2026-06-21 : +24V sur X1 =
> rien ; X1↔COM = la broche démarre.* Aucun correctif logiciel possible (physique).
>
> **Solution : remplacer l'EL2008 (pos 4) par un EL2088** = version *ground switching
> (NPN)* de l'EL2008 (8ch / 0.5A / 24V, mêmes pins `dout-0..7`). Supporté par lcec
> (`type="EL2088"`, PID `0x08283052`). **Drop-in** : seul changement
> `type="EL2008"`→`"EL2088"` dans `ethercat-conf.xml`, **zéro changement HAL** (logique
> `dout-N ON = actif` conservée). Câbler le **0V de l'EL2088 au COM du VFD**.
> Stopgap sans EL2088 : carte relais (dout-N → bobine → contact NO entre X_n et COM).

| dout | Borne EL | HAL Pin | Borne VFD | Param | Fonction VFD | Connexion HAL |
|------|----------|---------|-----------|-------|--------------|---------------|
| 0 | 1 | `lcec.0.4.dout-0` | **X1** | P91 | Forward (FWD) | `spindle.0.forward` |
| 1 | 2 | `lcec.0.4.dout-1` | **X2** | P92 | Reverse (REV) | `spindle.0.reverse` |
| 2 | 3 | `lcec.0.4.dout-2` | **X3** | P93 | Stop | bouton PyVCP |
| 3 | 4 | `lcec.0.4.dout-3` | **X4** | P94 | Jog Forward | non connecté |
| 4 | 5 | `lcec.0.4.dout-4` | **X5** | P95 | Reset défaut | bouton PyVCP |
| 5 | 6 | `lcec.0.4.dout-5` | **X6** | P96 | External Fault | non connecté |
| 6 | 7 | `lcec.0.4.dout-6` | **X7** | P97 | First Multi Speed — **NON FONCT.** | non connecté |
| 7 | 8 | `lcec.0.4.dout-7` | **X8** | P98 | Jog Reverse — **NON PRÉSENTE** | non connecté |

⚠️ Aucune borne « Enable » : Forward/Reverse = commandes run. L'ancien
`spindle.0.on → dout-6` a été retiré (déclenchait X7 multi-speed en erreur).

**Connexions HAL réelles** (dans `spindle.hal` + `pyvcp_panel.hal`) :
```
net spindle-fwd       spindle.0.forward         => lcec.0.4.dout-0
net spindle-rev       spindle.0.reverse         => lcec.0.4.dout-1
net spindle-stop-btn  pyvcp.spindle-stop-btn    => lcec.0.4.dout-2
net spindle-reset-btn pyvcp.spindle-fault-reset => lcec.0.4.dout-4
```

<details>
<summary>XML dans ethercat-conf.xml</summary>

```xml
<slave idx="4" type="EL2008"/>
```
</details>

---

## EL2008 #2 — Position 6 — `lcec.0.6.*`

**Module** : Beckhoff EL2008 — 8 sorties digitales 24VDC, 0.5A par canal  
**Usage prévu** : Sorties auxiliaires machine  
**État** : À définir et configurer

| Canal | Borne | PDO Index | HAL Pin | Fonction prévue | État |
|-------|-------|-----------|---------|-----------------|------|
| 1 | 1 | 0x7000:01 | `lcec.0.6.output-1` | Éclairage machine | À définir |
| 2 | 2 | 0x7010:01 | `lcec.0.6.output-2` | Signal sonore / alarme | À définir |
| 3 | 3 | 0x7020:01 | `lcec.0.6.output-3` | Pompe arrosage | À définir |
| 4 | 4 | 0x7030:01 | `lcec.0.6.output-4` | Réservé | — |
| 5 | 5 | 0x7040:01 | `lcec.0.6.output-5` | Réservé | — |
| 6 | 6 | 0x7050:01 | `lcec.0.6.output-6` | Réservé | — |
| 7 | 7 | 0x7060:01 | `lcec.0.6.output-7` | Réservé | — |
| 8 | 8 | 0x7070:01 | `lcec.0.6.output-8` | Réservé | — |

---

## EL4002 — Position 7 — `lcec.0.7.*` → Vitesse broche (AVI)

**Module** : Beckhoff EL4002 — 2 sorties analogiques ±10V, 12-bit  
**Câblage direct** : canal 1 → borne AVI du VFD (consigne vitesse)  
**État** : Câblé, à configurer dans `ethercat-conf.xml`

| Canal | Bornes | PDO Index | HAL Pin | Destination VFD | Plage | Fonction | État |
|-------|--------|-----------|---------|-----------------|-------|----------|------|
| 1 | 1 (+), 2 (0V) | 0x3001:01 | `lcec.0.7.channel-1` | **AVI** | 0–+32767 = 0–10V | Consigne vitesse broche | Câblé |
| 2 | 3 (+), 4 (0V) | 0x3002:01 | `lcec.0.7.channel-2` | Référence fixe | fixé à 32767 | 100% référence (obligatoire) | Câblé |

> **Important** : Le canal 2 doit être maintenu à 32767 en permanence.  
> Ajouter dans le HAL : `setp lcec.0.7.channel-2 32767`

**Conversion vitesse** : `valeur = (RPM / RPM_MAX) × 32767`  
Exemple pour 6000 RPM max : `gain = 32767 / 6000 = 5.46`

**Connexions HAL suggérées** :
```
# Composant scale pour conversion RPM → valeur analogique
loadrt scale names=scale.spindle-ao
setp scale.spindle-ao.gain 5.46      # à ajuster selon RPM max VFD
net spindle-rpm-abs spindle.0.speed-out-abs => scale.spindle-ao.in
net spindle-ao      scale.spindle-ao.out    => lcec.0.7.channel-1
setp lcec.0.7.channel-2 32767              # référence fixe 100%
```

<details>
<summary>XML à ajouter dans ethercat-conf.xml</summary>

```xml
<slave idx="7" type="el4002" vid="00000002" pid="0FA23052"/>
```
</details>

---

## EL1008 #1 — Position 8 — `lcec.0.8.*` → Sécurités

**Module** : Beckhoff EL1008 — 8 entrées digitales 24VDC  
**Usage prévu** : Circuit E-stop, protections machine  
**État** : À câbler et configurer

| Canal | Borne | PDO Index | HAL Pin | Fonction prévue | Logique | État |
|-------|-------|-----------|---------|-----------------|---------|------|
| 1 | 1 | 0x6000:01 | `lcec.0.8.input-1` | E-STOP bouton cabine | NC : HIGH=OK | À câbler |
| 2 | 2 | 0x6010:01 | `lcec.0.8.input-2` | E-STOP porte machine | NC : HIGH=OK | À câbler |
| 3 | 3 | 0x6020:01 | `lcec.0.8.input-3` | Pressostat air comprimé | NO : HIGH=pression OK | À câbler |
| 4 | 4 | 0x6030:01 | `lcec.0.8.input-4` | Niveau refroidissement | À définir | À définir |
| 5 | 5 | 0x6040:01 | `lcec.0.8.input-5` | Porte/capot ouvert | NC : HIGH=fermé | À définir |
| 6 | 6 | 0x6050:01 | `lcec.0.8.input-6` | Réservé sécurité | — | — |
| 7 | 7 | 0x6060:01 | `lcec.0.8.input-7` | Réservé sécurité | — | — |
| 8 | 8 | 0x6070:01 | `lcec.0.8.input-8` | Réservé sécurité | — | — |

> **Convention NC (normalement fermé)** : câbler en série, circuit ouvert = E-stop déclenché.  
> LinuxCNC : `net estop-ext lcec.0.8.input-1 => iocontrol.0.emc-enable-in`

<details>
<summary>XML à ajouter dans ethercat-conf.xml</summary>

```xml
<slave idx="8" type="el1008" vid="00000002" pid="03F03052"/>
```
</details>

---

## EL1008 #2 — Position 9 — `lcec.0.9.*` → Palpeur / Outil

**Module** : Beckhoff EL1008 — 8 entrées digitales 24VDC  
**Usage prévu** : Palpeur pièce, capteurs outil  
**État** : À câbler et configurer

| Canal | Borne | PDO Index | HAL Pin | Fonction prévue | État |
|-------|-------|-----------|---------|-----------------|------|
| 1 | 1 | 0x6000:01 | `lcec.0.9.input-1` | Palpeur pièce | À câbler |
| 2 | 2 | 0x6010:01 | `lcec.0.9.input-2` | Capteur outil dans broche | À câbler |
| 3 | 3 | 0x6020:01 | `lcec.0.9.input-3` | Capteur longueur outil | À câbler |
| 4 | 4 | 0x6030:01 | `lcec.0.9.input-4` | Réservé outil | — |
| 5 | 5 | 0x6040:01 | `lcec.0.9.input-5` | Réservé | — |
| 6 | 6 | 0x6050:01 | `lcec.0.9.input-6` | Réservé | — |
| 7 | 7 | 0x6060:01 | `lcec.0.9.input-7` | Réservé | — |
| 8 | 8 | 0x6070:01 | `lcec.0.9.input-8` | Réservé | — |

**Connexions HAL palpeur** :
```
net probe-in lcec.0.9.input-1 => motion.probe-input
```

---

## EL1008 #3 — Position 10 — `lcec.0.10.*` → Retours VFD

**Module** : Beckhoff EL1008 — 8 entrées digitales 24VDC  
**Câblage direct** : borne YN du VFD → input-N du module (correspondance 1:1)  
**État** : Câblé sur Y1/Y2, à configurer dans `ethercat-conf.xml`

| Canal | Borne EL | PDO Index | HAL Pin | Borne VFD | Retour VFD | État |
|-------|----------|-----------|---------|-----------|------------|------|
| 1 | 1 | 0x6000:01 | `lcec.0.10.input-1` | **Y1** | Fault | Câblé |
| 2 | 2 | 0x6010:01 | `lcec.0.10.input-2` | **Y2** | Run (broche en marche) | Câblé |
| 3 | 3 | 0x6020:01 | `lcec.0.10.input-3` | Y3 (si dispo) | At-Speed / Ready | À définir |
| 4 | 4 | 0x6030:01 | `lcec.0.10.input-4` | — | Réservé VFD | — |
| 5–8 | 5–8 | — | `lcec.0.10.input-[5-8]` | — | Réservés | — |

**Connexions HAL suggérées** :
```
net spindle-fault   lcec.0.10.input-1 => pyvcp.spindle-fault
net spindle-at-speed lcec.0.10.input-2 => spindle.0.at-speed
```

<details>
<summary>XML à ajouter dans ethercat-conf.xml</summary>

```xml
<slave idx="10" type="el1008" vid="00000002" pid="03F03052"/>
```
</details>

---

## Récapitulatif global des I/O

| Module | Pos | Canaux | Utilisés | Câblés (non cfg) | À définir |
|--------|-----|--------|---------|-------------------|-----------|
| EL2024 #1 | 2 | 4 DO 2A | 4 (frein X) | — | — |
| EL2024 #2 | 3 | 4 DO 2A | — | — | 4 (Y+Z) |
| EL2024 #3 | 4 | 4 DO 2A | — | — | 4 (vannes) |
| EL2008 #1 | 5 | 8 DO 0.5A | — | 8 (VFD X1–X8) | — |
| EL2008 #2 | 6 | 8 DO 0.5A | — | — | 8 |
| EL4002 | 7 | 2 AO | — | 2 (AVI + ref) | — |
| EL1008 #1 | 8 | 8 DI | — | — | 8 (sécurités) |
| EL1008 #2 | 9 | 8 DI | — | — | 8 (outil) |
| EL1008 #3 | 10 | 8 DI | — | 2 (VFD Y1+Y2) | 6 |
| **TOTAL** | | **54 I/O** | **4** | **12** | **38** |

---

## Convention logique

| Type | Niveau actif | Signification |
|------|-------------|---------------|
| Sorties axes (EL2024) | HIGH 24V | Frein desserré / variateur enable |
| Commandes VFD (EL2008) | HIGH 24V | Commande active |
| E-stop (EL1008) | HIGH 24V | Circuit OK (câblage NC) |
| Retours VFD (EL1008) | HIGH 24V | État actif (Fault=1 = défaut présent) |
