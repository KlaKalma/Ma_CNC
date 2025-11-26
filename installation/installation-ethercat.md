# Installation EtherCAT, LCEC et CIA402 pour LinuxCNC

**Date** : 9 novembre 2025  
**Système** : Raspberry Pi / Linux avec LinuxCNC

---

## Vue d'ensemble

Ce guide documente l'installation complète d'un système EtherCAT pour LinuxCNC, incluant :
- **EtherCAT Master** (IgH EtherLab)
- **LCEC** (LinuxCNC EtherCAT HAL driver)
- **CIA402** (Interface standard pour servomoteurs)

---

## Prérequis

- LinuxCNC installé (version uspace ou temps réel)
- Accès root/sudo
- Carte réseau Ethernet pour EtherCAT
- Git installé

---

## 1. Installation d'EtherCAT Master (IgH EtherLab)

### 1.1 Cloner et configurer

```bash
cd ~
git clone https://gitlab.com/etherlab.org/ethercat.git
cd ethercat
./bootstrap
./configure --prefix=/usr/local/etherlab --disable-8139too --enable-generic
```

### 1.2 Compiler et installer

```bash
make
sudo make install
```

### 1.3 Charger les modules kernel

```bash
sudo modprobe ec_master
sudo modprobe ec_generic
```

### 1.4 Vérifier l'installation

```bash
lsmod | grep ec_
# Devrait afficher : ec_master et ec_generic

ls -l /usr/local/etherlab/bin/ethercat
# L'outil en ligne de commande doit exister
```

---

## 2. Installation de LCEC (LinuxCNC EtherCAT)

### 2.1 Cloner le repository

```bash
cd ~
git clone https://github.com/linuxcnc-ethercat/linuxcnc-ethercat.git
cd linuxcnc-ethercat
```

### 2.2 Appliquer le patch pour les chemins EtherLab

⚠️ **Important** : LCEC a besoin de connaître le chemin d'installation d'EtherLab Master.

**Appliquer le patch** :
```bash
git apply ~/installation/linuxcnc-ethercat-paths.patch
```

**OU modifier manuellement** les 3 fichiers suivants :

#### Fichier `src/Kbuild`
Ajouter au début du fichier :
```makefile
ccflags-y := -I/usr/local/etherlab/include

```

#### Fichier `src/user.mk`
Après la ligne `EXTRA_CFLAGS := $(filter-out -Wframe-larger-than=%,$(EXTRA_CFLAGS))`, ajouter :
```makefile
EXTRA_CFLAGS += -I/usr/local/etherlab/include
```

Et modifier la ligne `lcec_conf:` pour :
```makefile
lcec_conf: $(LCEC_CONF_OBJS)
	$(CC) -o $@ $(LCEC_CONF_OBJS) -Wl,-rpath,$(LIBDIR) -Wl,-rpath,/usr/local/etherlab/lib -L$(LIBDIR) -L/usr/local/etherlab/lib -llinuxcnchal -lexpat
```

#### Fichier `src/realtime.mk`
Après la ligne `include $(MODINC)`, ajouter :
```makefile
EXTRA_CFLAGS += -I/usr/local/etherlab/include
```

Et modifier les lignes `LDFLAGS` et `EXTRA_LDFLAGS` :
```makefile
ifeq ($(MODINC_HAS_EXTRA_LDFLAGS),y)
  LDFLAGS += -Wl,-rpath,$(LIBDIR) -Wl,-rpath,/usr/local/etherlab/lib
  EXTRA_LDFLAGS += -L$(LIBDIR) -L/usr/local/etherlab/lib -llinuxcnchal -lethercat -lrt
else
  LDFLAGS += -Wl,-rpath,$(LIBDIR) -Wl,-rpath,/usr/local/etherlab/lib -L$(LIBDIR) -L/usr/local/etherlab/lib -llinuxcnchal -lethercat
endif
```

### 2.3 Compiler LCEC

```bash
make clean
make
```

Si tout compile correctement, vous devriez voir :
```
Compiling realtime lcec_main.c
Compiling realtime lcec_class_enc.c
...
Linking lcec.so
```

### 2.4 Installer LCEC

```bash
sudo make install
```

### 2.5 Vérifier l'installation

```bash
ls -lh /usr/lib/linuxcnc/modules/lcec.so
# Devrait afficher le module (~707 KB)

which lcec_conf
# Devrait afficher : /usr/bin/lcec_conf
```

---

## 3. Installation de CIA402

### 3.1 Localiser le fichier source

Le fichier `cia402.comp` se trouve normalement dans votre configuration LinuxCNC :
```bash
ls ~/linuxcnc/configs/Ma_CNC/cia402.comp
```

### 3.2 Compiler et installer

```bash
cd ~/linuxcnc/configs/Ma_CNC
sudo halcompile --install cia402.comp
```

### 3.3 Vérifier l'installation

```bash
ls -lh /usr/lib/linuxcnc/modules/cia402.so
# Devrait afficher le module (~91 KB)
```

---

## 4. Configuration LinuxCNC

### 4.1 Structure des fichiers

Votre configuration LinuxCNC doit contenir :
```
~/linuxcnc/configs/Ma_CNC/
├── LC10E.ini              # Configuration principale
├── LC10E.hal              # Configuration HAL
├── LC10E_vendeur.xml      # Description du device EtherCAT
├── cia402.comp            # Source du composant CIA402
├── pyvcp_panel.xml        # Interface utilisateur (optionnel)
└── pyvcp_panel.hal        # HAL pour PyVCP (optionnel)
```

### 4.2 Fichier INI (LC10E.ini)

Section critique :
```ini
[EMCMOT]
EMCMOT = lcec
SERVO_PERIOD = 1000000
```

### 4.3 Fichier HAL (LC10E.hal)

Ordre de chargement **très important** :
```hal
# 1. Load Kinematics et Motion
loadrt [KINS]KINEMATICS
loadrt [EMCMOT]EMCMOT servo_period_nsec=[EMCMOT]SERVO_PERIOD num_joints=1

# 2. Load EtherCAT
loadusr -W lcec_conf LC10E_vendeur.xml
loadrt lcec

# 3. Load CIA402
loadrt cia402 count=1

# 4. Servo thread - ORDRE CRUCIAL !
addf lcec.read-all         servo-thread  # Lire EtherCAT
addf cia402.0.read-all     servo-thread  # Traiter CIA402
addf motion-command-handler servo-thread  # Motion
addf motion-controller      servo-thread  # Motion
addf cia402.0.write-all    servo-thread  # Préparer commandes CIA402
addf lcec.write-all        servo-thread  # Écrire EtherCAT
```

### 4.4 Fichier XML du périphérique

Le fichier `LC10E_vendeur.xml` doit décrire votre servomoteur EtherCAT (ProductCode, PDO mapping, etc.).

---

## 5. Tests et vérification

### 5.1 Vérifier les esclaves EtherCAT

```bash
sudo /usr/local/etherlab/bin/ethercat slaves
```

**Sans périphérique connecté** : liste vide
**Avec périphérique(s)** : 
```
0  0:0  PREOP  +  LC10E V1.11
```

### 5.2 Tester la configuration LinuxCNC

```bash
cd ~/linuxcnc/configs/Ma_CNC
linuxcnc LC10E.ini
```

### 5.3 Vérifier les modules chargés

Pendant l'exécution de LinuxCNC :
```bash
halcmd show comp | grep -E "lcec|cia402"
```

---

## 6. Gestion des modifications (Git)

### ⚠️ Important sur le repo linuxcnc-ethercat

Le dossier `~/linuxcnc-ethercat` est un clone d'un repository Git externe. Les modifications apportées pour les chemins EtherLab sont **spécifiques à votre installation** et ne doivent **pas** être committées dans ce repo.

### Stratégie recommandée

1. **Laisser les modifications non committées** dans `~/linuxcnc-ethercat`
2. **Conserver le patch** dans `~/installation/linuxcnc-ethercat-paths.patch`
3. **En cas de mise à jour** du repo :
   ```bash
   cd ~/linuxcnc-ethercat
   git stash                    # Sauvegarder les modifications
   git pull                     # Mettre à jour
   git stash pop                # Réappliquer les modifications
   # OU
   git apply ~/installation/linuxcnc-ethercat-paths.patch
   ```

---

## 7. Dépannage

### Erreur : "ecrt.h: No such file or directory"

→ Les chemins vers EtherLab ne sont pas configurés. Appliquez le patch.

### Erreur : "cannot find -lethercat"

→ La bibliothèque EtherCAT n'est pas trouvée. Vérifiez :
```bash
ls /usr/local/etherlab/lib/libethercat*
```

### LinuxCNC ne démarre pas

1. Vérifier les logs :
   ```bash
   dmesg | tail -50
   cat /var/log/linuxcnc.log
   ```

2. Tester HAL manuellement :
   ```bash
   halrun
   halcmd: loadrt lcec
   halcmd: show comp
   ```

### EtherCAT ne trouve pas les périphériques

1. Vérifier l'interface réseau :
   ```bash
   ip link show
   ```

2. Vérifier que les modules sont chargés :
   ```bash
   lsmod | grep ec_
   ```

3. Configurer l'interface dans `/usr/local/etherlab/etc/ethercat.conf`

---

## 8. Ressources

- **EtherLab Master** : https://gitlab.com/etherlab.org/ethercat
- **LCEC GitHub** : https://github.com/linuxcnc-ethercat/linuxcnc-ethercat
- **LinuxCNC Docs** : https://linuxcnc.org/docs/
- **CIA402 Standard** : CAN in Automation - Profile for drives and motion control

---

## 9. Résumé des commandes d'installation

```bash
# EtherCAT Master
cd ~ && git clone https://gitlab.com/etherlab.org/ethercat.git
cd ethercat
./bootstrap
./configure --prefix=/usr/local/etherlab --disable-8139too --enable-generic
make && sudo make install
sudo modprobe ec_master ec_generic

# LCEC
cd ~ && git clone https://github.com/linuxcnc-ethercat/linuxcnc-ethercat.git
cd linuxcnc-ethercat
git apply ~/installation/linuxcnc-ethercat-paths.patch
make && sudo make install

# CIA402
cd ~/linuxcnc/configs/Ma_CNC
sudo halcompile --install cia402.comp

# Vérification
ls -l /usr/lib/linuxcnc/modules/{lcec,cia402}.so
which lcec_conf
sudo /usr/local/etherlab/bin/ethercat slaves
```

---

## 10. Ajout d'un nouveau drive EtherCAT

Lorsque vous ajoutez un nouveau servo/drive à votre chaîne EtherCAT, il peut avoir une **configuration PDO persistante** incompatible qui empêche LinuxCNC de le configurer correctement.

### 10.1 Symptômes

- Le nouveau drive reste en **PREOP** avec erreur (flag E)
- Logs EtherCAT montrent :
  ```
  EtherCAT WARNING 0-X: Failed to configure mapping of PDO 0x1600
  EtherCAT ERROR 0-X: AL status message 0x001E: "Invalid input configuration"
  ```
- Le drive refuse la configuration même si elle est identique à celle déjà mappée

### 10.2 Solution : Réinitialiser les PDO assignments

**Important** : Arrêter LinuxCNC avant d'exécuter ces commandes !

#### Étape 1 : Identifier la position du nouveau drive

```bash
sudo /usr/local/etherlab/bin/ethercat slaves
# Exemple de sortie :
# 0  0:0  OP     +  LC10E_V1.04  (ancien drive qui fonctionne)
# 1  0:1  PREOP  E  LC10E_V1.04  (nouveau drive avec erreur)
```

#### Étape 2 : Réinitialiser les PDOs du nouveau drive UNIQUEMENT

Remplacez `-p1` par la position de votre nouveau drive (0, 1, 2, etc.) :

```bash
# Désactiver RxPDO assignment (Master → Slave)
sudo /usr/local/etherlab/bin/ethercat download -p1 -t uint8 0x1c12 0 0

# Désactiver TxPDO assignment (Slave → Master)
sudo /usr/local/etherlab/bin/ethercat download -p1 -t uint8 0x1c13 0 0
```

**Explication** :
- `0x1c12` = RxPDO Assignment (commandes vers le drive)
- `0x1c13` = TxPDO Assignment (feedback du drive)
- Subindex `0` mis à `0` = désactive tous les PDOs assignés

#### Étape 3 : Vérifier l'état

```bash
sudo /usr/local/etherlab/bin/ethercat slaves
# Le nouveau drive devrait être en PREOP sans erreur (pas de flag E)
```

#### Étape 4 : Relancer LinuxCNC

Le nouveau drive devrait maintenant accepter la configuration PDO définie dans votre `ethercat-conf.xml`.

### 10.3 ⚠️ Attention

**NE PAS** réinitialiser les PDOs d'un drive qui fonctionne déjà ! Cela le casserait et vous devriez le reconfigurer aussi.

Si vous réinitialisez accidentellement tous les drives :
1. Répétez les commandes de l'Étape 2 pour **chaque drive**
2. Relancez LinuxCNC - tous les drives seront reconfigurés

### 10.4 Alternative : Reset complet

Si les commandes SDO échouent, vous pouvez aussi :

1. **Éteindre physiquement le nouveau drive** (couper l'alimentation)
2. Attendre 5-10 secondes
3. **Rallumer le drive**
4. Relancer LinuxCNC

Cette méthode efface la mémoire volatile du drive, incluant les PDO assignments.

---

## 11. Problème aléatoire de PDO au démarrage

### 11.1 Symptôme

De manière **aléatoire**, un ou plusieurs drives restent en **PREOP** avec erreur lors du démarrage de LinuxCNC :

```bash
sudo /usr/local/etherlab/bin/ethercat slaves
# 0  0:0  OP     E  LC10E_V1.04
# 1  0:1  PREOP  E  LC10E_V1.04   ← Bloqué en PREOP
```

Les logs montrent :
```
EtherCAT WARNING 0-1: Failed to assign PDO 0x1600 at position 1 of SM2
EtherCAT ERROR 0-1: AL status message 0x001D: "Invalid output configuration"
```

### 11.2 Cause

Les drives LC10E **sauvegardent parfois les PDOs en mémoire persistante**. Lors du redémarrage, cette configuration entre en conflit avec celle définie dans `ethercat-conf.xml`.

Le SDO standard `0x1010` (Save Parameters) n'est pas supporté par les LC10E, donc on ne peut pas désactiver cette sauvegarde.

### 11.3 Solution : Reset PDOs automatique au démarrage

Ajouter le reset des PDOs dans le script `start-ethercat.sh` **après** la détection des esclaves :

```bash
# Reset persistent PDOs on all slaves to avoid configuration conflicts
echo ""
echo "=== Reset PDOs persistants ==="
for slave in 0 1; do
    sudo /usr/local/etherlab/bin/ethercat state -p$slave PREOP 2>/dev/null
done
sleep 0.5
for slave in 0 1; do
    sudo /usr/local/etherlab/bin/ethercat download -p$slave -t uint8 0x1c12 0 0 2>/dev/null
    sudo /usr/local/etherlab/bin/ethercat download -p$slave -t uint8 0x1c13 0 0 2>/dev/null
done
echo "PDOs reset - prêt pour LinuxCNC"
```

### 11.4 Explication des SDOs 0x1C12 et 0x1C13

Ces deux SDOs font partie du standard **CIA301** (CANopen over EtherCAT) et définissent quels PDOs sont actifs :

| SDO | Nom | Direction | Rôle |
|-----|-----|-----------|------|
| **0x1C12** | RxPDO Assignment | Master → Slave | Liste des PDOs de **commande** (ce qu'on envoie au drive) |
| **0x1C13** | TxPDO Assignment | Slave → Master | Liste des PDOs de **feedback** (ce qu'on reçoit du drive) |

#### Structure de ces SDOs

```
0x1C12:00 = Nombre de PDOs assignés (uint8)
0x1C12:01 = Index du 1er PDO (ex: 0x1600)
0x1C12:02 = Index du 2ème PDO (ex: 0x1601)
...

0x1C13:00 = Nombre de PDOs assignés (uint8)
0x1C13:01 = Index du 1er PDO (ex: 0x1A00)
0x1C13:02 = Index du 2ème PDO (ex: 0x1A01)
...
```

#### Ce que fait le reset

```bash
ethercat download -p0 -t uint8 0x1c12 0 0
#                              │     │ │
#                              │     │ └─ Valeur = 0 (aucun PDO)
#                              │     └─── Subindex 0 (nombre de PDOs)
#                              └───────── SDO RxPDO Assignment
```

En mettant le subindex 0 à **zéro**, on dit au drive : "oublie tous les PDOs configurés". 
Ensuite, quand LinuxCNC démarre, `lcec` reconfigure les PDOs selon `ethercat-conf.xml`.

#### Mnémotechnique

- **0x1C12** = "1C **R**eceive" → RxPDO (drive **reçoit** les commandes)
- **0x1C13** = "1C **T**ransmit" → TxPDO (drive **transmet** le feedback)

**Notes** :
- Adapter la boucle `for slave in 0 1` au nombre de drives (ex: `0 1 2` pour 3 drives)
- Le reset prend environ **0.5 seconde** - impact négligeable
- Les erreurs sont ignorées (`2>/dev/null`) car certains drives peuvent déjà être en bon état

### 11.4 Pourquoi c'est acceptable

Bien que ce soit un **workaround** et non une solution élégante :
- C'est **rapide** (~0.5s)
- C'est **fiable** - garantit un démarrage propre à chaque fois
- C'est **nécessaire** - les LC10E n'ont pas d'option pour désactiver la sauvegarde PDO
- C'est **non-destructif** - lcec reconfigurera les PDOs correctement au lancement

---

## 12. Notes de version

| Date | Action | Notes |
|------|--------|-------|
| 2025-11-09 | Installation initiale | EtherCAT Master + LCEC + CIA402 sur Raspberry Pi |
| 2025-11-09 | Création du patch | Chemins EtherLab pour compilation LCEC |
| 2025-11-24 | Ajout section nouveau drive | Procédure pour réinitialiser PDOs d'un nouveau servo |
| 2025-11-27 | Ajout reset PDO automatique | Workaround pour problème aléatoire de configuration PDO |

---

**Auteur** : Documentation générée lors de l'installation  
**Licence** : Les composants utilisent diverses licences open-source (GPL, LGPL)
