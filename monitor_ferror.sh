#!/bin/bash
# Script pour monitorer l'erreur de suivi pendant un test
# Usage: ./monitor_ferror.sh

echo "=== Monitoring Erreur de Suivi ==="
echo "Appuyez sur Ctrl+C pour arrêter"
echo ""
echo "Légende: ✓ < 0.5mm | ⚠ < 2mm | ✗ > 2mm"
echo ""

MAX_X=0
MAX_Y=0

while true; do
    # Lire les erreurs
    X_ERR=$(halcmd getp joint.0.f-error 2>/dev/null)
    Y_ERR=$(halcmd getp joint.1.f-error 2>/dev/null)
    
    # Calculer valeurs absolues
    X_ABS=$(echo "$X_ERR" | awk '{print ($1<0)?-$1:$1}')
    Y_ABS=$(echo "$Y_ERR" | awk '{print ($1<0)?-$1:$1}')
    
    # Tracker les max
    MAX_X=$(echo "$MAX_X $X_ABS" | awk '{print ($1>$2)?$1:$2}')
    MAX_Y=$(echo "$MAX_Y $Y_ABS" | awk '{print ($1>$2)?$1:$2}')
    
    # Status icons
    if (( $(echo "$X_ABS < 0.5" | bc -l) )); then X_STAT="✓"
    elif (( $(echo "$X_ABS < 2" | bc -l) )); then X_STAT="⚠"
    else X_STAT="✗"; fi
    
    if (( $(echo "$Y_ABS < 0.5" | bc -l) )); then Y_STAT="✓"
    elif (( $(echo "$Y_ABS < 2" | bc -l) )); then Y_STAT="⚠"
    else Y_STAT="✗"; fi
    
    printf "\rX: %s %+8.4f mm (max: %6.4f) | Y: %s %+8.4f mm (max: %6.4f)   " \
           "$X_STAT" "$X_ERR" "$MAX_X" "$Y_STAT" "$Y_ERR" "$MAX_Y"
    
    sleep 0.05
done
