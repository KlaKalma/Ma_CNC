# Volant haptique brushless (moyeu creux) — matériel

Évolution du pendant : remplacer le MPG mécanique par un **moteur gimbal à
moyeu creux** piloté en FOC. Le moteur fabrique les crans par logiciel
(détentes virtuelles, butées molles, nombre de crans variable selon l'axe ou le
pas de jog). Voir `pendant/README.md` pour le pendant actuel et le protocole série.

**Montage retenu : TMC6300 en 5 V, capteur MT6701 en I²C, sans écran.**

| Élément | Référence | Rôle |
|---|---|---|
| MCU | Arduino **UNO R4 WiFi** (RA4M1, 48 MHz, 32 ko RAM) | FOC + haptique + liaison série |
| Driver | **TMC6300**, breakout SparkFun (rouge, ~20×15 mm) | étage de puissance, 6-PWM, 2–11 V |
| Capteur | **MT6701** (MagnTek), carte sous le moteur | angle absolu 14 bits |
| Moteur | gimbal à moyeu creux | actionneur **et** organe de ressenti |
| Écran | GC9A01 1,28" rond — **plus tard, sur ESP32-S3** | pas de broche libre ici |

La SimpleFOC Mini (DRV8313) reste en pièce de rechange : elle demande **8 V
minimum**, donc inutilisable en 5 V.

## Qui fait quoi

Cinq éléments, chacun ne sait faire qu'une chose, et c'est une **boucle fermée**.

**L'aimant**, collé au bout de l'axe creux face à la puce. Aimant diamétral :
son champ pointe dans une direction qui tourne avec l'axe.

**Le MT6701 — les yeux.** Il mesure la direction de ce champ et la convertit en
angle absolu 14 bits (16384 pas/tour), sans contact. Il sert à deux choses : le
FOC a besoin de l'angle du rotor **à chaque instant** pour savoir quelles
bobines alimenter (un brushless n'a pas de balais pour commuter seul), et
l'application a besoin de la position du bouton pour fabriquer les crans et les
counts envoyés à LinuxCNC.

**L'UNO R4 — le cerveau.** Aucune puissance. Il lit l'angle, calcule les
tensions à appliquer sur les 3 phases pour obtenir le couple demandé (c'est ça,
le FOC), et les sort en **6 signaux PWM**. Il héberge aussi la logique haptique
(couple = f(angle)) et les trames série.

**Le TMC6300 — le muscle.** Zéro intelligence : 3 demi-ponts de MOSFET
intégrés. Il recopie en puissance ce que les PWM demandent, en puisant dans le
5 V. `VIO` fixe le niveau logique et sert d'enable.

**Le 5 V.** Le moteur ne peut pas être alimenté par la logique du MCU.

Le courant dans les bobines crée un couple → le doigt le sent → l'axe tourne un
peu → l'aimant tourne → le MT6701 voit le nouvel angle → le cerveau recalcule.
1000 à 3000 fois par seconde. Ce ne sont pas des crans mécaniques, c'est un
couple recalculé en permanence.

```
   angle (I²C)        3 tensions        courant          couple
  MT6701 ───────► UNO R4 ───────► TMC6300 ───────► moteur ───────► doigt
     ▲            (FOC)           (PWM)           (bobines)         │
     │                                                             │
     └──────────────── l'aimant tourne avec l'axe ◄─────────────────┘
```

## Graph de connexion

Carte **TMC6300 au format SparkFun** (rouge, ~20×15 mm). Attention : `VIN`
(puissance) et `VIO` (logique) sont **deux broches distinctes**, les deux
doivent être câblées.

### Alimentation

```
    ┌─────────────┐
    │  ALIM 5 V   │  ← USB du pendant, buck 24 V→5 V… (au choix)
    └──┬───────┬──┘
       │ +5 V  │ GND
       │       │
       │       ├────────────► UNO R4   GND   ◄── masse commune, obligatoire
       │       ├────────────► TMC6300  GND   (2 broches, l'une ou l'autre)
       │       └────────────► MT6701   GND
       │
       ├────────────────────► TMC6300  VIN   puissance moteur, 2–11 V
       ├────────────────────► TMC6300  VIO   niveau logique + sortie de standby
       └────────────────────► MT6701   VCC   (accepte 3,0–5,5 V)
```

Si le 5 V vient de la R4 elle-même (cas USB), la masse est commune d'office.
S'il vient d'un buck séparé, **relier impérativement sa masse à celle de la R4**.

### Signaux

```
   UNO R4 WiFi                    TMC6300                   moteur gimbal
  ┌────────────┐                ┌──────────────────┐      ┌──────────────┐
  │    D3  ────┼──── UL ───────►│ UL           U ──┼─ ph.1┤              │
  │    D5  ────┼──── UH ───────►│ UH           V ──┼─ ph.2┤  moyeu creux │
  │    D6  ────┼──── VL ───────►│ VL           W ──┼─ ph.3┤              │
  │    D9  ────┼──── VH ───────►│ VH               │      └──────┬───────┘
  │    D10 ────┼──── WL ───────►│ WL               │             │
  │    D11 ────┼──── WH ───────►│ WH               │        axe creux
  │    D4  ◄───┼──── DIAG ──────┤ DIAG  (option)   │        + aimant
  │            │                │                  │             │
  │            │                │ SENSE ──── NC    │             │
  │            │                │ VCP   ──── NC    │             ▼
  │            │                └──────────────────┘      ┌──────────────┐
  │ A4 ou SDA ◄┼──── SDA ───────────────────────────────► │   MT6701     │
  │ A5 ou SCL ─┼──── SCL ───────────────────────────────► │   MODE = NC  │
  └────────────┘                                          └──────────────┘
                                                   le capteur lit l'aimant
                                                   de l'axe, sans contact
```

| De | Vers | Rôle |
|---|---|---|
| R4 `D3` | `UL` | PWM phase U, bras bas |
| R4 `D5` | `UH` | PWM phase U, bras haut |
| R4 `D6` | `VL` | PWM phase V, bras bas |
| R4 `D9` | `VH` | PWM phase V, bras haut |
| R4 `D10` | `WL` | PWM phase W, bras bas |
| R4 `D11` | `WH` | PWM phase W, bras haut |
| R4 `D4` | `DIAG` | défauts, optionnel — déclarer `INPUT_PULLUP` côté R4 |
| — | `SENSE` | **ne pas câbler** — mais le vérifier à l'ohmmètre, voir ci-dessous |
| — | `VCP` | **ne pas câbler** : pompe de charge, condensateur externe optionnel |
| `U`, `V`, `W` | 3 fils du moteur | ordre libre, il change juste le sens de rotation |
| R4 `A4` **ou** `SDA` | MT6701 `SDA` | donnée I²C |
| R4 `A5` **ou** `SCL` | MT6701 `SCL` | horloge I²C |

⚠️ **L'ordre du connecteur TMC6300 est `UL UH VL VH WL WH`** — bas avant haut.
Câblé en suivant l'ordre physique D3→D11, ça donne la correspondance ci-dessus,
et donc le constructeur SimpleFOC **`BLDCDriver6PWM(5, 3, 9, 6, 11, 10)`**
(qui attend, lui, l'ordre `UH UL VH VL WH WL`). Inverser haut et bas dans une
phase, c'est au mieux un moteur qui ne tourne pas, au pire un bras en conduction
permanente.

Côté capteur, on utilise le connecteur **I²C** de la carte (`VCC — SCL SDA GND`),
`MODE` **laissé en l'air**. Attention, ce connecteur a une position inutilisée :
**compter les positions, pas les fils** — un décalage d'un cran enlève
l'alimentation du capteur ET met une ligne du bus à la masse.

Le fil `Z` qui pend est la broche 8 du MT6701 : index en mode ABZ, mais **`CSN`
en mode I²C/SSI**. Le laisser en l'air n'est pas idéal : si le bruit le tire au
niveau bas, le chip bascule en SSI et se met à piloter `DO` (= la ligne SDA),
ce qui corrompt le bus. Le **tirer au +5 V par 10 kΩ** (résistance, pas un fil
direct : si jamais `MODE` était à GND, `Z` serait une sortie).

### A4/A5 ou les broches SDA/SCL : c'est le même fil

Sur la R4 (comme sur toutes les UNO depuis la R3), les broches `SDA`/`SCL`
près de `AREF` **sont le même net électrique que A4/A5** — deux points d'accès
au même bus `Wire`, dupliqués pour la compatibilité des shields. Prendre l'un
ou l'autre est indifférent ; les broches dédiées sont même plus propres, elles
évitent de squatter visuellement le connecteur analogique.

⚠️ Mais ça **ne libère pas A4/A5** pour autre chose : dès que le bus I²C
tourne, ces deux broches sont prises, quel que soit le point où on s'est
branché. Ne pas y mettre de bouton ni de lecture analogique.

(Le troisième point d'accès, le connecteur **Qwiic**, est lui un bus *séparé*
— `Wire1`, en 3,3 V. Celui-là libérerait vraiment A4/A5, mais demande un câble
JST-SH qu'on n'a pas.)

`VIO` est fixé au 5 V en dur, pas piloté par une broche : il alimente la logique
du TMC6300 et le sort du standby. Couper le moteur se fait par
`motor.disable()`, qui met les phases en l'air via les états de phase du 6-PWM.

### Carte sans LED : deux vérifications à l'ohmmètre d'abord

Le design SparkFun d'origine porte trois LED (`V` rouge = VIN présent,
`S` bleue = hors standby, `D` verte = défaut). Les clones les omettent souvent
— emplacements `S`/`D`/`V` vides. Sans elles, on perd le diagnostic visuel, et
surtout **une autre économie de BOM est possible et bloquante** :

1. **Shunt de `SENSE`** — ohmmètre entre `SENSE` et `GND`, carte hors tension.
   Doit lire ~0 Ω (0,12 Ω + les pointes de touche). **Si c'est ouvert, le
   retour des bras bas n'existe pas et le moteur ne tournera jamais** : relier
   alors `SENSE` à `GND` par un fil (on ne mesure pas le courant ici, donc
   aucune perte).
2. **Court-circuit** — ohmmètre entre `VIN` et `GND` : doit lire quelques
   kΩ ou plus, jamais 0 Ω.

Puis, carte sous tension et avant d'envoyer le moindre PWM : voltmètre sur
`VIN` et sur `VIO`, les deux doivent être à 5 V par rapport à `GND`. `VIO` à
0 V = circuit en reset, rien ne bougera.

`DIAG` ne sert à rien tant qu'on n'a pas de référence : relever son niveau au
repos une fois tout sain, et considérer tout changement comme un défaut.

Broches restantes : **D0, D1, D2, D7, D8, D12, D13, A0–A3** — de quoi loger les
9 boutons et le potentiomètre prévus. ⚠️ **D3 devient une sortie PWM** : si le
MPG mécanique reste câblé, le déplacer en D7/D8 (toutes les broches de la R4
acceptent les interruptions, la contrainte D2/D3 ne valait que pour le Pro Micro).

## Régler la tension appliquée — le point qui grille le moteur

Un brushless n'a pas de tension nominale. Il a une résistance de phase (10–15 Ω
sur un gimbal) et un KV très faible. Un volant passe sa vie **à l'arrêt** :
électriquement c'est une résistance, et c'est la loi d'Ohm qui décide.

| Tension réellement appliquée | I = U/R (R ≈ 12 Ω) | Dissipé dans le moteur | Effet |
|---|---|---|---|
| 3 V | 0,25 A | 0,75 W | détentes douces |
| **5 V** | **0,42 A** | **2,1 W** | **bon couple de volant** |
| 12 V | 1 A | 12 W | ça cuit |
| 24 V | 2 A | 48 W | fumée en quelques secondes |

Mesurer la vraie valeur : ohmmètre entre deux fils de phase, **÷ 2** pour un
bobinage en étoile. C'est cette valeur par phase qui va dans
`motor.phase_resistance`.

Ce n'est **pas** l'alim qui fixe le courant mais `motor.voltage_limit` :
l'étage découpe en PWM, le moteur ne voit que la moyenne. En 5 V le pire cas
reste tolérable, ce qui est confortable — mais garder une limite basse est la
seule protection, le TMC6300 n'a pas de mesure de courant câblée ici.

Monter en tension n'apporterait rien : la tension d'alim ne sert qu'à atteindre
de la *vitesse*, et un volant tourne à 1–2 tr/s. Le 5 V est le point de
fonctionnement correct, pas un compromis — c'est ce que fait le SmartKnob de
référence.

🔴 **Le TMC6300 accepte 2–11 V. Jamais 24 V : destruction immédiate.**

## MT6701 — à quoi servent tous ces pads

Un seul boîtier SOP-8, mais **cinq interfaces de sortie exclusives**. Les
connecteurs de la petite carte sont juste différentes façons de câbler les
**mêmes** broches.

| Pin | Nom | Fonction selon le mode |
|---|---|---|
| 1 | VDD | 3,0 – 5,5 V (> 4,5 V requis pour écrire l'EEPROM) |
| 2 | **MODE** | **à GND = ABZ/UVW** · **en l'air = I²C/SSI** |
| 3 | OUT | sortie analogique (DAC 12 bits) ou PWM de l'angle |
| 4 | GND | — |
| 5 | PUSH | détection d'appui axial sur l'aimant |
| 6 | A | A (incrémental) · SDA (I²C) · DO (SSI) |
| 7 | B | B (incrémental) · SCL (I²C) · CLK (SSI) |
| 8 | Z | Z (index, 1 impulsion/tour) · CSN (SSI) |

| Sérigraphie du connecteur | Mode | Ce que c'est |
|---|---|---|
| `VCC OUT PUSH GND` | analogique/PWM + appui | OUT = angle sur **un seul fil** ; PUSH = clic |
| `VCC — SCL SDA GND` | **I²C** ← utilisé | adresse **0x06**, angle en 0x03/0x04, ≤ 400 kHz, ~300 µs/lecture |
| `VCC CSN CLK DO GND` | **SSI** | trame 24 bits, SPI mode 1, ≤ 500 kHz, ~60 µs/lecture |
| (non sorti ici) | ABZ | quadrature 1–1024 PPR, mais perd l'absolu |

**OUT** = l'angle absolu en tension (rail-à-rail) ou en rapport cyclique.
Inutilisable pour du FOC propre (bruit, saut au passage 360°→0°).

**PUSH** = le MT6701 mesure aussi le déplacement **axial** de l'aimant. Si la
mécanique laisse quelques dixièmes de mm avec un ressort de rappel, on obtient
un **clic sur le bouton** sans contact ni switch. Réputé délicat à régler.

Trame **SSI** (évolution future, plus rapide et plus robuste qu'I²C) :

```
 bits 23..10  angle 14 bits   (angle/16384 × 360°)
 bits  9..6   status 4 bits   b0..b1 champ magnétique (00 = OK)
                              b2     appui axial (PUSH)  ← gratuit, sans fil en plus
                              b3     perte de suivi (rotation trop rapide)
 bits  5..0   CRC-6           polynôme x^6 + x + 1
```

Si le capteur ne répond pas en I²C : vérifier qu'un strap ne tire pas `MODE` à
GND sur la carte (ce serait le mode ABZ).

### Diagnostic du bus I²C

Le sketch `pendant/knob_test1/` (palier 1, capteur seul, moteur bloqué) affiche à lui
seul de quoi trancher. Ce qu'il faut lire :

| Ce qu'affiche le sketch | Interprétation |
|---|---|
| `code=2` NACK adresse | le bus est **sain**, mais personne ne répond en 0x06 → `MODE` strappé à GND (mode ABZ), ou mauvaise adresse |
| `code=5` + scan lent (~5 s) | **timeouts** : une ligne est tenue bas, le bus ne fonctionne pas |
| `repos SDA=1 SCL=1` | bus au repos correct |
| `repos SDA=0 SCL=0` **et** `pull-up interne SDA=0 SCL=0` | les deux lignes sont maintenues à la masse malgré 33 kΩ de tirage |

Ce dernier cas a une cause dominante : **le capteur n'est pas alimenté**. Un
circuit non alimenté ramène ses broches d'entrée vers la masse par ses diodes
de protection, un pull-up faible n'y peut rien, et les deux lignes lisent 0.
Autre cause possible, le court-circuit franc d'une ligne vers GND.

Les deux se vérifient au multimètre, côté carte capteur :

1. **Voltmètre entre `VCC` et `GND` du capteur** (sur la carte elle-même, pas
   sur l'Arduino) → doit lire 5 V. 0 V = le fil d'alimentation n'arrive pas.
2. **Ohmmètre, hors tension et débranché**, entre le fil `SDA` et le fil `GND`
   → doit être ouvert, jamais 0 Ω. Idem pour `SCL`.

Le suspect numéro un est le **décalage d'un cran** du connecteur à 5 positions
du capteur : il prive le chip d'alimentation et met une ligne à la masse, ce qui
produit exactement cette signature.

## Configuration qui fonctionne (2026-10-11, mesurée)

**Mesures de référence, reproduites à chaque lancement :**

| grandeur | valeur | comment elle est obtenue |
|---|---|---|
| `pole_pairs` | **7** | un tour électrique = 0,9031 rad mécanique → 2π/0,9031 = 6,96 |
| `sensor_direction` | **CW** | el 0→+90° fait avancer le mécanique de +0,229 rad (attendu π/2/7 = 0,224) |
| `zero_electric_angle` | ≈ **3,51** | mesuré, hystérésis 0,0000 rad entre les deux approches |
| rotation | **15 tours en 3 s à 0,35 A**, symétrique à 1 % | mode couple, les deux sens |
| boucle FOC | **996 Hz**, 0 décrochage sur 85 s | deux cycles complets des 7 profils |

```cpp
BLDCDriver6PWM driver = BLDCDriver6PWM(5, 3, 9, 6, 11, 10);  // UH UL VH VL WH WL
BLDCMotor      motor  = BLDCMotor(7);
MagneticSensorI2C sensor = MagneticSensorI2C(MT6701_I2C);    // preset SimpleFOC 2.4.0

driver.voltage_power_supply = 5.0;
driver.pwm_frequency        = 10000;
driver.init();

motor.foc_modulation    = FOCModulationType::SpaceVectorPWM;
motor.phase_resistance  = 2.55;                  // fiche constructeur
motor.controller        = MotionControlType::torque;
motor.torque_controller = TorqueControlType::estimated_current;   // AMPERES
motor.init();

// APRES init(), et par les SETTERS (voir plus bas)
motor.updateVoltageLimit(3.0);
motor.updateCurrentLimit(0.60);                  // nominal moteur 0,5 A

calageDirect();          // mesure sens + zero electrique, PAS initFOC()

// detentes : N positions stables par tour, amorties et fondues en vitesse
motor.move(-K * sin(N * motor.shaft_angle) * fonduCrans(v, N) - B * v);
```

### `TorqueControlType` : volts ou ampères, ce n'est pas le même code

`SimpleFOC 2.4.0`, `FOCMotor.cpp` :

```cpp
case TorqueControlType::voltage:            // ligne 606 — des VOLTS
  voltage.q = _constrain(current_sp, -voltage_limit, voltage_limit) + feed_forward_voltage.q;

case TorqueControlType::estimated_current:  // ligne 618 — des AMPERES
  current_sp = _constrain(current_sp, -current_limit, current_limit) + feed_forward_current.q;
  voltage.q  = current.q * phase_resistance + voltage_bemf;
```

En mode `voltage`, **`phase_resistance` et `current_limit` n'interviennent pas
du tout**, et `updateCurrentLimit()` est un no-op complet :

```cpp
void FOCMotor::updateCurrentLimit(float v) {
  current_limit = v;
  if(torque_controller != TorqueControlType::voltage) { PID_velocity.limit = v; }
}
```

Conséquence vécue : les profils haptiques renvoyaient `0,18` en croyant des
ampères, donc 0,18 V sur 2,55 Ω = **0,07 A**. Soixante-dix milliampères — le
moyeu tournait libre et le symptôme lu était « plus aucune force dans le knob ».

### Les limites se posent par les setters, après `init()`

`motor.init()` recopie `voltage_limit` dans `PID_velocity.limit`,
`PID_current_q.limit` et `PID_current_d.limit`. **Écrire ensuite dans le champ
`motor.voltage_limit` ne met rien à jour** : les PID gardent la valeur figée à
l'init. Symptôme observé : `Isp` et `Uq` écrêtés à `2.00` pile alors que le
champ valait `2.55`. Utiliser `updateVoltageLimit()` / `updateCurrentLimit()`.

### Calage mesuré plutôt que `initFOC()`

`initFOC()` déplace le champ par pas de 0,7° ; le cogging accroche le rotor et
il sort **une valeur différente à chaque passe** — `3.00` puis `5.94` sur deux
essais consécutifs. Un zéro électrique faux = champ appliqué de travers = pas
de couple utile.

Le calage mesuré part du seul chemin reproductible, `setPhaseVoltage(U, 0, el)`
à pleine amplitude avec 1,2 s pour se poser. Deux pièges :

1. **Revenir à `el=0` après un pas de 0,23 rad ne ramène pas le rotor**, le
   cogging le retient : on relit la position de `el=90°` et le zéro sort décalé
   de π/2 pile. Il faut approcher `el=0` par le dessous **puis** par le dessus
   et moyenner — en moyenne **circulaire**, l'angle mécanique boucle à 2π.
   Résultat : hystérésis 0,0000 rad, répétable au centième.
2. **`setPhaseVoltage(Uq, 0, el)` ne pose pas le champ sur `el`.** Avec `Ud=0`
   il calcule `Ualpha = -sin(el)·Uq`, `Ubeta = cos(el)·Uq` : le vecteur stator
   pointe à `el + π/2`. Un rotor au repos sous `el=0` se tient donc à π/2
   électrique, et `electricalAngle()` doit rendre **±π/2**, pas 0. Un contrôle
   qui attend 0 criera « incohérent » sur un calage parfaitement juste.

### Obligatoire : débloquer le bus I²C avant d'initialiser

Le MT6701 peut rester **bloqué en plein octet** et répondre `err=2` indéfiniment.
Un reflash n'y change rien — la R4 redémarre, pas le capteur — et une coupure
secteur non plus. Seules **9 impulsions d'horloge sur SCL** le libèrent :

```cpp
pinMode(A4, INPUT_PULLUP);              // SDA relachee
pinMode(A5, OUTPUT);                    // SCL pilotee
for (uint8_t i = 0; i < 9; i++) {
  digitalWrite(A5, LOW);  delayMicroseconds(10);
  digitalWrite(A5, HIGH); delayMicroseconds(10);
  if (digitalRead(A4)) break;
}
pinMode(A4, OUTPUT); digitalWrite(A4, LOW); delayMicroseconds(10);
digitalWrite(A5, HIGH);                    delayMicroseconds(10);
pinMode(A4, INPUT_PULLUP);                 delayMicroseconds(50);
pinMode(A5, INPUT);
Wire.begin();  Wire.setClock(400000);  sensor.init();
```

C'est la seule différence qui séparait `knob_test1` (capteur lu sans une
erreur) des sketches SimpleFOC (`err=2` en boucle). Des heures ont été perdues
à chercher cette panne côté câblage.

### Garde-fou capteur : sonder le bus, et surtout AGIR

Si le capteur se tait, SimpleFOC pilote le champ sur un angle faux et le moyeu
devient mou. Il faut couper le couple — mais en **sondant le bus directement**.
Une première version lisait `sensor.getAngle()` sans appeler `sensor.update()`
et sortait de `loop()` avant `loopFOC()` : l'angle restait figé sur la valeur
fautive, et le garde-fou ne se relâchait **jamais**, capteur réparé ou non.

**Deuxième erreur, plus coûteuse : constater sans agir.** Le déblocage du bus
ne tournait que dans `setup()`. Mesure : `err=2` au bout de 4 s de couple, puis
30 s d'affilée sans jamais revenir. Le garde-fou voyait la panne et attendait un
retour qui ne pouvait pas arriver tout seul. Il **rejoue maintenant le
déblocage** une fois par seconde tant que c'est muet — et chaque décrochage se
répare en ~300 ms.

`Wire.end()` est indispensable dans le déblocage en cours de route : sans lui,
A4/A5 restent aux mains du périphérique I²C et les `pinMode()` ne prennent pas.

**Signature du vrai fautif :** `1 impulsion, SDA=1` — SDA n'était jamais tenue
bas. Ce n'est donc pas l'esclave qui se verrouille, c'est le **périphérique I²C
du RA4M1**, et ce qui le répare c'est `Wire.end()` + `Wire.begin()`.

### Trois causes logicielles de décrochage I²C, toutes mesurées

**1. Matraquage du bus.** `loopFOC()` tournait à la cadence de la boucle, 1 800
à 3 200 lectures I²C par seconde, chaque transaction collée à la précédente.
Zéro temps mort pour le périphérique. **Cadence fixe à 1 kHz** → le bus est
libre 90 % du temps, et 1 kHz reste dix fois plus rapide qu'une main.

**2. Baisser l'horloge empire les choses.** Essai à 100 kHz en pensant gagner
de la marge au bruit : décrochage toutes les secondes au bout de 30 s. Une
lecture de 2 octets passe de 75 à 300 µs — à cadence de boucle égale le bus est
occupé 54 % du temps au lieu de 30. **Rester à 400 kHz** et prendre la marge en
espaçant les lectures.

**3. Repliement de spectre sur les détentes.** `-K·sin(N·a)` est un signal de
fréquence *spatiale* N. À 1 kHz il faut ~10 points par cran, soit
`v ≤ 1000·(2π/N)/10 = 628/N` rad/s — **15,7 rad/s seulement pour N=40**.
Au-delà le couple se replie, pousse à contretemps, la boucle entre en cycle
limite, ça hurle et ça tire du courant en continu. Trace : parfaitement propre
sur les profils lisses, rafales de décrochage sur les profils à crans, surtout
juste après la roue libre qui rend le moyeu lancé.

Correctif : fondre le terme de crans au-dessus du seuil et ne garder que
l'amortissement. Au toucher c'est même ce qu'on veut — on lance le volant, il
roule libre, les crans reviennent en ralentissant.

**Après ces trois correctifs : 0 décrochage sur 85 s, boucle à 996 Hz.**

### Détentes : jamais de ressort sans amortissement

`-K·sin(N·a)` seul est un ressort **sans perte**. Sur un moyeu quasi sans
frottement c'est un oscillateur non amorti : le rotor sonne dans son cran au
lieu de s'y poser. À 40 crans/tour le puits est raide, donc ça sonne vite, ça
siffle, et ça tire du courant en permanence. Un terme `-B·v` avec B ≈ 0,012
A/(rad/s) y met fin : 0,04 A à vitesse de main (imperceptible), 0,24 A sur une
sonnerie à 20 rad/s (tuée en une période).

### Ne jamais faire confiance à la première vitesse après un décrochage

`Sensor::getVelocity()` force `Ts = 1e-3` dès que l'intervalle dépasse 0,5 s
(son garde-fou anti-débordement de `micros()`). La première lecture vaut donc
`Δangle/0,001` : **mesuré à 2074, puis 9136 rad/s**. Avec un terme en `-B·v`,
ça devient un coup de couple pleine puissance dans une direction arbitraire.

Deux parades, les deux en place : brider `v` à ±60 rad/s dans la fonction de
couple (une main ne dépasse jamais 570 tr/min, au-delà c'est un artefact), et
rester à couple nul 200 ms après une reprise.

### Le sens du capteur : CW, mesuré

⚠️ Une version antérieure de ce document affirmait `CCW`. **C'est faux**, et
l'erreur venait de ce que la conclusion reposait sur un balayage qui appelait
`initFOC()` — dont le `zero_electric_angle` est justement non reproductible.

Le sens se mesure directement : `el 0 → +90°` doit faire avancer l'angle
mécanique de `π/2 / pole_pairs`. Mesure, répétée à chaque lancement :
**+0,229 rad pour 0,224 attendu** → le capteur et l'angle électrique
s'accordent en signe → **`CW`**. Et avec CW + le zéro mesuré, le mode couple
sort 15 tours en 3 s dans les deux sens à 1 % près.

Un sens inversé donne une réaction positive : le champ oscille autour du rotor
au lieu de le précéder, on lit de la vibration et zéro déplacement net.

**`voltage_sensor_align` assez haut.** Le calage déplace le champ par pas de
0,7° ; sous ~1 A le cogging bloque le rotor et SimpleFOC répond `Failed to
notice movement`. Ce message ne dit **rien** sur le moteur, seulement que le
capteur n'a pas vu bouger.

### Fiche constructeur du moteur (2804, M创动工坊)

| | |
|---|---|
| Encoches / pôles | **12N14P → 7 paires de pôles** |
| Résistance | 5,1 Ω entre phases, **2,55 Ω par phase** |
| Courant | **0,5 A nominal, 2 A maxi** |
| Tension | 12 V nominale (plage 7,4–16 V) |
| KV / Ke / Kt | 220 / 0,0434 V·s/rad / 0,0434 Nm/A |
| Couple | 300 g·cm = 0,03 Nm |
| Inertie rotor | 3700 g·mm² — 37 g |

Tourner en **5 V ne limite que la vitesse, pas le couple** : 5 V / 2,55 Ω =
1,96 A, déjà au-delà du maximum moteur. Inutile de chercher du 12 V, que le
TMC6300 ne supporterait pas de toute façon (11 V max).

### Carte encodeur MT6701 — ce que dit son schéma

Le connecteur 5 points est **`VCC MODE SCL SDA GND`** : la position que je
croyais vide est le **MODE**. `R5 = 0 Ω` le relie au +5 V, `R2 = 10 kΩ` à la
masse, et le pont `IIC` bascule l'un vers l'autre — `MODE=0` ABZ (défaut
d'usine), `MODE=1` I²C/SSI. **Il suffisait donc de relier cette broche au +5 V
par le connecteur, sans rien souder.** `R3`/`R4` = 10 kΩ de tirage sur SCL/SDA,
déjà sur la carte. ABZ par défaut à 1024 points.

## Logiciel

```cpp
BLDCMotor motor = BLDCMotor(7);                              // paires de pôles à vérifier
BLDCDriver6PWM driver = BLDCDriver6PWM(5, 3, 9, 6, 11, 10);  // UH UL VH VL WH WL
MagneticSensorI2C sensor = MagneticSensorI2C(                // config de départ, à valider
  MagneticSensorI2CConfig_s{ .chip_address = 0x06, .bit_resolution = 14,
                             .angle_register = 0x03, .data_start_bit = 15 });
// Wire.setClock(400000); sensor.init();          // A4/A5
// driver.voltage_power_supply = 5;
// motor.phase_resistance = 12;  motor.current_limit = 0.4;   // jamais la pleine tension
```

`pole_pairs` = aimants du rotor ÷ 2 (exemple SimpleFOC `find_pole_pairs_number`
en cas de doute). Une valeur fausse donne un moteur qui vibre ou chauffe sans
tourner.

### Ce qui est validé sur la machine (2026-10-10)

| Étape | Résultat |
|---|---|
| MT6701 en I²C sur A4/A5 | ✅ répond à **0x06**, 14 bits, **zéro erreur** de bus, l'angle suit la rotation |
| Mode du module | il sort d'usine en **ABZ** : le pont `IIC` doit être soudé pour obtenir l'I²C. Sans lui, les broches A/B sont des **sorties** qui tiennent SDA/SCL à 0 — bus mort, insensible aux pull-ups et au déblocage par 9 impulsions |
| Preset SimpleFOC | `MagneticSensorI2C(MT6701_I2C)` existe en **2.4.0**, inutile d'écrire la config à la main |
| `driver.init()` 6-PWM sur RA4M1 | ✅ **réussit**. Chaque broche reçoit son propre canal GPT (D5→ch0/A, D3→ch1/A, D9→ch7/B…), donc **temps-mort logiciel**, pas matériel. PWM à 24 kHz, inaudible |
| Alimentation | 4,5 V mesurés au capteur sur l'USB du Pi, **moteur à l'arrêt** |
| **Résistance du moteur** | **6 Ω entre phases → 3 Ω par phase** (ohmmètre). Ce n'est **pas** un gimbal haute résistance : il faut raisonner en **courant**, pas en tension |
| Couple | présent, le rotor se cale sur un vecteur fixe et le tient |
| Câblage des phases | confirmé bon : balayage des 5 permutations H/L, les inversions d'une seule phase donnent une rotation nette nulle, l'inversion des trois ne change rien (= décalage de 180° élec.) |
| `pole_pairs` | **non déterminé** — 4 mesures incohérentes (3,5 / 3,3 / 12,68 / 93), toutes faites pendant que le rotor décrochait |
| Rotation continue | **pas encore obtenue** — mais tous les échecs constatés l'ont été capteur muet, donc non concluants |
| Couple à bas courant | le rotor suit **dès 0,2 A** sur un pas de 90° élec. (0,2385 rad, aller/retour symétriques à 0,3 %). Il n'y a **pas** de seuil de décollage : ce qui bloquait, c'étaient des pas de 5–30°, trop petits pour produire assez de couple face au cogging |
| Ordre de grandeur de `pole_pairs` | 6 ou 7 d'après le pas de 90° ; le cogging fait toujours **sous-estimer** le déplacement, ce qui penche pour 6. **À confirmer en comptant les aimants du rotor et en divisant par 2** |

### Les deux points durs qui restent

**1. L'alimentation.** À 3 Ω par phase, la loi d'Ohm devient brutale :

| Tension appliquée | Courant par phase | Puissance | Verdict |
|---|---|---|---|
| 0,6 V | 0,2 A | 0,12 W | trop faible pour vaincre le cogging |
| 1,5 V | 0,5 A | 0,75 W | mini utilisable |
| 2,0 V | 0,67 A | 1,3 W | plafond raisonnable sur USB |
| 4,0 V | **1,33 A** | 5,3 W | **au-delà du TMC6300 (1,4 A RMS) et du port USB** |

Les premiers essais tournaient à 4 V : l'USB du Pi s'effondrait, la tension
réelle n'avait plus rien à voir avec la consigne, les sinusoïdes étaient
écrêtées et le rotor claquait d'un cran à l'autre. **Un buck 24 V→5 V d'au
moins 1 A n'est plus une option de confort, c'est un prérequis.**

Config logicielle correspondante : `motor.phase_resistance = 3.0` et raisonner
en `motor.current_limit`, avec `motor.voltage_limit` comme garde-fou dur.

Deuxième preuve, indépendante : le **capteur se fait débarquer du bus** dès que
le moteur tire. Signature `I2C err=2` (NACK sur l'adresse : bus sain, personne
ne répond), alors qu'un simple reset le fait revenir intact (`raw=1900,
err=0`). Le MT6701 demande **3,0 V minimum** ; on part de 4,5 V à vide et
chaque pointe de courant passe sous le seuil. Il décroche silencieusement, et
SimpleFOC répond alors `Failed to notice movement` — ce qui ne dit **rien** sur
le moteur, seulement que le capteur est muet. Toutes les mesures faites dans
cet état sont à jeter.

→ **Câblage à retenir : le TMC6300 sur le buck 24 V→5 V, le capteur et la R4
sur l'USB.** Ainsi le courant moteur ne peut plus faire plonger le rail du
capteur. Masses reliées.

**2. `CSN`/`Z` laissé en l'air — RÉGLÉ.** Pull-up 10 kΩ vers +5 V posé le
2026-10-11 : plus aucun décrochage du type « ligne tenue à 0 » (26 sondes
pendant un test complet, 0 invalide). Ce défaut-là est bien distinct de la
chute d'alimentation ci-dessus, et les deux se cumulaient.

**Ancien diagnostic, conservé pour mémoire :** Le bus I²C est parfait moteur à l'arrêt
(`err=0`), et décroche par intermittence dès que les PWM commutent : les
lectures reviennent alors à `0xFFFF` (angle figé à 6,2828 rad = 2π×16383/16384),
ce qui invalide silencieusement toute mesure. C'est le symptôme exact d'un
`CSN` qui bascule sous le bruit et fait passer le MT6701 en mode SSI, où il
cesse d'acquitter l'I²C. **À tirer au +5 V par 10 kΩ avant toute nouvelle
mesure.**

### Pièges rencontrés, à ne pas refaire

- `PIN_PWM` est une **macro du core Renesas** : un tableau de ce nom ne compile pas.
- La R4 WiFi **ne redémarre pas** sur un toggle DTR (pont ESP32) : pour relire
  la bannière de démarrage, il faut reflasher ou prévoir un `delay()` au début
  de `setup()`.
- Une valeur d'angle **exactement** à 0 ou à 2π×16383/16384 n'est pas une
  position, c'est un bus mort dans un sens ou dans l'autre. Toujours tracer le
  `raw` et le code d'erreur de `endTransmission()` à côté de l'angle.
- Mesurer `pole_pairs` par pas successifs est biaisé par le cogging ; il faut
  faire aller **et** retour et moyenner — et même ainsi, ça ne vaut rien si le
  rotor décroche.
- **Jamais de `while (1) { }` sur un échec d'init.** Le démo en avait deux, sur
  `driver.init()` et sur `initFOC()`. La matrice restait figée sur le `0`
  affiché juste avant, sans un mot d'explication, et aucun débranchement n'en
  sortait. Le symptôme lu était « 0 à nouveau » et il a envoyé chercher la
  panne dans le câblage. Signaler, lever un drapeau, continuer à tourner,
  offrir une commande pour réessayer.
- **Les 6 bras du pont doivent être forcés OUTPUT/LOW en toute première
  instruction de `setup()`.** Au reset le RA4M1 laisse D3/D5/D6/D9/D10/D11 en
  entrées flottantes ; sur le TMC6300 un UH et un UL flottants peuvent conduire
  ensemble — les deux MOSFET d'un bras court-circuitent VIN à GND, trois bras
  en parallèle. `knob_test2` attendait jusqu'à 5,5 s avant son `driver.init()`.
  C'est un candidat direct au « 5 V débranché, 0,5 V branché », et à 0,5 V le
  MT6701 est en brown-out donc `raw=0`.
- `raw == 0` **exactement** doit compter comme un échec de lecture : c'est la
  signature du brown-out du capteur. `angle_prev` tombe à 0.000 pile,
  `full_rotations` à −1, et `getAngle()` rend **−6,283 = −2π exactement** puis
  se fige. Une position légitime à exactement 0 sur 14 bits, c'est 1 chance
  sur 16384.
- Un **angle constant avec une vitesse constante non nulle** est impossible :
  c'est toujours un bug de code, jamais un comportement du moteur. C'est ce
  couple de valeurs (`angle=-6.283`, `vitesse=0.44 rad/s` pendant 5 s) qui a
  identifié le brown-out.

Sketches : `pendant/knob_test1/` (capteur seul, moteur bloqué, diagnostic I²C complet)
et `pendant/knob_test2/` (SimpleFOC, moteur désactivé au démarrage, `g` = 5 s de
rotation lente, `s` = stop).

⚠️ **Le 6-PWM sur RA4M1 est le chemin le moins balisé de SimpleFOC.** Le driver
Renesas gère le temps-mort matériel (paire haut/bas sur les sorties A/B d'un
même canal GPT) ou logiciel (2 canaux), et **dit lequel il a obtenu** si on
compile avec `SIMPLEFOC_RENESAS_DEBUG` + `SimpleFOCDebug`. Messages d'échec
possibles : `no PWM on pin`, `channel in use`, `output A/B mismatch`. Garder
`driver.dead_zone` par défaut, ne jamais commander UH et UL hauts ensemble.

### Mise en route par paliers

Pour ne pas chercher trois pannes à la fois :

1. **Capteur seul** (4 fils, pas d'alim de puissance) : afficher
   `sensor.getAngle()` en tournant l'axe à la main. Doit balayer 0 → 6,28 rad,
   proprement. Valide l'I²C, l'aimant et sa distance à la puce.
2. **+ moteur en boucle ouverte** (`velocity_openloop`, `voltage_limit` à
   2 V) : il doit tourner régulièrement. Valide alim, phases, PWM.
3. **Boucle fermée en couple** : `initFOC()` fait un petit mouvement de calage
   au démarrage (zéro électrique + sens) — cet à-coup est normal. Puis détentes.

## Limites de la R4 dans ce montage

- **Pas d'écran** : les 6 PWM prennent les 6 broches PWM de la R4, dont
  **D11 = MOSI**, donc plus de SPI matériel. Admis : le GC9A01 était de toute
  façon hors d'atteinte ici (framebuffer 240×240×16 bpp = 115 ko contre 32 ko
  de RAM, SPI bloquant sans DMA → ~38 ms par trame pleine pendant lesquelles
  `loopFOC()` s'arrête).
- **I²C vers un pendant déporté** : bus lent et fragile sur un long câble, à
  côté du VFD. Un blocage du bus fige `Wire` donc `loopFOC()`, et le dernier
  vecteur de tension reste appliqué → `voltage_limit` bas est la vraie
  protection. Câble court et blindé ; passer en SSI (CS dédié, pas d'état
  partagé) pour l'installation définitive.
- Pas d'USB natif (`NO_USB`) : `Serial` passe par le pont ESP32-S3 en UART
  115200. Suffisant (≈ 20 octets/trame), déjà documenté dans `pendant/README.md`.
- L'ESP32-S3 présent sur la R4 WiFi **n'est pas réutilisable** : ses GPIO ne
  sont pas sortis, il ne fait que le pont USB/WiFi.

## Trajectoire

1. **Maintenant** : le montage ci-dessus. Valide moteur, capteur, `pole_pairs`,
   sens, détentes, et le protocole série existant.
2. **Ensuite** : capteur en SSI, 74HC165 sur le bus SPI pour les 9 boutons.
3. **Cible ESP32-S3** : même TMC6300, même 5 V, + écran GC9A01. N'importe quel
   GPIO y fait du PWM, donc 6-PWM et SPI cohabitent sans conflit ; 2 cœurs,
   SPI DMA et RAM lèvent les limites ci-dessus. La R4 redevient la carte
   boutons, ou disparaît.

Côté LinuxCNC, **rien à changer** : `pendant-hal.py` travaille en **delta** sur
`count`. Le volant brushless remplace la source de quadrature et continue
d'émettre des trames `S <count> <btn> <pot>`.

## Sources

- MT6701 — [datasheet MagnTek](https://uploadcdn.oneyac.com/attachments/files/brand_pdf/magntek/F3/CA/MT6701QT-STD.pdf) ·
  [notes pratiques Curious Scientist](https://curiousscientist.tech/blog/mt6701-magnetic-encoder)
- TMC6300 — [datasheet](https://cdn.sparkfun.com/assets/d/4/9/9/4/TMC6300_datasheet_rev1.07.pdf) ·
  [hookup guide](https://docs.sparkfun.com/SparkFun_Three_Phase_Motor_Driver-TMC6300/single_page/)
- SimpleFOC — [MCU supportés](https://docs.simplefoc.com/arduino_mcu) ·
  [choix des broches PWM](https://docs.simplefoc.com/choosing_pwm_pins) ·
  [BLDCDriver6PWM](https://docs.simplefoc.com/bldcdriver6pwm) · support
  `ARDUINO_UNOR4_WIFI` et contraintes de canaux GPT dans
  `src/drivers/hardware_specific/renesas/renesas.cpp`
- Référence haptique 5 V — [smartknob (scottbez1)](https://github.com/scottbez1/smartknob)
