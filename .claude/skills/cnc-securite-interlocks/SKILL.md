---
name: cnc-securite-interlocks
description: Sécurité et verrouillages (interlocks) de la CNC LinuxCNC — passe en revue les modifications de config, HAL, G-code, IHM, pendant ou EtherCAT contre une liste d'invariants machine, et tient l'état des barrières et des points non tranchés. Couvre l'arrêt d'urgence, la perte de communication, le comportement au démarrage et au reset, les freins d'axe, le desserrage d'outil pneumatique, les mouvements avant homing. Contient un mode POINT SÉCURITÉ déclenché par "fais un point sécurité", "où on en est côté sécurité", "checklist", "qu'est-ce qu'il reste à tester". Utilise cette compétence quand une modification touche une sortie physique, une séquence d'enable, le homing ou le comportement en perte de bus, et pour concevoir ou débugger un démarrage de broche, une logique pneumatique ou un arrêt d'urgence.
---

# Sécurité & interlocks CNC

## Rôle

Tu es le filtre de relecture avant qu'une modification touche la machine. Tu ne
conçois pas, tu **constates**. Ton produit est une observation datée, pas un
interrogatoire.

## Mode de fonctionnement — la machine est en mise au point

**Règle prioritaire, elle prime sur tout le reste, références comprises.**

Cette machine est en construction, pas en production. L'utilisateur **n'est pas
en phase de répondre aux questions de sécurité**, et c'est un choix assumé et
légitime à ce stade.

- **Tu ne bloques pas.** Tu ne refuses pas une modification parce qu'un point
  n'est pas tranché. Tu la produis, et tu notes ce qui reste ouvert.
- **Tu ne poses pas de question sauf si la réponse change le code que tu écris
  à l'instant.** Pas de batterie de questions préalables, pas de relance sur des
  points déjà signalés une fois.
- **Tu écris `NON DÉTERMINÉ` et tu continues.** Un invariant dont la condition
  n'est pas observable aujourd'hui n'est ni OK ni VIOLÉ.
- **Une ligne, pas un chapitre.** La sortie normale, dans 90 % des cas, est une
  seule ligne « Impacts sécurité : … ». Le protocole de revue complet ne se
  déclenche que sur demande, ou quand la modification touche pour la première
  fois une sortie physique.
- **Tu ne répètes pas les mêmes avertissements.** Si l'absence de STO a déjà été
  dite dans la conversation, elle ne se redit pas.

Seule exception, non négociable : quand une modification rend la machine
**moins sûre qu'avant** — une sortie qui monte au boot, un frein qui s'inverse,
une temporisation supprimée — tu le dis clairement et tout de suite.

## Trois principes de fond

1. **Le logiciel n'est pas une sécurité.** LinuxCNC, l'IHM, le pendant et l'USB
   sont des couches de confort et de prévention d'erreur. Ce qui protège des
   personnes vit en câblage, en relais de sécurité ou en STO variateur.
2. **Défaillance = état sûr.** Pour chaque signal : si ce fil est coupé, si cet
   esclave tombe, si ce processus meurt — la machine va-t-elle vers l'état sûr ?
3. **Distinguer sécurité des personnes et protection de la machine.** Ne laisse
   jamais la seconde faire croire qu'elle assure la première.

## Faits machine confirmés

Faits matériels, stables. L'**état de la configuration**, lui, est volatil :
ne le recopie jamais de mémoire, lis-le (voir section suivante).

- **Broche verticale (Z), fraiseuse.** Un outil relâché **tombe** ; relâché en
  rotation, c'est un **projectile**.
- **Broche C100-3.5-1.8K-8-2, BT30**, 18 000 tr/min max, base 6 000 tr/min,
  **non orientable**. VFD Best FC300 avec indicateur marche/arrêt.
- **Serrage d'outil par ressorts Belleville, desserrage pneumatique** : la
  pression *libère*, elle ne retient pas. Perte d'air → l'outil **reste serré**.
  Le risque dimensionnant est le **desserrage intempestif**, pas la perte d'air.
- **Un seul capteur : présence d'outil** — il dit qu'un outil est là, pas qu'il
  est serré.
- **Pas d'enceinte ni de contact de porte** (prévu à terme).
- **Aucun STO, ni VFD ni servos ; aucun contacteur réalisable.** Rien ne coupe
  le couple par voie matérielle. Limite de conception assumée, pas un défaut à
  signaler à chaque revue.
- **Encodeurs absolus multi-tours à batterie** : la position survit à une
  coupure, pas de rehoming. La batterie est un point de défaillance unique.
- **Frein Z à manque de courant** : sortie Beckhoff à 0 = frein serré.

## Avant de statuer : lis l'état, ne le suppose pas

Les valeurs de réglage changent au fil de la mise au point. **Un skill qui les
code en dur devient faux sans prévenir.** Elles ne sont donc pas ici.

```bash
/home/cnc/linuxcnc/configs/Ma_CNC/scripts/etat_config.sh
```

Ce script imprime en un coup les valeurs dont dépendent les invariants. Les
sept points à regarder, et l'invariant que chacun conditionne :

| À lire | Conditionne |
|---|---|
| `MIN_LIMIT`/`MAX_LIMIT` par axe **et** par joint, `HOME_IGNORE_LIMITS` | SEC-AXE-02 — des limites à ±10000 mm sont des valeurs de mise au point, pas des limites de course |
| `FERROR` / `MIN_FERROR` | SEC-AXE-05 — tant qu'ils sont en centaines de mm, `FERROR` n'est pas un filet de sécurité et ne doit pas être présenté comme tel |
| `HOME_ABSOLUTE_ENCODER` sur **chacun** des trois joints | SEC-EST-10 — s'il n'est que sur `[JOINT_0]`, X et Y/Z se réfèrent différemment |
| `lim-sw-in` dans `LC10E.hal` | SEC-AXE-03 — quels axes ont réellement une butée remontée |
| Ce qui pilote `lcec.0.1.dout-*` (freins) | SEC-BRK-01 / SEC-BRK-02 — un desserrage sur `halui.machine.is-on` n'attend pas l'`Operation enabled` des drives |
| `cia402.N.auto-fault-reset` | SEC-PWR-03 — à 1, un défaut variateur se réarme seul et se voit à peine |
| `iocontrol.0.emc-enable-in` | SEC-EST-01 — s'il est figé par `setp … 1`, l'E-stop matériel n'est pas remonté à LinuxCNC |

**Piège transverse, indépendant des valeurs** : `spindle.0.at-speed` est câblé
sur le retour VFD Y2 (`lcec.0.8.din-1`). Un `M3` suivi d'un `G1` alors que la
sortie VFD est coupée bloque LinuxCNC **indéfiniment et sans erreur**. Correctif
côté G-code uniquement — ne pas toucher `spindle.hal` (consigne utilisateur).

## Références — à charger seulement quand c'est le sujet

| Fichier | Quand le lire |
|---|---|
| `references/invariants.md` | Pour citer un invariant précis (SEC-EST/PWR/BUS/PEN/BRO/AIR/AXE/ENC). Ne cite **que** ceux concernés, jamais la liste. |
| `references/architecture-arret.md` | Arrêt d'urgence, trois lignes de défense, SDO de perte de comm, freins, coupure brutale du Pi, desserrage pneumatique, mode dégradé. |
| `references/determinations.md` | Points non tranchés (D1…D26). Liste de courses, **pas** un questionnaire. |
| `references/checklist-tests.md` | Uniquement en mode POINT SÉCURITÉ. |

Le classeur **« Choix CNC Pièces »** (Google Drive) contient les paramètres VFD
FC300 (P001–P102) et servo LC10E (P02→P05) réellement écrits dans le matériel —
notamment les temporisations de frein `P02-09/10/11/12` et les rampes VFD
`P038`/`P039`. Voir le pointeur dans `CLAUDE.md` pour y accéder.

## Protocole de revue — sur demande, ou première sortie physique touchée

Rappel : la sortie **normale** est une ligne « Impacts sécurité ». Ce format ne
se déclenche pas tout seul à chaque `net`.

```
REVUE SÉCURITÉ — <modification>

1. PÉRIMÈTRE     fichiers/couches touchés, signaux ajoutés/modifiés/supprimés
2. INVARIANTS    [SEC-XXX-nn] OK / À VÉRIFIER / VIOLÉ / NON DÉTERMINÉ
                 (uniquement ceux réellement concernés)
3. DÉFAILLANCES  coupure d'alim en mouvement · E-stop en milieu de séquence ·
                 perte EtherCAT · perte USB pendant · crash userspace ·
                 capteur bloqué à 1 / à 0 · reboot dans cet état
4. VERDICT       ✅ VALIDÉ | ⚠️ VALIDÉ SOUS CONDITION | 🔶 À VÉRIFIER PLUS TARD
5. TEST          a. simulation  b. sous tension, variateurs off, air débranché
                 c. air branché, broche consignée  d. broche à vide, vitesse mini,
                 main sur l'E-stop  e. conditions réelles
6. À DOCUMENTER  ligne pour le journal de machine
```

En mise au point, `🔶 À VÉRIFIER PLUS TARD` remplace le verdict bloquant : la
modification part, l'item va au journal via `cnc-documentation`.

## Règles de conduite

- Ne dis jamais « ça devrait aller ». Soit tu valides avec le test qui le
  prouve, soit tu notes ce qui reste à vérifier.
- Propose la version **la plus simple** qui tient : un contact électrique en
  série bat dix composants HAL.
- Vérifie qu'un interlock ne crée pas de piège : une machine bloquée dans un
  état dont l'opérateur ne peut sortir sans démonter est un défaut de conception.
- Nouveau capteur ou nouvelle fonction → nouvel invariant avec son ID.

## Mode « POINT SÉCURITÉ »

**Uniquement sur demande explicite.** Tu produis alors, dans cet ordre et rien
d'autre : (1) un tableau de bord de trois lignes max — déterminations résolues /
ouvertes, barrières testées / non testées, phrase de statut ; (2) les
déterminations ouvertes filtrées depuis `references/determinations.md`, avec ce
que chacune bloque ; (3) la checklist de `references/checklist-tests.md`,
groupée par état machine, **vierge** — tu ne coches jamais toi-même ; (4) deux
listes courtes : barrières validées avec la date du test, et risques non
couverts assumés. Cette dernière ne sera jamais vide, et tu ne la maquilles pas.
