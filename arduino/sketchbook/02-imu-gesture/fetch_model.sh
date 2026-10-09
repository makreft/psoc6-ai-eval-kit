#!/usr/bin/env bash
# Holt das Imagimob-Gestenmodell (model.c, model.h) aus Infineons Beispiel mtb-example-ml-deepcraft-deploy-motion,
# gepinnt auf einen Commit und per SHA-256 geprueft. Die Dateien sind "Copyright Imagimob AB, All Rights Reserved"
# und liegen deshalb nicht im Repo. Aufruf: arduino/sketchbook/02-imu-gesture/fetch_model.sh
set -euo pipefail
cd "$(dirname "$0")"
COMMIT=3fbeb60d4f1cbfd6c56caca7b5a72a0309d72c90
BASE="https://raw.githubusercontent.com/Infineon/mtb-example-ml-deepcraft-deploy-motion/$COMMIT/models"
fetch() { curl -fsSL -o "$1" "$BASE/$1"; echo "$2  $1" | sha256sum -c - >/dev/null || { echo "Pruefsumme falsch: $1"; rm -f "$1"; exit 1; }; echo "ok  $1"; }
fetch model.c 881718be67819be5091a61023520348c83e9624d3a604eba2ffc38b26919a89c
fetch model.h b5247bb46c2328c6fe32aecf00ad331b846acc242c5550e8bf997ab132eed046
