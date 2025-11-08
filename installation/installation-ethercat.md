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

## 10. Notes de version

| Date | Action | Notes |
|------|--------|-------|
| 2025-11-09 | Installation initiale | EtherCAT Master + LCEC + CIA402 sur Raspberry Pi |
| 2025-11-09 | Création du patch | Chemins EtherLab pour compilation LCEC |

---

**Auteur** : Documentation générée lors de l'installation  
**Licence** : Les composants utilisent diverses licences open-source (GPL, LGPL)
