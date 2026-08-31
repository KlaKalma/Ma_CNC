# Checklist de tests — mode POINT SÉCURITÉ

Référence de `cnc-securite-interlocks`. Ne se produit que sur demande explicite
(« fais un point sécurité », « checklist », « qu'est-ce qu'il reste à tester »).

---

Déclenché par : « fais un point sécurité », « où on en est côté sécurité »,
« checklist », « qu'est-ce qu'il reste à tester ». **Uniquement sur demande.**
Tu produis alors, dans cet ordre et rien d'autre :

**1. Tableau de bord** — trois lignes max : déterminations résolues / ouvertes,
barrières testées / non testées, et la phrase de statut.

**2. Déterminations ouvertes** — le tableau D*, filtré sur les ⬜, avec ce que
chacune bloque.

**3. Checklist de tests**, groupée **par état machine** pour ne pas faire monter
et descendre la machine dix fois. Tu ne coches jamais toi-même : tu produis la
liste vierge, l'opérateur remplit, le résultat va au journal.

#### Bloc 0 — Hors tension, sectionneur ouvert
| # | Test | Critère |
|---|---|---|
| 0.1 | Manœuvrer l'arrêt général d'une main, sans regarder, depuis le poste | Réussi du premier coup, durée notée |
| 0.1b | Poser un cadenas en position ouverte | Possible — sinon SEC-EST-07 n'est pas garantie |
| 0.2 | Continuité de la boucle E-stop à l'ohmmètre | NF : passant au repos, ouvert enfoncé |
| 0.3 | Descendre Z à la main, tête basse | Aucun mouvement |
| 0.4 | Accès à l'arrêt général depuis le poste | Atteignable sans entrer en zone de travail |

#### Bloc 1 — Sous tension, variateurs désactivés, air branché
Mesures **au multimètre sur les bornes Beckhoff**, pas dans HAL.

| # | Test | Critère |
|---|---|---|
| 1.1 | Sorties pendant tout le boot du PC | Freins=0, marche VFD=0, desserrage=0 en permanence |
| 1.2 | Après lancement de LinuxCNC, avant « machine on » | Les trois restent à 0 (SEC-BRK-01) |
| 1.3 | Après E-stop puis reset, sans autre action | Aucune sortie ne remonte (SEC-EST-03) |
| 1.4 | Observer le vérin 5 min, air branché | Aucun mouvement |
| 1.5 | Tenter `halcmd setp` sur la sortie desserrage | Refusé ou sans effet (SEC-PNE-04) |
| 1.6 | Couper l'air | Outil reste serré, message affiché (SEC-AIR-02) |

#### Bloc 2 — Variateurs actifs, broche consignée, sans outil
Vitesse lente, **Z en position basse**, personne dans la zone.

| # | Test | Critère |
|---|---|---|
| 2.1 | Machine on, observer l'ordre enable/frein | `Operation enabled` **puis** frein desserré (SEC-BRK-02) |
| 2.2 | Machine off | Frein serré **puis** variateur désactivé |
| 2.3 | **Débrancher EtherCAT en mouvement lent** (D17/D19) | Arrêt autonome des axes + frein serré, sans le PC |
| 2.4 | `kill -9` sur LinuxCNC en mouvement lent | Mêmes effets que 2.3 |
| 2.5 | Arrêt général en mouvement lent | Z ne descend pas d'un millimètre |
| 2.5b | Rallumer, observer le boot du Pi | Système de fichiers sain, config intacte, aucune sortie active |
| 2.5c | Comparer la position avant/après coupure | Identique, sinon alarme encodeur à investiguer |
| 2.5d | Comparateur sur un point de référence connu | Écart dans la tolérance (SEC-EST-10) |
| 2.5e | Vérifier G54 et les offsets actifs | Conservés, ou perte identifiée et documentée |
| 2.5f | Bit alarme batterie présent en HAL et affiché | Pin existante, remontée à l'IHM (SEC-ABS-01) |
| 2.6 | E-stop pendant un jog | Arrêt, distance de dérive mesurée |
| 2.7 | Freiner doucement un axe à la main, vitesse réduite | Défaut avant contrainte mécanique (suppose FERROR resserré) |
| 2.8 | Approche lente de chaque butée | Arrêt sur chaque fin de course, dans les deux sens |
| 2.9 | Débrancher l'USB pendant un jog | `jog-enable` à 0 en < 200 ms (SEC-PEN-02) |
| 2.10 | Rebrancher après avoir tourné la molette | Aucun mouvement (SEC-PEN-03) |

#### Bloc 3 — Broche en rotation, sans outil puis outil d'essai
Personne dans l'axe de la broche. Main sur l'arrêt général, pas sur l'E-stop.

| # | Test | Critère |
|---|---|---|
| 3.1 | **Temps de descente en vitesse** (D10) : vitesse max → arrêt, chrono jusqu'à immobilité visuelle | Valeur notée ; tempo de desserrage = 2 × cette valeur |
| 3.2 | **Qualifier l'indicateur VFD** (D14) : bascule de la sortie vs arrêt réel | Écart noté ; si > 0,5 s, ne vaut pas autorisation |
| 3.3 | E-stop à vitesse max | Arrêt broche, durée notée |
| 3.4 | Arrêt général à vitesse moyenne, une seule fois | Roue libre, durée notée |
| 3.5 | Tenter le desserrage broche en rotation | Refusé (SEC-BRO-04) |
| 3.6 | Tenter le desserrage juste après l'ordre d'arrêt | Refusé jusqu'à expiration de la tempo |
| 3.7 | Débrancher `tool-present` en rotation lente | E-stop immédiat (SEC-PNE-05) |
| 3.8 | Tenter M3 avec desserrage actif | Refusé (SEC-BRO-03) |

**4. Verdict et non-testé** — deux listes courtes : barrières validées (avec la
date du test) et risques non couverts, assumés. La seconde ne sera jamais vide
sur cette machine, et tu ne la maquilles pas.
