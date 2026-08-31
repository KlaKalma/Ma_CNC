# Pendant USB — Ma_CNC

Boîtier de commande manuelle (MPG + boutons + potentiomètre) relié au Raspberry Pi
par **USB natif**, exposé dans HAL par un composant userspace.

## Choix de la carte

| | Pro Micro (ATmega32U4) | **UNO R4 Minima (RA4M1)** ← recommandée |
|---|---|---|
| USB natif device | oui (CDC + HID) | oui (CDC + HID) |
| Logique | 5 V (clones 3.3 V/8 MHz fréquents) | 5 V |
| CPU | 8 bits, 16 MHz | 32 bits, 48 MHz |
| ADC | 10 bits | 14 bits |
| E/S | ~18 (9 analog) | 20 (6 analog) |
| Connecteur | micro-USB | USB-C |
| Bootloader | double-reset, brickable | robuste |

**L'OTG n'entre pas en jeu** : OTG = être *hôte* USB. Ici la carte est un
*périphérique* USB.

### Carte installée : UNO R4 **WiFi** — nuance sur l'« USB natif »

La carte branchée est une **R4 WiFi** (`2341:1002`, `/dev/ttyACM0`), pas une
Minima. Différence réelle, vérifiée dans `boards.txt` du core Renesas 1.6.0 :

```
minima.build.defines   = -DF_CPU=48000000 -DARDUINO_UNOR4_MINIMA
unor4wifi.build.defines= -DF_CPU=48000000 -DNO_USB -DBACKTRACE_SUPPORT ...
                                          ^^^^^^^
```

Sur la **Minima**, l'USB-C va directement au contrôleur USB du RA4M1 : USB
natif au sens strict. Sur la **WiFi**, l'USB du RA4M1 est désactivé (`NO_USB`) ;
l'USB-C est géré par l'**ESP32-S3**, qui fait pont USB↔UART vers le RA4M1 (il
énumère d'ailleurs comme `UNO WiFi R4 CMSIS-DAP`). `Serial` est donc une UART
interne (broches 22/23, hors connecteurs), pas un CDC natif.

Conséquences concrètes :

- **Le débit compte vraiment** ici, contrairement à la Minima. 115200 bauds est
  largement suffisant (une trame ≈ 20 octets, au pire 200 trames/s ≈ 4 ko/s).
- Une latence supplémentaire de pont, de l'ordre de la fraction de milliseconde.
  Sans conséquence : la chaîne HAL userspace est de toute façon non temps réel,
  et le jog n'a aucune exigence de sécurité (E-stop sur EtherCAT).
- **Pas d'émulation HID** (clavier/souris) possible depuis le sketch sur la
  WiFi ; c'est réservé à la Minima. On n'en a pas besoin : on utilise le CDC.
- `D0`/`D1` restent **libres** (ce sont `Serial1`/UART2, pas le port console).

Bref : la R4 WiFi convient parfaitement à cet usage. La formulation « USB natif »
n'est exacte que pour la Minima.

Le besoin total est de **11 entrées digitales + 1 analogique** :

| Organe | Sorties |
|---|---|
| Encodeur MPG (A, B) | 2 |
| Sélecteur d'axe X/Y/Z (X et Z pilotent chacun une ligne, Y = aucune) | 2 |
| Inverseur ON/OFF n°1 (2 contacts) | 2 |
| Inverseur ON/OFF n°2 (2 contacts) | 2 |
| 3 poussoirs simples | 3 |
| Potentiomètre | 1 analogique |

Le Pro Micro passe tout juste, le R4 laisse de la marge. `pendant.ino` compile
sur les deux : A/B sont sur D2/D3, seuls pins à interruption garantis des deux
côtés.

## Sécurité

**L'USB n'est pas temps réel.** Rien dans cette chaîne ne doit porter de fonction
de sécurité. L'E-stop reste câblé sur le bus EtherCAT, comme actuellement.

La pin `pendant.online` est un watchdog : elle retombe si aucune trame n'arrive
pendant 0,5 s (câble arraché, carte plantée) et coupe alors `axis.x.jog-enable`.
Le firmware envoie un battement de cœur toutes les 200 ms pour l'alimenter.

## Câblage de l'encodeur

Les 4 fils du MPG :

| Fil | Vers |
|---|---|
| + | 5 V |
| − | GND |
| A | D2 |
| B | D3 |

Les entrées sont en `INPUT_PULLUP`, donc un MPG à collecteur ouvert fonctionne
sans résistance externe. Si le sens de rotation est inversé, mettre
`ENC_INVERT = true` dans `pendant.ino` — pas besoin de recâbler.

`ENC_DIV = 4` : décodage x4 en interne divisé par 4, soit **1 count par cran**
mécanique (MPG 100 imp/tour → 100 counts/tour). Si la molette compte double ou
moitié, ajuster à 2 ou 1.

## Pull-ups internes : oui — mais aucun pull-down

Vérifié dans le source du core (`cores/arduino/digital.cpp`) :

```c
case INPUT_PULLDOWN: // TODO: document the INPUT_PULLDOWN is unavailable
    R_IOPORT_PinCfg(..., IOPORT_CFG_PORT_DIRECTION_INPUT);      // entree nue !
case INPUT_PULLUP:
    R_IOPORT_PinCfg(..., ... | IOPORT_CFG_PULLUP_ENABLE);
```

Le RA4M1 a un pull-up interne sur **toutes** les broches, donc **aucune
résistance externe n'est nécessaire**. En revanche il n'a **aucun pull-down**,
et `INPUT_PULLDOWN` est accepté silencieusement en laissant l'entrée flottante.
→ **Tous les boutons se câblent entre la broche et GND** (actif bas).
Ne jamais câbler un bouton vers +5 V en comptant sur un pull-down.

Affectation des broches sur la R4 WiFi (20 digitales, 6 analogiques) :

| Broches | Usage |
|---|---|
| D2, D3 | encodeur A / B (interruptions) |
| **D4 → D12** | les 9 boutons — le compte tombe juste |
| A0 | potentiomètre |
| D0, D1, A1, A2, A3 | réserve (D0/D1 = `Serial1`, libres : la console passe par les broches 22/23) |
| D13 | éviter — `LED_BUILTIN`, charge sur la broche |
| A4, A5 | éviter — SDA/SCL, pull-ups externes sur la carte |

Le pull-up interne est **faible (~33 kΩ typique)**. En armoire, à côté du VFD,
sur un câble de pendant de plus d'1 m, il peut capter du bruit. Parade —
**différente selon le signal** :

- **Boutons** : 100 nF de l'entrée vers GND, au plus près du connecteur. Avec
  33 kΩ ça filtre à ~3 ms, invisible face à l'anti-rebond logiciel de 20 ms.
- **Encodeur** : surtout **pas** de condensateur, il détruirait les fronts.
  Paire torsadée blindée ; si le MPG est à collecteur ouvert, ajouter des
  pull-ups externes de 1 k–4,7 kΩ sur A et B pour raidir les fronts et
  augmenter l'immunité.

Une entrée d'encodeur laissée flottante génère des interruptions en continu
(50 Hz capté + rebonds), ce qui peut affamer `loop()` et rendre la carte
muette. Si la carte n'émet plus rien, vérifier d'abord le câblage de D2/D3 et
la **masse commune** entre le MPG et la carte.

## Installation

### Chaîne de compilation (arduino-cli)

Installée en `~/.local/bin/arduino-cli` (déjà dans le `PATH` via `~/.profile`),
version 1.5.1, core `arduino:renesas_uno` 1.6.0.

```bash
cd /home/cnc/linuxcnc/configs/Ma_CNC
BUILD=/tmp/build-pendant
arduino-cli compile --fqbn arduino:renesas_uno:unor4wifi --build-path $BUILD pendant
arduino-cli upload  --fqbn arduino:renesas_uno:unor4wifi --input-dir  $BUILD \
                    -p /dev/ttyACM0 pendant
```

**Piège vérifié** : le cache de build d'arduino-cli est indexé sur le *chemin
du sketch*, pas sur le FQBN. Compiler successivement pour `minima` puis
uploader en `unor4wifi` flashe le binaire Minima sans aucun avertissement — et
la carte reste muette (le binaire Minima active l'USB natif du RA4M1, qui n'est
pas connecté sur la WiFi). **Toujours passer `--build-path` / `--input-dir`
explicites.**

Si l'upload répond `No device found on ttyACM0`, le reset logiciel à 1200 bauds
n'a pas pris : appuyer **deux fois rapidement sur le bouton RESET** de la carte
pour forcer le bootloader, puis relancer l'upload.

### Étapes

```bash
# 1. flasher pendant.ino (voir ci-dessus, ou Arduino IDE)

# 2. nom de périphérique stable
sudo cp pendant/99-pendant.rules /etc/udev/rules.d/
sudo udevadm control --reload-rules && sudo udevadm trigger
ls -l /dev/pendant          # doit pointer sur ttyACM*

# 3. rendre le composant exécutable
chmod +x pendant/pendant-hal.py
```

Puis dans `LC10E.ini`, section `[HAL]`, **après** `HALFILE = LC10E.hal` :

```ini
HALFILE = pendant/pendant.hal
```

### Test hors LinuxCNC

```bash
python3 -m serial.tools.miniterm /dev/pendant 115200
```

Tourner la molette : des lignes `S <count> <btn> <pot>` défilent. `?` force une
trame, `Z` remet le compteur à zéro, `V` affiche la bannière.

### Diagnostic dans HAL

```
halcmd show pin pendant
watch pendant.counts pendant.online pendant.rx-count
```

`rx-count` doit augmenter en permanence (battement de cœur) même molette à
l'arrêt. S'il stagne : liaison morte.

## Protocole

Trames ASCII, une par ligne.

| Sens | Trame | Signification |
|---|---|---|
| → hôte | `# pendant v1 div=4 buttons=0 pot=0` | bannière (boot ou `V`) |
| → hôte | `S <count> <btn_hex> <pot>` | état complet |
| ← hôte | `?` | renvoyer une trame `S` |
| ← hôte | `Z` | remettre le compteur à zéro |
| ← hôte | `V` | renvoyer la bannière |

`count` est un compteur **absolu** signé depuis le boot de la carte. Le
composant HAL en extrait des **deltas** et maintient son propre accumulateur :
un reboot de l'Arduino ou un rebranchement ne provoque donc jamais de saut de
jog.

Émission sur changement (limitée à une trame / 5 ms) + battement de cœur toutes
les 200 ms.

---

## Suite : boutons et potentiomètre

Le squelette est déjà en place dans `pendant.ino` et le protocole prévoit déjà
les champs `btn` et `pot` — activer ces fonctions **ne changera pas le format
des trames**.

### 1. Firmware

Passer `ENABLE_BUTTONS` et `ENABLE_POT` à `1` dans `pendant.ino`, puis ajuster
la table `BUTTONS[]` aux broches réellement câblées. Chaque bouton se câble
entre sa broche et GND (`INPUT_PULLUP`, actif bas), anti-rebond logiciel 20 ms.

Affectation des bits proposée — **c'est le contrat avec le composant HAL**, ne
la changer que des deux côtés à la fois :

| bit | signal | rôle |
|---|---|---|
| 0 | `sel-x` | sélecteur d'axe, position X |
| 1 | `sel-z` | sélecteur d'axe, position Z |
| 2 | `sw1-on` | inverseur 1, contact ON |
| 3 | `sw1-off` | inverseur 1, contact OFF |
| 4 | `sw2-on` | inverseur 2, contact ON |
| 5 | `sw2-off` | inverseur 2, contact OFF |
| 6 | `btn-0` | poussoir 1 |
| 7 | `btn-1` | poussoir 2 |
| 8 | `btn-2` | poussoir 3 |

**Sélecteur d'axe** : Y se déduit — `sel-y = NOT sel-x AND NOT sel-z`. Si les
deux bits sont vrais en même temps, c'est un défaut de câblage : le composant
HAL devra alors mettre les trois `jog-enable` à faux plutôt que de choisir.

**Inverseurs 2 contacts** : les deux bits vrais ou les deux faux simultanément
signalent un contact usé ou un passage en cours. Ne valider l'état que si
exactement un bit est vrai — c'est l'intérêt d'avoir deux contacts.

Le potentiomètre se câble sur `A0` (curseur), 5 V et GND aux extrémités. La
valeur est envoyée en 0..1023 sur les deux cartes ; une bande morte de 6 LSB
évite le tremblement. Sur R4 on pourrait passer en 14 bits (`analogReadResolution(14)`)
si plus de finesse est utile — il faudra alors adapter l'échelle côté HAL.

### 2. Composant HAL

Dans `pendant-hal.py`, `handle_line()` décode déjà `btn` et `pot` (valeurs
actuellement jetées). Il reste à :

1. créer les pins dans `__init__` :
   ```python
   for name in ("sel-x", "sel-z", "sw1-on", "sw1-off",
                "sw2-on", "sw2-off", "btn-0", "btn-1", "btn-2"):
       self.h.newpin(name, hal.HAL_BIT, hal.HAL_OUT)
   self.h.newpin("pot", hal.HAL_FLOAT, hal.HAL_OUT)      # 0.0 .. 1.0
   ```
2. décomposer le masque et publier `pot / 1023.0` ;
3. remettre toutes les pins bit à faux quand `online` est faux, pour qu'un
   débranchement ne fige pas un bouton à l'état « appuyé ».

### 3. Câblage HAL

**Sélection d'axe** — remplacer le bloc `jog-enable` de `pendant.hal` par une
combinaison des bits de sélection et du watchdog (`and2` / `not` natifs) :

```hal
loadrt and2 count=3
loadrt not  count=2
addf and2.0 servo-thread
...
# sel-y = NOT sel-x AND NOT sel-z, puis chaque jog-enable = sel-<axe> AND online
```

**Potentiomètre** — le plus utile est le feed override :

```hal
net pendant-pot pendant.pot => halui.feed-override.scale   # ou un scale + halui.feed-override.counts
```

Attention : `halui.feed-override` fonctionne par comptage, pas par valeur
absolue. Pour piloter directement en absolu, passer par `motion.analog-in-NN`
ou un `scale` vers `halui.feed-override.value` selon la version. À valider sur
LinuxCNC 2.9.4 au moment de l'implémentation.

**Poussoirs** — cibles classiques : `halui.program.run`, `halui.program.pause`,
`halui.spindle.stop`, `halui.home-all`. Un `halui.estop.activate` peut être
ajouté en *complément* — jamais en remplacement de l'E-stop EtherCAT.

### 4. Points à trancher plus tard

- Faut-il un multiplicateur d'incrément (×0,001 / ×0,01 / ×0,1 mm) ? Un des
  inverseurs 2 contacts pourrait le porter, sinon il faudra un 4ᵉ organe.
- `jog-vel-mode` : 0 (incrément exact par cran, précis pour la mise en réf.)
  ou 1 (vitesse proportionnelle, plus fluide sur les longs déplacements).
  Actuellement à 0.
- Un bouton mort (deadman) sur le pendant conditionnant tous les `jog-enable`
  serait cohérent avec l'usage machine, si un poussoir reste disponible.
