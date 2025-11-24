#!/bin/bash
# Script de démarrage fiable pour les drives LC10E sur EtherCAT
# Assure que les drives passent en état OP avant de lancer LinuxCNC

ETHERCAT_CMD="/usr/local/etherlab/bin/ethercat"
MAX_RETRIES=5
RETRY_DELAY=2

echo "========================================="
echo "  Démarrage EtherCAT pour LC10E Drives"
echo "========================================="

# Fonction pour vérifier l'état des slaves
check_slaves_state() {
    local expected_state=$1
    $ETHERCAT_CMD slaves 2>/dev/null | grep -q "$expected_state"
    return $?
}

# Fonction pour compter les slaves en OP
count_op_slaves() {
    $ETHERCAT_CMD slaves 2>/dev/null | grep -c "OP"
}

# 1. Arrêter le master EtherCAT s'il tourne déjà
echo "📍 Étape 1: Arrêt du master EtherCAT existant..."
sudo systemctl stop ethercat 2>/dev/null
sleep 1

# 2. Démarrer le master EtherCAT
echo "📍 Étape 2: Démarrage du master EtherCAT..."
sudo systemctl start ethercat

# Attendre que le service démarre
sleep 2

# 3. Vérifier que les slaves sont détectés
echo "📍 Étape 3: Détection des slaves..."
for i in $(seq 1 $MAX_RETRIES); do
    SLAVE_COUNT=$($ETHERCAT_CMD slaves 2>/dev/null | wc -l)
    
    if [ "$SLAVE_COUNT" -ge 2 ]; then
        echo "✅ $SLAVE_COUNT slaves détectés"
        break
    else
        echo "⏳ Tentative $i/$MAX_RETRIES - $SLAVE_COUNT slaves détectés (attendu: 2+)"
        if [ $i -eq $MAX_RETRIES ]; then
            echo "❌ ERREUR: Impossible de détecter les slaves après $MAX_RETRIES tentatives"
            echo "   Vérifiez:"
            echo "   - Les câbles EtherCAT"
            echo "   - L'alimentation des drives"
            echo "   - L'état du service: sudo systemctl status ethercat"
            exit 1
        fi
        sleep $RETRY_DELAY
    fi
done

# 4. Passer les slaves en état PREOP -> SAFEOP -> OP
echo "📍 Étape 4: Transition des slaves vers l'état OP..."

for i in $(seq 1 $MAX_RETRIES); do
    # Forcer l'état INIT d'abord (reset propre)
    echo "   → INIT state..."
    sudo $ETHERCAT_CMD states -p0 INIT 2>/dev/null
    sudo $ETHERCAT_CMD states -p1 INIT 2>/dev/null
    sleep 0.5
    
    # PREOP
    echo "   → PREOP state..."
    sudo $ETHERCAT_CMD states -p0 PREOP 2>/dev/null
    sudo $ETHERCAT_CMD states -p1 PREOP 2>/dev/null
    sleep 0.5
    
    # SAFEOP
    echo "   → SAFEOP state..."
    sudo $ETHERCAT_CMD states -p0 SAFEOP 2>/dev/null
    sudo $ETHERCAT_CMD states -p1 SAFEOP 2>/dev/null
    sleep 0.5
    
    # OP
    echo "   → OP state..."
    sudo $ETHERCAT_CMD states -p0 OP 2>/dev/null
    sudo $ETHERCAT_CMD states -p1 OP 2>/dev/null
    sleep 1
    
    # Vérifier combien de slaves sont en OP
    OP_COUNT=$(count_op_slaves)
    
    if [ "$OP_COUNT" -ge 2 ]; then
        echo "✅ $OP_COUNT slaves en état OP"
        break
    else
        echo "⚠️  Tentative $i/$MAX_RETRIES - Seulement $OP_COUNT slaves en OP"
        
        if [ $i -eq $MAX_RETRIES ]; then
            echo "❌ ERREUR: Impossible de mettre tous les slaves en OP"
            echo ""
            echo "État actuel des slaves:"
            $ETHERCAT_CMD slaves
            echo ""
            echo "Diagnostics:"
            $ETHERCAT_CMD slaves -v
            exit 1
        fi
        
        # Attendre avant de réessayer
        sleep $RETRY_DELAY
    fi
done

# 5. Afficher l'état final
echo ""
echo "========================================="
echo "  ✅ Démarrage EtherCAT réussi !"
echo "========================================="
echo ""
echo "État des slaves:"
$ETHERCAT_CMD slaves
echo ""

# 6. Afficher les informations de diagnostic
echo "Informations DC (Distributed Clocks):"
$ETHERCAT_CMD slaves -p0 -v | grep -A 5 "DC"
echo ""

# 7. Optionnel: configurer les SDOs critiques
echo "📍 Étape 5: Configuration des SDOs (si nécessaire)..."

# Exemple: Configurer le watchdog timeout (0x1C32:02 = SM2 Watchdog, 0x1C33:02 = SM3 Watchdog)
# Décommentez si vous voulez configurer le watchdog
# sudo $ETHERCAT_CMD download -p0 -t uint16 0x1C32 0x02 1000  # 1000ms watchdog
# sudo $ETHERCAT_CMD download -p1 -t uint16 0x1C32 0x02 1000

echo "✅ Configuration terminée"
echo ""
echo "Vous pouvez maintenant lancer LinuxCNC"
echo ""

exit 0
