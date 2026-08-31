#!/usr/bin/env bash
# etat_config.sh -- imprime l'etat courant des reglages volatils de la config.
#
# Raison d'etre : les competences Claude (~/.claude/skills/) portent la METHODE
# et les pieges ; l'ETAT vit ici. Un skill qui code en dur "FERROR = 500" devient
# faux des que la valeur change, et un document confiant mais perime est pire
# qu'un document vague. Les skills appellent donc ce script au lieu de recopier
# des valeurs.
#
# Usage : scripts/etat_config.sh
set -u
cd "$(dirname "$0")/.." || exit 1

echo "=== ETAT CONFIG — $(date +%Y-%m-%d\ %H:%M) — $(git rev-parse --short HEAD 2>/dev/null || echo 'hors git') ==="

echo
echo "--- Cycle / temps reel ---"
grep -H 'SERVO_PERIOD'  LC10E.ini
grep -Ho 'appTimePeriod="[0-9]*"' ethercat-conf.xml
echo "  (les deux doivent etre egaux)"

echo
echo "--- Limites & securite de mouvement (par axe puis par joint) ---"
grep -HnE '^\s*(MIN_LIMIT|MAX_LIMIT|MAX_VELOCITY|MAX_ACCELERATION|FERROR|MIN_FERROR|HOME_IGNORE_LIMITS|HOME_ABSOLUTE_ENCODER|HOME_OFFSET|HOME_SEQUENCE)' LC10E.ini

echo
echo "--- Trajectoire ---"
grep -HnE '^\s*(DEFAULT_LINEAR_VELOCITY|MAX_LINEAR_VELOCITY|COORDINATES)' LC10E.ini

echo
echo "--- Gains PID ---"
grep -HnE 'setp\s+pid\.' pid_tuning.hal 2>/dev/null || echo "  (pid_tuning.hal absent)"

echo
echo "--- Echelles & options CiA402 ---"
grep -HnE 'setp\s+cia402\.[0-9]+\.(pos-scale|velo-scale|csp-mode|auto-fault-reset)' LC10E.hal

echo
echo "--- Sorties physiques : qui pilote quoi ---"
grep -HnE 'lcec\.0\.(1|4|6|7|8)\.' LC10E.hal spindle.hal | grep -vE '^\s*#'

echo
echo "--- Fins de course remontees a LinuxCNC ---"
grep -HnE 'lim-sw-in' LC10E.hal || echo "  AUCUNE"

echo
echo "--- Topologie EtherCAT declaree ---"
grep -HoE '<slave idx="[0-9]+" type="[^"]*"' ethercat-conf.xml

echo
echo "--- Bus en direct (si le maitre tourne) ---"
if command -v ethercat >/dev/null && ethercat master >/dev/null 2>&1; then
    ethercat slaves 2>/dev/null | sed 's/^/  /'
else
    echo "  maitre EtherCAT non interrogeable (machine eteinte ou droits)"
fi
