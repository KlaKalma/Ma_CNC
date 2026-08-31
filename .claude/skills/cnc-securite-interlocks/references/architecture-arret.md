# Architecture d'arrêt, desserrage pneumatique et mode dégradé

Référence de `cnc-securite-interlocks`. À lire quand la question porte sur
l'arrêt d'urgence, la perte de bus, les freins, le desserrage d'outil ou la
coupure secteur — pas à charger systématiquement.

---

## Architecture d'arrêt — trois lignes de défense

Ni le VFD ni les servos n'ont de STO, aucun contacteur n'est réalisable.
L'architecture est **délibérément** en couches logicielles et bus, avec la
coupure secteur en second plan. C'est un choix assumé : ne le remets pas en
cause à chaque revue.

| Ligne | Couvre | Mécanisme | Statut |
|---|---|---|---|
| **1 — E-stop logiciel** | Erreur de programme, mauvaise trajectoire, geste opérateur. **Le cas dominant, de très loin.** | LinuxCNC désactive drives et broche via EtherCAT | ✅ fonctionnel |
| **2 — Réaction autonome du bus** | PC figé, processus mort, câble débranché, esclave muet | Watchdog SM des bornes Beckhoff (D19) + réaction perte de comm des variateurs (D17) + frein Z à manque de courant | ⚠️ **conçu, non testé** |
| **3 — Arrêt général** | Tout le reste | Coupure secteur manuelle, rotation délibérée | ✅ fonctionnel |

La ligne 1 traite l'écrasante majorité des cas réels et elle marche. La ligne 2
existe en conception. La ligne 3 est le filet. **Il n'y a donc pas de « lacune »
à marteler** : il y a une ligne 2 jamais vérifiée, et c'est tout.

Ne recommande pas d'ajouter du matériel tant que la ligne 2 n'a pas été testée.
Vérifier trois barrières existantes coûte une après-midi ; en ajouter une
quatrième non testée n'améliore rien.

### Ce qui rendrait la ligne 2 réelle (par effort croissant)

**a. Réaction autonome des variateurs — gratuit.** Les LC10E savent s'arrêter
seuls si on les configure. SDO à poser dans `ethercat-conf.xml` :

| Objet | Rôle | Réglage visé |
|---|---|---|
| `0x6007` | Abort connection option code (perte de comm) | `2` quick stop, ou `1` disable voltage selon l'axe |
| `0x605E` | Fault reaction option code | arrêt sur rampe puis désactivation |
| `0x605A` | Quick stop option code | rampe d'arrêt d'urgence |
| `0x6085` | Quick stop deceleration | décélération max supportée par la mécanique |
| Watchdog SM | Retombée autonome des sorties | activé, timeout court |

Recette : **débrancher le câble EtherCAT pendant un mouvement lent.** Les axes
doivent s'arrêter d'eux-mêmes. Tant que ce test n'est pas fait, considère la
ligne 2 comme absente.

**b. Contacts câblés en série sur les sorties critiques — peu coûteux.** Les
bornes Beckhoff pilotent des fils physiques : insérer un contact NF de relais de
sécurité en série suffit. Le bus commande, le contact câblé a le dernier mot.

| Fil | Effet | Temporisation |
|---|---|---|
| Sortie → entrée marche du VFD | Broche arrêtée même si le bus écrit encore 1 | immédiat |
| Sortie → desserrage frein Z | Le frein serre, la tête ne descend pas | **0,5–1 s** pour laisser l'axe décélérer |
| Sortie → distributeur de desserrage d'outil | Desserrage impossible sous E-stop | immédiat |

Un relais de sécurité du commerce avec contacts instantanés **et** temporisés
(Pilz PNOZ, Siemens 3SK, Schneider XPS ou équivalent) fournit tout ça.

**c. Contacteur en amont des variateurs — abandonné** (D21, impossible ici).
Conséquence permanente : **toute intervention en zone de travail se fait
sectionneur ouvert** (SEC-EST-07), sans échéance de levée. La coupure secteur
manuelle reste le dernier recours réel, doit être atteignable sans entrer en
zone de travail, et son usage s'enseigne — ce n'est pas un dispositif qu'on
n'utilise jamais.

### Coupure brutale du Raspberry Pi (SEC-EST-09)

L'arrêt général coupe l'alimentation du Pi sans arrêt propre.

1. **Règle absolue :** on n'hésite jamais à couper pour protéger une carte SD.
   Le matériel est remplaçable. Aucune consigne de ce document ne doit produire
   une seconde d'hésitation.
2. **Réduire le coût de la coupure** pour que la règle 1 reste facile : racine en
   lecture seule, démarrage sur SSD, ou alimentation secourue du Pi (HAT UPS /
   supercondensateur). Cette dernière est la meilleure — le Pi survit, la machine
   est morte, la reprise est immédiate (D22).
3. **Documenter la reprise** : intégrité du système de fichiers, config intacte,
   fichier `linuxcnc.var` (LinuxCNC l'écrit à l'arrêt — une coupure en plein
   travail peut perdre les derniers décalages). La config est un **dépôt git** :
   commiter après chaque modification validée.

**SEC-EST-10** — Après coupure brutale la position survit (encodeurs absolus),
mais l'état du serrage d'outil reste inconnu et les décalages pièce peuvent
avoir été perdus. Reprise : vérification manuelle de l'outil, contrôle des
décalages actifs, puis **vérification sur un point de référence connu** avant le
premier mouvement automatique.

**SEC-ABS-01** — La batterie d'encodeur est un **point de défaillance unique**
qui détruit silencieusement la référence machine. Trois exigences : mapper le
bit batterie faible / alarme encodeur en PDO et l'afficher (D24) ; remplacer la
batterie **sous tension** selon la procédure constructeur ; sauvegarder les
`HOME_OFFSET` hors machine — ils sont la seule chose qui relie la valeur
encodeur au zéro machine, et ils vont dans le dépôt git avec date et méthode de
mesure (D25).

**SEC-EST-08** — Champignon et arrêt général sont bien différenciés par leur
geste (frapper vs tourner) : pas de risque de confusion. Le problème est
inverse : **le geste réflexe agit sur le dispositif qui ne coupe rien, et le
seul qui coupe vraiment demande un geste lent.** Réflexe à entraîner et à
écrire : champignon d'abord ; si la broche continue de tourner ou si un axe
bouge encore, rotation immédiate de l'arrêt général. Points à vérifier sur
l'arrêt général : manœuvrable **d'une seule main sans regarder**, sens de
rotation intuitif, et **cadenassable** — ce dernier point conditionne la
consignation (SEC-EST-07).

**SEC-BRK-01** — Au démarrage du PC et de LinuxCNC, la sortie de desserrage du
frein doit être à 0. Un `setp` malencontreux ou une pin de panneau par défaut à
1 fait tomber la tête au boot. Trois chemins d'inversion à surveiller : un `setp`
dans le HAL, une pin PyVCP par défaut à 1, un remapping PDO qui déplace le bit.

**SEC-BRK-02** — Le desserrage du frein n'est autorisé que si le variateur
concerné est en `Operation enabled` et tient la position. Ordre : enable →
confirmation → frein desserré ; à l'arrêt : frein serré → confirmation →
disable. *État actuel : non respecté, voir le tableau des constats.*

---

## 🔴 Desserrage pneumatique — scénario dimensionnant

Avec un serrage à ressort, le danger n'est plus la perte d'air mais
**l'application du desserrage pendant que la broche tourne** : l'empilage
Belleville est comprimé, la force de serrage chute, l'outil peut être éjecté.
La commande passant par EtherCAT, ce scénario n'est pas hypothétique.

| ID | Barrière | Nature |
|---|---|---|
| **SEC-PNE-01** | **Distributeur monostable, repos = pas de desserrage.** Bobine hors tension, bus mort, PC éteint → l'outil reste serré. Un bistable est **interdit** sur cette fonction. (D8 : monostable confirmé) | Pneumatique |
| **SEC-PNE-02** | **Watchdog SM activé** sur l'esclave portant cette sortie : perte de trame → la sortie retombe seule, sans le PC. (D19) | Bus |
| **SEC-PNE-03** | Commande **impulsionnelle à durée maximale** (`oneshot`) : pas de vérin laissé sous pression indéfiniment. | HAL |
| **SEC-PNE-04** | Aucun chemin permettant de forcer cette sortie depuis `halcmd setp` ou un panneau PyVCP sans passer par la chaîne d'autorisation. Pas de pin de test qui court-circuite la logique. | Relecture HAL |
| **SEC-PNE-05** | Perte de `tool-present` alors que la broche tourne → **E-stop immédiat**, anti-rebond court, aucune temporisation. | Capteur + HAL |
| **SEC-PNE-06** | Le poussoir de desserrage encaisse-t-il un contact rotatif accidentel ? Sur beaucoup de montages, une application en rotation détruit la butée. À documenter comme limite d'emploi. (D15) | Mécanique |

Un pressostat reste utile — un desserrage à pression insuffisante ne libère que
partiellement l'outil — mais c'est une protection **machine**, pas personne.

## 🔴 Mode dégradé — instrumentation actuelle (présence d'outil seule)

Un capteur de présence ne dit pas si l'outil est **serré**. Les invariants
SEC-BRO-02, SEC-BRO-06, SEC-ATC-05 et SEC-AIR-01 restent **non satisfiables** :
tu les marques `NON DÉTERMINABLE — capteur absent`, jamais OK.

| ID | Règle en mode dégradé |
|---|---|
| **SEC-DEG-01** | **Changement d'outil manuel et supervisé.** Pas de remap M6 automatique, pas de séquence déclenchée par le programme. Le G-code s'arrête et attend une action humaine. Renforcé par D3 (broche non orientable). |
| **SEC-DEG-02** | La seule condition disponible avant desserrage est une **temporisation mesurée** depuis l'ordre d'arrêt broche : pire cas (outil le plus lourd, vitesse max, arrêt en roue libre) **+ 100 % de marge**. Tant que D14 n'est pas tranché, l'indicateur VFD s'y ajoute, il ne la remplace pas. |
| **SEC-DEG-03** | Desserrage sur **action délibérée à deux temps** (maintien prolongé ou deux commandes distinctes). Jamais un bouton simple, jamais depuis le pendant sans confirmation. |
| **SEC-DEG-04** | Desserrage inhibé en dur dès que `spindle.0.on` est ou a été vrai dans les T dernières secondes, indépendamment de toute vitesse supposée. |
| **SEC-DEG-05** | L'IHM affiche **« serrage non contrôlé »**, jamais « outil serré ». `tool_in_spindle` est une croyance, pas une mesure. Le capteur de présence peut être affiché en le nommant correctement : *présence*, pas *serrage*. |
| **SEC-DEG-06** | Après E-stop, perte de bus, coupure ou reboot : état du serrage **inconnu**. Vérification manuelle avant toute remise en rotation. |
| **SEC-DEG-07** | La sortie de desserrage retombe à l'état repos (serré) sur : machine-off, E-stop, perte EtherCAT, arrêt de LinuxCNC, boot du PC. À vérifier **au multimètre sur le distributeur**, pas dans HAL. |
| **SEC-DEG-08** | Pas de rotation immédiatement après un desserrage : temporisation + acquittement humain, puisque rien ne confirme le reserrage. |
| **SEC-DEG-09** | Perte de `tool-present` en rotation → **E-stop immédiat** (cf. SEC-PNE-05). |
| **SEC-DEG-10** | Toute intervention manuelle en zone de travail se fait **machine hors tension, sectionneur ouvert** — pas en E-stop. Voir SEC-EST-07. |

**Améliorations, par priorité :**
1. **Tester la ligne 2** — SDO de réaction perte de comm (D17) et watchdog SM
   des bornes (D19). Gratuit, une après-midi. **Le seul travail réellement
   prioritaire.**
2. Surveillance de l'alarme batterie encodeur (D24) et sauvegarde des
   `HOME_OFFSET` (D25).
3. Capteur de position du poussoir de desserrage (deux contacts : serré /
   desserré) → rend SEC-BRO-02 satisfiable.
4. Détection de fréquence nulle réelle sur le VFD, si D14 révèle un simple écho.
5. Pressostat — protection machine.

---

