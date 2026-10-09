# Firmware-Abbilder (nicht versioniert)

Dieses Verzeichnis enthält im Repo nur `fetch.sh`. Die Abbilder selbst sind Fremddateien und werden geladen:

- `fetch.sh` holt die DEEPCRAFT-Streaming-Firmware von Imagimob (ZIP, SHA-256 geprüft) und entpackt `streaming-firmware-2026-01-07.hex`. Flashen: `tools/flash.sh program firmware/streaming-firmware-2026-01-07.hex`.
- `backup-original-<datum>.bin` ist die Sicherung der Werksfirmware des eigenen Boards (`tools/flash.sh backup`). Sie bleibt lokal.

Hintergrund in SETUP-PROTOKOLL.md Abschnitte 9 bis 12 und 74.
