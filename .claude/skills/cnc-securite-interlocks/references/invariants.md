# Liste des invariants de sécurité

Référence de `cnc-securite-interlocks`. Ne cite que les invariants réellement
concernés par la modification en cours — jamais la liste entière.

---

Convention : `sig-*` = signal HAL, `ok` = état sûr/autorisé. Les noms sont
génériques ; les noms réels de cette machine sont dans `LC10E.hal`,
`spindle.hal` et `CLAUDE.md`.

### A — Chaîne d'arrêt d'urgence

| ID | Invariant | Vérification |
|---|---|---|
| **SEC-EST-01** | L'E-stop coupe la puissance **par voie matérielle**, indépendamment du PC. LinuxCNC ne fait que l'observer via `iocontrol.0.emc-enable-in`. | Débrancher le PC → l'E-stop doit encore couper |
| **SEC-EST-02** | Boucle en **contacts NF** : câble coupé = E-stop actif. | Débrancher un contact → E-stop |
| **SEC-EST-03** | Le reset d'E-stop ne **redémarre rien** tout seul. Ni broche, ni air, ni mouvement. | Reset pendant une pause → aucune sortie ne remonte |
| **SEC-EST-05** | Le coup-de-poing du pendant est dans la boucle **matérielle**, jamais transmis par la liaison série. | Couper l'USB → l'E-stop pendant fonctionne encore |
| **SEC-EST-06** | `estop_latch` en HAL est un miroir d'état, pas la source de vérité. | Revue de câblage |
| **SEC-EST-07** | Toute intervention **les mains en zone de travail** se fait sectionneur ouvert et cadenassé — parce qu'aucun couple n'est coupé sous E-stop. | Procédure écrite |

### B — Puissance & variateurs

| ID | Invariant | Vérification |
|---|---|---|
| **SEC-PWR-01** | Aucune sortie active au boot du PC : broche, air, arrosage, enable à 0 tant que « machine on » n'est pas commandé. | Redémarrer le PC, **mesurer les sorties** |
| **SEC-PWR-02** | `machine-is-on` ne suffit pas à autoriser un mouvement : il faut aussi absence de défaut variateur et bus en OP. | `halcmd show sig` |
| **SEC-PWR-03** | Un défaut variateur (fault CiA402, following error, surtempérature) déclenche un arrêt, pas un simple message. *Cf. `auto-fault-reset 1` dans les constats.* | Simuler le défaut, moteurs débrayés |
| **SEC-PWR-04** | La coupure de puissance ne doit pas laisser Z descendre : frein, contrepoids ou vis irréversible. | **Test broche démontée**, à faible hauteur |

### C — Communication (EtherCAT, pendant, RT)

| ID | Invariant | Vérification |
|---|---|---|
| **SEC-BUS-01** | Si `lcec.all-op` retombe ou si un esclave sort de OP → arrêt immédiat, pas de dégradation silencieuse. | Débrancher un câble EtherCAT à vide |
| **SEC-BUS-02** | Le **watchdog des esclaves de sortie est activé** : perte de trame → les sorties retombent d'elles-mêmes. | Vérifier la config SM watchdog |
| **SEC-BUS-03** | Un dépassement du temps réel est détecté et traité. | Surveiller `.tmax` des threads |
| **SEC-PEN-01** | **Aucune fonction de sécurité ne transite par la liaison série du pendant.** | Revue d'architecture |
| **SEC-PEN-02** | Perte de heartbeat pendant → tous les `jog-enable` à 0 en < 200 ms. *En place : `pendant.online`, 0,5 s.* | Débrancher l'USB pendant un jog |
| **SEC-PEN-03** | À la reconnexion, les compteurs sont **resynchronisés** sans appliquer le delta accumulé. | Débrancher, tourner la molette, rebrancher → aucun mouvement |
| **SEC-PEN-04** | Le jog exige une autorisation explicite (axe sélectionné + liaison vivante). Une molette qu'on frôle ne fait pas bouger la machine. | Test au repos |

### D — Broche ↔ outil ↔ air

| ID | Invariant | Vérification |
|---|---|---|
| **SEC-BRO-02** | Pas de démarrage broche si l'outil n'est pas confirmé serré (deux capteurs, états contradictoires = défaut). | `NON DÉTERMINABLE` aujourd'hui (D4) |
| **SEC-BRO-03** | Pas de démarrage broche si le vérin de desserrage est actionné, même une milliseconde. | `and` avec `not(drawbar-release)` |
| **SEC-BRO-04** | **Jamais d'air de desserrage tant que la broche tourne.** Condition : vitesse nulle **et** stable depuis ≥ T (temporisation mesurée, pas supposée). | Chronométrer la descente en vitesse réelle |
| **SEC-BRO-05** | La condition d'arrêt broche repose sur une **mesure**, pas sur la consigne. Consigne à 0 ≠ broche arrêtée. | Comparer consigne et retour au ralentissement |
| **SEC-BRO-08** | Le desserrage n'est possible qu'en position de changement (Z en hauteur sûre). | Forcer Z ailleurs → refus |
| **SEC-AIR-02** | **Perte d'air = état sûr** (serrage à ressort). L'outil reste serré, seul le desserrage devient impossible. Cet état doit être **affiché**, pas subi en silence. | Couper l'air, vérifier le message |
| **SEC-AIR-03** | L'air de desserrage n'est pas maintenu en permanence : `oneshot` à durée max. | Maintenir la commande |
| **SEC-AIR-05** | La sortie pneumatique passant par EtherCAT hérite des modes de défaillance du bus : retombée sur perte de trame (D19) **et** distributeur monostable (D8). Deux barrières, pas une. | Débrancher le câble EtherCAT |
| **SEC-AIR-06** | La sortie ne doit jamais pouvoir être forcée depuis `halcmd setp` ou un panneau PyVCP sans la chaîne d'autorisation. | Relecture du HAL |
| **SEC-AIR-07** | **Broche verticale** : tout desserrage suppose que la chute de l'outil est acceptable à cet instant. La machine ne peut pas le vérifier — c'est la procédure (SEC-DEG-01). | Procédure écrite |

**Matrice d'exclusion mutuelle** (les ✗ ne doivent jamais coexister) :

| | Broche tourne | Air desserrage | Axe en rapide |
|---|---|---|---|
| **Broche tourne** | — | ✗ | ok |
| **Air desserrage** | ✗ | — | ✗ |

### E — Mouvements d'axe

| ID | Invariant | Vérification |
|---|---|---|
| **SEC-AXE-01** | Pas de mouvement automatique (programme, MDI) avant homing complet (`motion.is-all-homed`). | Lancer un programme non homé |
| **SEC-AXE-02** | Limites logicielles cohérentes avec la course réelle. *Constat : ±10000 mm + `HOME_IGNORE_LIMITS=YES` ⇒ inactives.* | `scripts/set_limits.py`, puis test d'approche |
| **SEC-AXE-03** | Fins de course matériels présents et **indépendants** des capteurs de home. *Constat : X seul.* | Débrancher un fin de course |
| **SEC-AXE-05** | `FERROR`/`MIN_FERROR` réglés pour détecter un obstacle avant la casse, sur **chaque** joint. *Constat : 500/50 mm ⇒ inactifs.* | Blocage doux à vitesse réduite |

### F — Enceinte (à terme, cf. D7)

| ID | Invariant |
|---|---|
| **SEC-ENC-01** | Porte ouverte → broche inhibée, ou vitesse limitée, ou feed hold. Politique à choisir explicitement. |
| **SEC-ENC-02** | Fermer la porte ne relance rien automatiquement. |

---

