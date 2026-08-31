# Déterminations — points non tranchés

Référence de `cnc-securite-interlocks`. **Liste de courses, pas questionnaire.**
Ne se déroule pas spontanément : sert au mode POINT SÉCURITÉ et à savoir quoi
marquer `NON DÉTERMINÉ`. L'utilisateur n'est pas en phase de trancher ces points.

---

Ce tableau est une **liste de courses, pas un questionnaire**. Tu ne le
déroules pas spontanément et tu ne relances pas dessus : il sert au mode POINT
SÉCURITÉ et à savoir quoi marquer `NON DÉTERMINÉ`.

| # | Question | Statut |
|---|---|---|
| D1 | Nature du serrage | ✅ ressorts Belleville + desserrage pneumatique |
| D2 | Type d'ATC | ⬜ ouvert — aucun ATC aujourd'hui |
| D3 | Orientation broche | ✅ non — VFD avec indicateur marche/arrêt |
| D4 | Capteurs présents | ✅ présence d'outil uniquement |
| D5 | Retour vitesse nulle fiable | ⚠️ à tester — voir D14 |
| D6 | STO / contacteur | ✅ aucun, ni broche ni axes |
| D7 | Enceinte / contact de porte | ✅ non pour l'instant, prévu à terme |
| D8 | Distributeur de desserrage | ✅ solénoïde monostable, non distribuant au repos |
| D10 | Temps de descente en vitesse **mesuré**, pire cas | ⬜ ouvert — dimensionne SEC-DEG-02 |
| D11 | État de la sortie de desserrage à machine-off / reset E-stop | ⬜ ouvert |
| D14 | L'indicateur VFD est-il une détection de fréquence nulle ou un écho de la commande ? | ⬜ ouvert — critique |
| D15 | Butée à billes en bout de vérin de desserrage, ou poussée directe sur le tirant ? | ⬜ ouvert |
| D16 | Frein de maintien Z | ✅ présent, sortie Beckhoff, à manque de courant |
| D17 | `0x6007`/`0x605E`/`0x605A`/`0x6085` configurés ? Test de débranchement fait ? | ⬜ ouvert — **barrière n°2 des axes** |
| D18 | Sens de sécurité du frein Z | ✅ à manque de courant — sortie à 0 = frein serré |
| D19 | Watchdog SM des bornes portant une sortie critique | ⬜ ouvert — **barrière n°1 des axes** |
| D20 | Fil borne Beckhoff → entrée marche VFD accessible pour un contact NF ? | ⬜ ouvert |
| D21 | Contacteur en amont des variateurs | ✅ non, impossible |
| D22 | Le Pi démarre-t-il sur SD ou SSD ? Racine en lecture seule ? Alim secourue ? | ⬜ ouvert |
| D23 | Encodeurs d'axe | ✅ absolus multi-tours, batterie externe |
| D24 | Bit batterie faible / alarme encodeur mappé en PDO et surveillé ? | ⬜ ouvert |
| D25 | `HOME_OFFSET` absolus sauvegardés hors machine ? | ⬜ ouvert |
| D26 | La broche expose-t-elle réellement les contacts serrage/desserrage des griffes ? Câblés ? | ⬜ ouvert — **le plus rentable** |

**Note sur D14** : beaucoup de VFD ont une sortie « RUN » qui recopie l'ordre de
marche, pas la rotation réelle. En arrêt roue libre, le VFD annonce « arrêté »
alors que la broche tourne encore plusieurs secondes. Test : broche à vitesse
max, coupure, chronométrer la bascule de la sortie **contre** l'arrêt visuel du
mandrin. Écart significatif → cette sortie ne peut pas servir d'autorisation de
desserrage ; seule une temporisation mesurée le peut.

---

