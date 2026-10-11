# Remise a plat du volant haptique -- plan

## 1. Ce qui ne marchait pas, et pourquoi c'est structurel

### Le defaut que j'ai introduit hier soir

Dernier symptome signale : *« les vibrations en bas sont tres chiantes, le
moteur vibre tout seul sans que je le touche »*. C'est une consequence directe
de ma derniere modification, et il faut le dire clairement.

J'avais mis l'amortissement a zero dans la zone morte, au motif que la raideur
y est nulle. Resultat : dans la zone morte il n'y a plus **ni ressort ni
frein**, c'est-a-dire plus aucune dissipation. Le rotor devient un oscillateur
conservatif : le ressort le pousse vers le centre, il traverse la zone morte en
roue libre sans rien pour l'arreter, ressort de l'autre cote, se fait repousser,
et ainsi de suite. L'energie injectee a chaque aller n'est jamais retiree.
C'est le cycle limite classique d'un systeme a zone morte sans amortissement --
et il s'entretient sans aucune main sur le volant, ce qui est exactement ce qui
est observe.

### La cause commune des trois bugs d'amortissement

Trois fois de suite, dans trois directions differentes, le meme mecanisme :

| # | Symptome | Cause |
|---|----------|-------|
| 1 | granuleux, resonance 28-53 Hz | `raideurLocale` 7x trop petite la ou la force decolle |
| 2 | vibration dans la zone morte | `raideurLocale` maximale la ou la force est nulle |
| 3 | **oscillation libre dans la zone morte** | `raideurLocale` nulle, donc frein nul, donc zero dissipation |

Le point commun n'est pas le reglage : c'est **le principe d'ordonnancer
l'amortissement sur la pente locale de la force**. `D = f(raideur_locale)` est
une idee qui parait juste (amortissement critique `2*sqrt(kJ)`) mais qui est
fausse en pratique, pour deux raisons :

- la pente locale d'un profil non lineaire varie d'un facteur 100 sur un seul
  cran, donc `D` saute dans le meme rapport, et une marche de `D` est une
  marche de couple proportionnelle a la vitesse ;
- l'amortissement ne sert pas seulement a amortir le ressort : il dissipe
  l'energie du systeme entier, y compris la ou il n'y a pas de ressort.

C'est moi qui avais introduit cette loi en `sqrt(P)`. SmartKnob ne le fait pas.
**Je la supprime.**

## 2. Ce que fait SmartKnob, verifie dans le source

[`motor_task.cpp`](https://github.com/scottbez1/smartknob/blob/master/firmware/src/motor_task.cpp),
verbatim :

```cpp
dead_zone_adjustment = CLAMP(
    angle_to_detent_center,
    fmaxf(-config.position_width_radians*DEAD_ZONE_DETENT_PERCENT, -DEAD_ZONE_RAD),
    fminf( config.position_width_radians*DEAD_ZONE_DETENT_PERCENT,  DEAD_ZONE_RAD));

if (fabsf(motor.shaft_velocity) > 60) {
    // Don't apply torque if velocity is too high (helps avoid positive feedback loop/runaway)
    motor.move(0);
} else {
    float input = -angle_to_detent_center + dead_zone_adjustment;
    ...
    float torque = motor.PID_velocity(input);
    motor.move(torque);
}
```

avec `DEAD_ZONE_DETENT_PERCENT = 0.2`, `DEAD_ZONE_RAD = 1 deg`,
`PID_velocity.P = detent_strength_unit * 4`, `PID_velocity.limit = 10`.

Trois choses a retenir, et c'est tout le secret :

1. **`input = -(e - clamp(e, +-z))`** : nul dans la zone morte, puis **pente
   CONSTANTE** au-dela. Pas de tanh, pas de sinus, pas de plateau. Une droite.
2. Comme la pente est constante, **la raideur ne depend pas de la position** :
   `k = P` partout hors zone morte. Il n'y a donc **qu'un seul `D` a choisir**,
   et la question de l'ordonnancement ne se pose meme pas. Tout le probleme que
   j'ai cree n'existe pas dans cette structure.
3. Un **garde-fou de vitesse** coupe le couple au-dela de 60 rad/s.

Un profil sinusoidal a sa force **maximale a mi-chemin entre deux crans** :
c'est ce qui produit le « faux cran » dans l'intervalle, et c'est pour ca que
la zone morte + droite est preferee. L'observation faite sur la machine
(« si le haut du pic est plus haut que la force, la force devient 0 ») etait la
meme chose vue de l'autre bout.

## 3. L'ecart de materiel qu'il faut traiter, et lui seul

Un autre portage FOC du meme principe
([simonhtet10/Haptic-Knob](https://github.com/simonhtet10/Haptic-Knob)) a
mesure precisement notre probleme :

> *« A `−DAMP · velocity` term is the textbook way to settle a haptic detent.
> Here it made the knob oscillate much worse. The velocity signal is a finite
> difference of a ~1 kHz quantized angle, so the damping term was injecting
> noise-driven torque faster than it removed energy. »*

Chiffrage sur notre banc :

- MT6701 14 bits : 1 LSB = `2*pi/16384` = **383,5 urad**
- vitesse par difference finie sur 4 pas a 500 us, soit 2 ms :
  1 LSB de tremblement = `383,5e-6 / 2e-3` = **0,192 rad/s**
- avec `D = 0,27` : **52 mV de couple par LSB de bruit capteur**, a l'arret,
  en permanence, a la cadence de la boucle.

C'est la source du bourdonnement a l'arret. La reponse n'est pas de supprimer
l'amortissement (il faut bien dissiper, cf. §1) mais de le rendre **aveugle au
tremblement de quantification** : une zone morte sur la VITESSE, de la meme
forme que celle sur la position.

```
v_utile = v - clamp(v, -v_zm, +v_zm)      avec v_zm = 0,30 rad/s
```

0,30 rad/s = 17 deg/s = 1,5 LSB par fenetre : le bruit capteur donne
**exactement zero** couple, et tout mouvement reel de la main (> 1 rad/s) est
amorti normalement. Continu, donc sans marche de couple.

## 4. Reglages de la cible : 30 crans par tour

| grandeur | valeur | d'ou elle vient |
|---|---|---|
| largeur de cran | 12,0 deg = 0,2094 rad | 360/30 |
| demi-largeur | 6,0 deg | |
| zone morte | **1,5 deg** = min(0,25*largeur ; 1,5 deg) | SmartKnob dit 1,0 deg ; elargi, la zone morte a ete demandee plus large a plusieurs reprises |
| point de bascule | 1,1 * demi-largeur = 6,6 deg | valeur SmartKnob (`snap_point`), donne 0,6 deg d'hysteresis |
| course du ressort | 6,6 - 1,5 = **5,1 deg** = 0,0890 rad | |
| force au pic | **1,40 V** | 1,6 V juge bon sur des crans a 27 deg ; on en croise plus souvent |
| raideur | P = 1,40 / 0,0890 = **15,7 V/rad** | contre 70 V/rad avant : la droite etale la montee sur toute la demi-largeur au lieu de 1,3 deg |
| frequence propre | `sqrt(P/(R/Kt)/J)` = 115 rad/s = **18,4 Hz** | J = 2e-5, R/Kt = 58,8 |
| retard de phase a 18,4 Hz | 2,4 ms -> **16 deg** | tres confortable ; a 70 V/rad on etait a 39 Hz et 34 deg |
| amortissement critique | `2*sqrt(P*J*R/Kt)` = 0,272 | |
| **D retenu** | **0,19** (zeta = 0,7) | leger sous-amortissement : une detente trop molle n'a pas de « clic » |
| zone morte de vitesse | **0,30 rad/s** | 1,5 LSB par fenetre, cf. §3 |
| garde-fou | couple nul au-dela de **60 rad/s** | SmartKnob |
| rampe de sortie | 5000 V/s | SmartKnob |
| plafond de tension | 2,20 V | mesure : SinePWM sur 5 V plafonne a 2,50 V |

La baisse de raideur de 70 a 15,7 V/rad est l'autre moitie du gain de
stabilite, et elle est **gratuite** : c'est la consequence geometrique du
passage a une droite sur toute la demi-largeur.

## 5. La librairie

But : que le croquis ne contienne plus que du cablage et des profils, et que la
loi de couple soit un objet reutilisable, teste, avec les modes utiles a la CNC.

```
pendant/knob/
  knob.ino          croquis : cablage, table de profils, console serie, matrice
  HapticKnob.h      l'API et la structure de profil
  HapticKnob.cpp    la loi de couple -- le seul endroit ou il y a de la physique
  MT6701I2C.h       le capteur : mediane repliee + garde-fou de saut
  README.md         mode d'emploi
```

### Profil

```cpp
struct HapticProfil {
  const char* nom;
  float   largeur;        // rad entre deux crans ; 0 = aucun cran
  float   force;          // V au point de bascule
  float   zoneMorte;      // rad ; 0 = loi SmartKnob (min(0,25*largeur ; 1,5 deg))
  float   snap;           // 1,0 = bascule a mi-chemin ; 1,1 = hysteresis
  int32_t posMin, posMax; // butees, en numero de cran ; egales = libre
  bool    boucle;         // le dernier cran revient au premier (menu circulaire)
  float   forceButee;     // V du mur hors plage
  float   largeurButee;   // rad de montee du mur
  float   D;              // V.s/rad, CONSTANT -- cf. §1
  float   frottement;     // V de frottement sec, hors zone morte de vitesse
  const int32_t* crantees; uint8_t nCrantees;  // crans sur certaines positions
};
```

### Modes vises, tous exprimables avec ce seul profil

| mode | reglage | usage CNC |
|---|---|---|
| **jog** | 30 crans/tour, sans butee | un cran = un pas de jog, rotation infinie |
| **axe borne** | 10 crans de 54 deg, butees + mur | les 540 deg deja valides |
| **menu** | N crans forts, `boucle` ou butees | choix d'axe, de pas, de fichier |
| **libre** | `largeur = 0`, `frottement` seul | override de vitesse continu |
| **retour au centre** | 1 cran, butees a +-1 | manette de jog proportionnel |
| **crans choisis** | `crantees` | butees molles sur des cotes rondes |

### API

```cpp
HapticKnob volant;
volant.begin(&capteur, &moteur);
volant.charge(PROFILS[0]);
volant.boucle();              // le plus souvent possible

volant.position();            // numero du cran courant
volant.delta();               // +-N depuis le dernier appel, puis remis a zero
volant.angle();               // rad depuis le zero
volant.vitesse();
volant.enButee();
volant.fraction();            // 0..1 dans la course bornee
```

`delta()` est ce dont `pendant-hal.py` a besoin : il travaille deja sur les
ecarts de `count`.

## 6. Verification

L'utilisateur dort. Le defaut principal est donc justement celui que je peux
mesurer **seul** : « le moteur vibre tout seul sans que je le touche » est un
essai sans main.

1. **compilation** -- objectif.
2. **table de la loi** : commande `T` qui imprime la force calculee sur un
   balayage d'angles, d'un centre de cran jusqu'au mur. Verifie la largeur de
   zone morte, la constance de la pente, le pic, le point de bascule et le mur
   **sans toucher au volant**.
3. **essai a vide, 20 s, sans main** : RMS de Uq et crete-a-crete de l'angle.
   Critere : `Uq_RMS < 10 mV` et angle crete-a-crete `< 5 LSB`. C'est la
   mesure directe du symptome signale.
4. **cadence de boucle** : relevee, pas supposee.
