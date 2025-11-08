# Installation réussie d'EtherCAT, LCEC et CIA402

## Date : 9 novembre 2025

## Résumé

Installation complète et réussie des modules nécessaires pour utiliser EtherCAT avec LinuxCNC.

## Modules installés

### 1. EtherCAT Master (IgH EtherLab)
- **Statut** : ✅ Installé et fonctionnel
- **Localisation** : `/usr/local/etherlab/`
- **Modules kernel** : `ec_master`, `ec_generic`
- **Outil** : `/usr/local/etherlab/bin/ethercat`
- **Vérification** :
  ```bash
  lsmod | grep ec_
  # Résultat : ec_master et ec_generic sont chargés
  ```

### 2. LCEC (LinuxCNC EtherCAT)
- **Statut** : ✅ Compilé et installé
- **Module** : `/usr/lib/linuxcnc/modules/lcec.so`
- **Utilitaire** : `/usr/bin/lcec_conf`
- **Taille** : 707 KB

### 3. CIA402 (Driver interface)
- **Statut** : ✅ Compilé et installé
- **Module** : `/usr/lib/linuxcnc/modules/cia402.so`
- **Taille** : 91 KB
- **Source** : `/home/cnc/linuxcnc/configs/Ma_CNC/cia402.comp`

## Modifications apportées

### Fichiers modifiés pour la compilation de LCEC :

1. **`/home/cnc/linuxcnc-ethercat/src/Kbuild`**
   - Ajout : `ccflags-y := -I/usr/local/etherlab/include`

2. **`/home/cnc/linuxcnc-ethercat/src/user.mk`**
   - Ajout des chemins d'inclusion et de bibliothèque EtherCAT
   - Flags : `-I/usr/local/etherlab/include` et `-L/usr/local/etherlab/lib`

3. **`/home/cnc/linuxcnc-ethercat/src/realtime.mk`**
   - Ajout des chemins d'inclusion et de bibliothèque EtherCAT
   - Configuration des LDFLAGS avec rpath

## Configuration actuelle

### Fichiers de configuration LinuxCNC

- **Configuration** : `/home/cnc/linuxcnc/configs/Ma_CNC/`
- **INI** : `LC10E.ini`
- **HAL** : `LC10E.hal`
- **XML Device** : `LC10E_vendeur.xml` (Shenzhen Xinchuan LC10E V1.11)

### Configuration HAL actuelle

Le fichier `LC10E.hal` contient déjà :
- Chargement de lcec avec `lcec_conf`
- Configuration CIA402 pour 1 axe
- Thread servo correctement configuré

## Étapes suivantes

### 1. Vérifier votre interface réseau EtherCAT
```bash
ip link show
# Identifiez votre interface Ethernet pour EtherCAT
```

### 2. Configurer l'interface dans EtherCAT Master
```bash
sudo /usr/local/etherlab/bin/ethercat slaves
# Doit afficher vos périphériques EtherCAT connectés
```

### 3. Tester la configuration LinuxCNC
```bash
cd /home/cnc/linuxcnc/configs/Ma_CNC
linuxcnc LC10E.ini
```

## Commandes utiles

### Vérifier l'état EtherCAT
```bash
# Voir les esclaves connectés
sudo /usr/local/etherlab/bin/ethercat slaves

# Voir l'état du master
sudo /usr/local/etherlab/bin/ethercat master

# Voir les domaines
sudo /usr/local/etherlab/bin/ethercat domains
```

### Démarrer/Arrêter EtherCAT
```bash
# Démarrer (scripts sur le bureau)
~/Desktop/start-ethercat.sh

# Arrêter
~/Desktop/stop-ethercat.sh
```

### Déboguer LCEC
```bash
# Voir les logs LinuxCNC
dmesg | grep -i ethercat
dmesg | grep -i lcec

# Tester lcec_conf
lcec_conf -h
```

## Ressources

- **Documentation EtherLab** : https://etherlab.org/
- **LCEC GitHub** : https://github.com/linuxcnc-ethercat/linuxcnc-ethercat
- **CIA402 Standard** : Profile for drives and motion control
- **LinuxCNC Docs** : https://linuxcnc.org/docs/

## Notes importantes

1. **LCEC nécessite** :
   - EtherCAT Master (IgH) installé
   - LinuxCNC avec support temps réel ou uspace
   - Fichier XML de description du périphérique

2. **CIA402 Mode** :
   - Par défaut : CSP (Cyclic Synchronous Position)
   - Alternative : CSV (Cyclic Synchronous Velocity)
   - Configurable via `setp cia402.0.csp-mode 0`

3. **Ordre de chargement HAL** :
   ```
   lcec.read-all → cia402.read-all → motion → cia402.write-all → lcec.write-all
   ```

## Problèmes résolus

- ✅ Fichiers d'en-tête EtherCAT (`ecrt.h`) non trouvés → Chemins ajoutés dans Makefiles
- ✅ Bibliothèque EtherCAT non liée → LDFLAGS configurés avec rpath
- ✅ CIA402 non disponible → Compilé depuis source avec halcompile

## Support

Pour toute question ou problème :
1. Vérifiez les logs avec `dmesg`
2. Consultez `/var/log/linuxcnc.log`
3. Vérifiez l'état EtherCAT avec les commandes ci-dessus
