#!/usr/bin/env bash
# Holt die DEEPCRAFT-Streaming-Firmware fuer das CY8CKIT-062S2-AI (Imagimob, Stand 2026-01-07) und prueft sie per SHA-256.
# Ergebnis: firmware/streaming-firmware-2026-01-07.hex (flashen mit tools/flash.sh program <hex>).
set -euo pipefail
cd "$(dirname "$0")"
curl -fsSL -o streaming-firmware-ai-eval-kit.zip https://developer.imagimob.com/Resources/streaming-firmware-ai-eval-kit.zip
echo "e6ec0d26c929dad628e6388dfd374e024e4b0a6b94eb86cd47197179ba58cd9f  streaming-firmware-ai-eval-kit.zip" | sha256sum -c - >/dev/null || { echo "ZIP-Pruefsumme falsch: Imagimob hat die Datei geaendert, bitte pruefen"; exit 1; }
unzip -p streaming-firmware-ai-eval-kit.zip mtb-example-ml-deepcraft-streaming-protocol.hex > streaming-firmware-2026-01-07.hex
echo "84c7b7a287145cad87f06644f327b951a35ba50a6a1f39868dc7cb13e0a722ed  streaming-firmware-2026-01-07.hex" | sha256sum -c - >/dev/null || { echo "HEX-Pruefsumme falsch"; exit 1; }
echo "ok  streaming-firmware-2026-01-07.hex"
