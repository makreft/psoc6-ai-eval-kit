#!/usr/bin/env bash
# Flasht die DEEPCRAFT-Streaming-Firmware auf das PSOC 6 AI Evaluation Kit (CY8CKIT-062S2-AI)
# ueber den KitProg3-Debugger (Board an J1 anschliessen).
#
# Aufruf:
#   tools/flash.sh backup            Hauptflash (2 MB ab 0x10000000) als Sicherung auslesen
#   tools/flash.sh program [HEX]     HEX programmieren und verifizieren (Standard: firmware/streaming-firmware-2026-01-07.hex)
#   tools/flash.sh info              Nur verbinden, Chip erkennen, Targets anzeigen
#   tools/flash.sh reset             Board zuruecksetzen (fuer Boot-Banner auf der Konsole)
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(dirname "$HERE")"
OCD="$HERE/openocd/bin/openocd"
SCRIPTS="$HERE/openocd/scripts"
CFG=(-s "$SCRIPTS" -f interface/kitprog3.cfg -f target/infineon/cy8cxxa.cfg)
HEX_DEFAULT="$ROOT/firmware/streaming-firmware-2026-01-07.hex"
MAIN_FLASH_ADDR=0x10000000
MAIN_FLASH_SIZE=0x200000   # 2 MB, CY8C624ABZI-S2D44

cmd="${1:-}"
case "$cmd" in
  info)
    "$OCD" "${CFG[@]}" -c "init; targets; shutdown"
    ;;
  backup)
    out="$ROOT/firmware/backup-original-$(date +%Y%m%d-%H%M%S).bin"
    echo "Sichere Hauptflash nach $out"
    "$OCD" "${CFG[@]}" -c "init; reset init; dump_image \"$out\" $MAIN_FLASH_ADDR $MAIN_FLASH_SIZE; shutdown"
    ls -la "$out"; sha256sum "$out"
    ;;
  program)
    hex="${2:-$HEX_DEFAULT}"
    [ -f "$hex" ] || { echo "HEX nicht gefunden: $hex" >&2; exit 1; }
    echo "Programmiere $hex"
    "$OCD" "${CFG[@]}" -c "program \"$hex\" verify reset exit"
    ;;
  reset)
    "$OCD" "${CFG[@]}" -c "init; reset run; shutdown"
    ;;
  *)
    sed -n '2,10p' "$0"; exit 1
    ;;
esac
