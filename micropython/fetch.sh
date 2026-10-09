#!/usr/bin/env bash
# Holt die MicroPython-Firmware fuer das CY8CKIT-062S2-AI aus Infineons Release v1.4.1 und prueft sie per SHA-256.
set -euo pipefail
cd "$(dirname "$0")/firmware"
curl -fsSL -o mpy-psoc6_CY8CKIT-062S2-AI.hex https://github.com/Infineon/micropython/releases/download/v1.4.1/mpy-psoc6_CY8CKIT-062S2-AI.hex
echo "6b252bbeae780bf37a55107245d07da081aa360a1e2dfc821b28bfb89dfc8344  mpy-psoc6_CY8CKIT-062S2-AI.hex" | sha256sum -c - >/dev/null || { echo "Pruefsumme falsch"; exit 1; }
echo "ok  firmware/mpy-psoc6_CY8CKIT-062S2-AI.hex"
