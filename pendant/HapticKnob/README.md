# HapticKnob

Volant haptique brushless pour le pendant CNC : crans, butées, menus,
pendant d'axe. UNO R4 WiFi + capteur MT6701 (I²C, A4/A5) + TMC6300 (6-PWM,
D3 D5 D6 D9 D10 D11), moteur gimbal 2804 alimenté en 5 V.

Tout ce qui est codé ici a d'abord été **mesuré** au banc `../knob_lab/`
(essais e01–e12, branche `knob-haptique`, étiquette `knob-v1-crans-stables`).

## Utilisation

```cpp
#include <HapticKnob.h>
HapticKnob knob;

void setup() {
  knob.begin();                    // capteur, pont, calibration EEPROM
  knob.charge(HK::pendant(100, 10));
}

void loop() {
  knob.boucle();                   // le plus souvent possible, ne bloque jamais
  int32_t d = knob.delta();        // crans franchis depuis le dernier appel
  if (d) { /* jog de d pas */ }
}
```

Ne jamais bloquer `loop()` plus de quelques millisecondes : la loi tourne à
2 kHz. Au-delà de 100 ms sans itération, une garde (timer AGT1) coupe le pont
moteur ; il se rallume seul à l'itération suivante.

## Profils prêts à l'emploi

| fabrique | comportement | réglage validé à la main |
|---|---|---|
| `HK::menu()` | crans larges francs, rotation infinie | 27°, 1,05 V, tic 1,5 V |
| `HK::pendant(100, 10)` | 100 crans/tour, tic fort en **arrivant** sur chaque dizaine | « vraiment parfait » |
| `HK::interrupteur()` | 2 creux, butées qui ramenent toujours | 90°, 1,6 V, tic 2,2 V |
| `HK::borne(n, deg)` | n positions, mur au-delà | |
| `HK::crans(parTour, force)` | crans réguliers infinis | |
| `HK::retourCentre(course)` | ressort de rappel au milieu (manette) | |
| `HK::libre(amorti)` | roue libre | |

Un `HKProfil` se modifie champ par champ (largeur, force, zone morte, bascule,
nombre de crans, mur, amortissement, tic, crans forts…) : voir `HapticKnob.h`.

## Les trois règles (et pourquoi)

1. **Amortissement petit** (`amorti` ≈ 0,045 V·s/rad). L'inertie réelle du
   rotor est 1,56·10⁻⁴ V·s²/rad et la boucle a ~1,5 ms de retard ; avec
   l'ancien 0,2 la boucle était **instable** (oscillation à 113 Hz mesurée,
   110 Hz prédite). Le moteur s'amortit déjà lui-même (Ke = 0,0224).
2. **Pente plafonnée** à ~10 V/rad (`penteMax`). Au-delà, même un ressort pur
   oscille. Un cran « plus fort » = bascule plus loin ou **tic** plus fort,
   jamais une pente plus raide. `charge()` plafonne et rend `false`.
3. **Capteur sans filtre.** Les lectures I²C corrompues par la PWM (jusqu'à
   17 % à couple nul) sont toutes détectables et rejetées sans retard.

## Calibration — à refaire une fois le moteur monté

Moteur **libre**, personne ne touche, ~50 s : `knob.calibre(&Serial)`
(touche `c` dans l'exemple `Demo`). Mesure le calage électrique et la carte
de cogging, et les range en EEPROM (relus par `begin()`). À refaire si le
moteur, l'aimant ou le capteur est démonté : un cogging décalé d'un angle
fait plus de mal que pas de compensation.

Sans calibration, la librairie utilise le calage du banc (`zea = 0,310`) et
**ne compense pas** le cogging.

## Flasher

```
./flash.sh                     # examples/Demo
./flash.sh chemin/du/sketch
```

Gère les deux cas : carte en USB natif (`knob_lab`) ou via l'ESP32.

## Limites connues

- Le volant peut s'arrêter jusqu'à ~1° du centre d'un cran (frottement sec
  du moteur, 0,17 V) — jugé non gênant.
- Le tic est plafonné en tension (`uMax` = 2,2 V, limite linéaire du 6-PWM) :
  son énergie se règle par sa durée (`clicMs`, `fortClicMs`).
- L'impulsion « aller » d'un tic est écrêtée quand la force du cran tire déjà
  dans le même sens : tic légèrement asymétrique.
