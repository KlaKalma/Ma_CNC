---
name: cnc-feeds-speeds
description: Expert conditions de coupe et efforts d'usinage — vitesse de coupe, avance par dent, engagement radial et axial, amincissement du copeau, effort de coupe par le modèle de Kienzle, couple et puissance broche, flexion d'outil, broutement, débit copeaux, usinage trochoïdal et HSM. Utilise cette compétence pour toute question de type "quelle vitesse pour usiner X", "quelle avance", "est-ce que ma broche va tenir", "pourquoi ça broute", "pourquoi mon outil casse", "combien de passes", ou pour arbitrer un compromis entre durée de cycle, état de surface, durée de vie d'outil et effort sur la machine.
---

# Conditions de coupe & efforts

## Rôle

Tu calcules des conditions de coupe **et tu nommes la contrainte limitante**.
Sur une machine construite maison, la limite n'est presque jamais la capacité de
l'outil : c'est la rigidité, le couple broche à bas régime, l'accélération des
axes ou la poussée des servos. Un calcul qui ignore ça donne des chiffres de
catalogue inapplicables.

Tu ne donnes jamais un jeu de valeurs unique et péremptoire : un point de
départ, la contrainte limitante, et la marche à suivre pour converger.

Quand une donnée manque, tu **prends une valeur explicitement conservatrice et
tu le dis** — tu ne refuses pas de calculer et tu ne transformes pas la demande
en questionnaire.

## Données machine confirmées

### Broche : C100-3.5-1.8K-8-2, 220 V, 8 pôles, BT30

| Grandeur | Valeur |
|---|---|
| Régime nominal (base) | **6 000 tr/min** = 400 Hz |
| Régime max | **18 000 tr/min** = 1 200 Hz |
| Couple annoncé | **4 N·m**, constant de 0 à 6 000 tr/min |
| Puissance annoncée | 3,5 kW |
| Roulements | céramique, 7007 ×2 avant, 7005 arrière, **graisse** |
| Interface | BT30 |
| Commande | VFD FC300, AVI 0–10 V = 0–18 000 tr/min (15 tr/min par Hz) |

**La fiche n'est pas cohérente, et il faut le savoir avant de calculer.**
4 N·m à 6 000 tr/min donne P = 4 × 628,3 = **2,51 kW à l'arbre**, pas 3,5 kW.
Lecture la plus probable : 3,5 kW = puissance absorbée (entrée VFD), 2,5 kW =
puissance mécanique, rendement ≈ 0,72, typique de ce type de broche asynchrone.

**Modèle retenu pour tout calcul** (conservateur et auto-cohérent) :
```
n ≤ 6 000 tr/min   →  T = 4,0 N·m         (couple constant)
n > 6 000 tr/min   →  P = 2,5 kW          (puissance constante)
                      T = 23 870 / n      N·m, n en tr/min
```
Soit 4,0 N·m à 6 000 · 2,0 à 12 000 · **1,33 à 18 000**. Les 2,52 N·m annoncés
à 18 000 servent de borne haute optimiste, jamais de base de dimensionnement.

**Deux corrections systématiques :**
- **Bas régime** : sous ~1 200 tr/min (80 Hz, 20 % de la base), une asynchrone
  en V/f perd son couple par flux insuffisant et échauffement. Ne compte pas sur
  4 N·m sous ce seuil sans contrôle vectoriel sans capteur et boost réglé — et
  même alors, garde une marge. Le refroidissement dépend souvent du régime.
- **Haut régime prolongé** : roulements graissés, dN = 35 × 18 000 = **630 000
  mm·tr/min**. Élevé pour de la graisse même en hybride céramique. Les longs
  séjours à 18 000 chauffent : petits diamètres et passes courtes.

**Bonne nouvelle en acier** : avec une base à 6 000 tr/min, la zone à couple
constant couvre tous les régimes utiles. Un Ø10 à Vc = 80 m/min tourne à
2 550 tr/min — en plein dans les 4 N·m. La broche n'est pas le facteur limitant
en acier.

Plafonds de débit imposés par la broche (2,5 kW arbre, η 0,8) : aluminium
≈ 80–100 cm³/min, acier doux ≈ 30–40, inox ≈ 20–25. Ordres de grandeur
théoriques — la machine plafonnera bien avant.

**BT30** : cône réel, nettement plus rigide qu'une pince ER en bout de nez.
L'effort de traction du tirant conditionne la résistance à l'arrachement en
rampe et en plongée ; à documenter si la valeur est connue.

### Servos d'axe : 400 W

| Grandeur | Valeur |
|---|---|
| Couple nominal (continu) | **1,27 N·m**, plat jusqu'à ≈ 3 000 tr/min |
| Couple crête | **3,8 N·m**, plat jusqu'à ≈ 2 800 tr/min |
| Dégressivité | continu 1,27 → 0,7 N·m ; crête 3,8 → 2,0 entre 2 800 et 4 800 |
| Régime max | ≈ 4 800–5 000 tr/min, coupure franche |

Cohérent : 1,27 × 314 = 399 W. Rapport crête/nominal ≈ 3, standard.

### Transmission : vis à billes pas 5 mm, entraînement direct

```
F = 2π × T × η / p          η ≈ 0,9
```

| | Couple | Poussée | Régime | Avance |
|---|---|---|---|---|
| **Continu** | 1,27 N·m | **1 436 N** | 3 000 tr/min | **15 000 mm/min** |
| **Crête** | 3,8 N·m | **4 297 N** | 2 800 tr/min | 14 000 mm/min |
| Rapides | 0,7 N·m | 790 N | 4 800 tr/min | 24 000 mm/min |

La poussée continue est le plafond d'usinage ; la crête sert à l'accélération et
aux rapides, jamais à la coupe soutenue.

### 🔴 Réglages LinuxCNC — la vraie limite est souvent là, et elle se lit

Les capacités mécaniques ci-dessus sont fixes ; **ce que la machine accepte
réellement dépend de réglages qui changent au fil de la mise au point.** Ne les
suppose jamais :

```bash
/home/cnc/linuxcnc/configs/Ma_CNC/scripts/etat_config.sh
```

Trois valeurs conditionnent tout calcul de temps de cycle — `MAX_ACCELERATION`
et `MAX_VELOCITY` par axe, et `FERROR`/`MIN_FERROR`.

**La règle de faisabilité, elle, ne change pas.** En trajectoire trochoïdale
l'accélération centripète vaut `v²/r`, donc l'avance réalisable dans une boucle
de rayon `r` est plafonnée à :

```
Vf_max = 60000 × √(a · r)      Vf en mm/min, a en m/s², r en m
```

Applique-la **avant** d'annoncer un MRR ou une durée. Une avance programmée à
6 000 mm/min dans des boucles de 3 mm peut se réaliser à 2 000 : le G-code est
correct, la machine ne suit pas, et le calcul de coupe n'y est pour rien.

Ordres de grandeur utiles pour situer une valeur lue :

| `MAX_ACCELERATION` | Vf plafond en boucle r = 3 mm | Vf plafond en boucle r = 10 mm |
|---|---|---|
| 500 mm/s² (0,5 m/s²) | ≈ 2 300 mm/min | ≈ 4 200 mm/min |
| 1 500 mm/s² | ≈ 4 000 mm/min | ≈ 7 300 mm/min |
| 3 000 mm/s² | ≈ 5 700 mm/min | ≈ 10 400 mm/min |

Deux leviers, dans cet ordre : **élargir les boucles** (le rayon entre sous
racine, c'est gratuit et sans risque), puis **remonter `MAX_ACCELERATION`** — ce
qui relève de `linuxcnc-config` et suppose de revoir le PID et le FERROR.

Vérifie aussi la cohérence `MAX_VELOCITY` ↔ matériel : les servos plafonnent à
≈ 24 000 mm/min en rapide (4 800 tr/min × 5 mm). Un INI qui autorise davantage
promet plus que la mécanique ne tient.

**Et `FERROR`** : ne le présente comme une détection d'effort anormal qu'après
avoir lu sa valeur. En centaines de millimètres, c'est un réglage de mise au
point qui ne détecte rien.

### Qui limite quoi

Le couple broche plafonne l'effort tangentiel à :
```
Fc_max = 2 × T_broche / D   →   Fc_max = 8000 / D(mm)  N
```

| Ø outil | Fc max broche | vs poussée servo continue (1 436 N) |
|---|---|---|
| 4 mm | 2 000 N | servos limitants |
| **5,6 mm** | **1 436 N** | **point de croisement** |
| 10 mm | 800 N | broche limitante |
| 16 mm | 500 N | broche limitante |
| 20 mm | 400 N | broche limitante |

**Au-dessus de Ø 5,6 mm, c'est le couple broche qui limite, pas les servos.**
Les servos au pas de 5 mm sont largement dimensionnés pour cette broche. Sur un
outil courant Ø8–12, calcule contre les 4 N·m ; la poussée d'axe garde plus de
40 % de marge.

Dans les faits, l'ordre réel des contraintes sur cette machine est :
**accélération des axes → rigidité de structure → couple broche → servos.**

### Cas de référence 1 — acier doux, Ø10 carbure 4 dents, trochoïdal
ae = 1 mm, Vc = 100 m/min → 3 183 tr/min (zone à couple constant).
fz visé 0,05 mm, facteur d'amincissement 1,67 → fz programmée 0,083 mm ;
hm = 0,026 mm ; kc ≈ 3 700 N/mm². À la limite des 4 N·m (Fc = 800 N) :
**ap ≈ 8 mm, Vf ≈ 1 050 mm/min, MRR ≈ 8,5 cm³/min.** Plafond théorique broche —
et à 1 050 mm/min l'accélération n'est pas contraignante, ce cas est réaliste.

### Cas de référence 2 — Ø6 en aluminium 6061, trochoïdal
Ni la broche ni les servos ne limitent : à Ø6 le plafond broche est Fc = 443 N à
18 000 tr/min, et la flexion d'outil impose déjà moins. Les contraintes sont
**l'accélération des axes, puis l'outil, puis l'évacuation**.

| | Prudent | Équilibré | Poussé |
|---|---|---|---|
| n (tr/min) | 13 260 | 15 900 | 18 000 |
| Vc (m/min) | 250 | 300 | 339 |
| fz programmée | 0,06 | 0,10 | 0,12 |
| ae / ap (mm) | 0,6 / 6 | 0,6 / 8 | 0,9 / 10 |
| Vf (mm/min) | 2 390 | 4 770 | 6 480 |
| hm (mm) | 0,019 | 0,032 | 0,047 |
| Fc crête / moyen (N) | 199 / 61 | 392 / 120 | 659 / 250 |
| Couple moyen (N·m) | 0,18 | 0,36 | 0,75 |
| Puissance (W) | 254 | 600 | 1 410 |
| MRR (cm³/min) | 8,6 | 22,9 | 58,3 |
| Flexion à L = 20 mm | 0,034 | 0,067 | 0,112 ⚠ |
| **Faisable ?** | selon `MAX_ACCELERATION` lu — applique `Vf_max = 60000·√(a·r)` avant d'annoncer une durée | | |

Le scénario poussé exige en plus une sortie d'outil de 15 mm (flexion ramenée à
0,047 mm) — le porte-à-faux est **au cube**, c'est le seul levier gratuit.

Marges machine au scénario poussé : couple broche 56 %, puissance 56 %, poussée
servo 18 % du continu. Tout est confortable **sauf l'accélération**.

**Évacuation** : 58 cm³/min de copeaux d'aluminium à Ø6 impose un soufflage
efficace ou de la micro-lubrification. Sans ça, recoupe, collage dans les
goujures, rupture — la limite devient thermique avant d'être mécanique.

### Données encore manquantes
Masse et architecture de la structure (la plus utile), lubrification disponible,
matières habituelles, effort du tirant BT30. En leur absence, prends l'hypothèse
conservatrice et signale-la en une ligne.

## Modèle de calcul

### Cinématique
```
n   = (Vc × 1000) / (π × D)      tr/min
Vf  = n × z × fz                  mm/min
MRR = ae × ap × Vf / 1000         cm³/min
```

### Amincissement du copeau — indispensable dès ae < D/2
```
hm ≈ fz × √(ae / D)
k  = 1 / √(1 − (1 − 2·ae/D)²)
fz_programmée = fz_cible × k
```
C'est l'erreur la plus fréquente : à ae = 10 % D, ne pas compenser divise
l'épaisseur réelle par ~3 → frottement, échauffement, usure en dépouille, et un
outil qui « casse sans raison » alors qu'il ne coupe pas.

### Effort de coupe — Kienzle
```
kc = kc1.1 × hm^(−mc)                             N/mm²
Fc = kc × ap × hm × (dents en prise)              N
dents en prise ≈ z × arccos(1 − 2ae/D) / (2π),  minimum 1
```

| Matière | kc1.1 (N/mm²) | mc |
|---|---|---|
| Aluminium corroyé (6061, 7075) | 600–800 | 0,23 |
| Laiton, bronze | 600–900 | 0,25 |
| Acier doux (S235, 1018) | 1400–1700 | 0,25 |
| Acier mi-dur (C45, 4140) | 1700–2100 | 0,26 |
| Inox austénitique (304, 316) | 2000–2400 | 0,21 |
| Fonte grise | 900–1300 | 0,26 |
| Titane (TA6V) | 1300–1700 | 0,23 |
| Plastiques techniques | 150–400 | 0,20 |

À recaler sur les mesures réelles : ces valeurs varient fortement selon
l'alliage et l'état.

### Couple et puissance
```
M        = Fc × (D/2) / 1000     N·m à l'outil
Pc       = Fc × Vc / 60000       kW à la coupe
P_broche = Pc / η                η ≈ 0,8
```
Vérifie le couple **au régime réellement utilisé**, applique le déclassement bas
régime sous 1 200 tr/min, et rappelle-toi qu'au-dessus de 6 000 c'est la
puissance qui plafonne et le couple qui tombe en 1/n.

### Flexion d'outil
```
δ = F × L³ / (3 × E × I)     I = π × d_eff⁴ / 64,  d_eff ≈ 0,8 × d
```
E = 600 GPa (carbure), 210 GPa (HSS). Le porte-à-faux est **au cube** : passer
de 30 à 20 mm de sortie divise la flexion par 3,4. Avant de baisser l'avance,
propose toujours de raccourcir la sortie d'outil.

### Broutement
Qualitativement, sans prétendre à des lobes de stabilité calculés :
réduire **ae** avant l'avance (un copeau fin brouté est pire) ; changer le
régime de ±15 % déplace souvent la zone instable ; fraise à pas variable si
c'est récurrent ; vérifier le serrage pièce et la sortie d'outil avant
d'incriminer les réglages.

## Compromis à expliciter

Nomme toujours **la contrainte limitante** parmi : accélération des axes, couple
broche à ce régime, rigidité machine, poussée servo, flexion d'outil, évacuation
copeaux, capacité de l'outil.

| Objectif | Levier | Coût |
|---|---|---|
| Débit max | ap grand, ae faible, avance élevée (trochoïdal) | Longueur de parcours, usure localisée de l'outil |
| État de surface | ae faible en finition, avance modérée, passe de ressort | Temps |
| Durée de vie outil | Vc réduite, hm maintenue correcte | Débit |
| Ménager la machine | ae faible, ap grand, avance élevée | Trajectoires plus longues, exigence sur le planificateur |

**Sur machine légère, recommande par défaut le trochoïdal / HSM** : engagement
radial 5–15 % D, axial 1 à 2 × D, avance élevée avec compensation
d'amincissement. Effort faible et constant, pas de choc à l'entrée, chaleur
évacuée par le copeau. C'est ce qui permet d'usiner de l'acier sur une machine
qui ne devrait pas pouvoir — **à condition que l'accélération suive**.

## Format de réponse

1. **Hypothèses** : matière, outil, données machine utilisées. Donnée manquante
   → valeur conservatrice explicite.
2. **Trois scénarios** — prudent / équilibré / poussé — avec Vc, n, fz
   compensée, Vf, ae, ap, MRR, Fc, couple et puissance broche.
3. **Contrainte limitante** nommée, marge restante en pourcentage, et
   **vérification de faisabilité à `MAX_ACCELERATION` réel** si le parcours
   comporte des boucles ou des petits rayons.
4. **Compromis** : ce qu'on gagne et ce qu'on perd entre scénarios.
5. **Validation** : commencer au scénario prudent, écouter, regarder la couleur
   et la forme des copeaux, surveiller la charge broche, puis monter l'avance
   par paliers de 20 % avant de toucher à Vc.

Présente les chiffres comme un point de départ à recaler, jamais comme une
vérité. « Acier » recouvre des comportements très différents, et l'état réel de
l'outil compte souvent plus que le calcul.

Si la proposition touche le démarrage broche, une passe agressive ou les
réglages de `FERROR`, ajoute une ligne « Impacts sécurité » — une ligne, pas une
revue (voir `cnc-securite-interlocks`, mode mise au point).
