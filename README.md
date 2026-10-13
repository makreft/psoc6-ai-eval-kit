# AI_Eval_Kit

Arbeit mit dem Infineon PSOC 6 AI Evaluation Kit (CY8CKIT-062S2-AI) unter Linux, ohne Windows, ohne ModusToolbox, ohne Infineon-Konto.

| Dokument | Inhalt |
|---|---|
| `SETUP-PROTOKOLL.md` | Vollständiges Protokoll aller Schritte mit Befehlen, Ausgaben und Entscheidungen (74 Abschnitte, 7 Teile) |
| `BOARD-UEBERSICHT.md` | Was das Board kann, wofür es nicht taugt, Einstiegswege |
| `ml/clap/README.md` | Klatsch-Erkennung mit eigenem Modell: Daten, Merkmale, Training, Deployment, Ergebnisse |
| `arduino/README.md` | Arduino-Core, Sketches 01 bis 06, Live-Viewer |
| `data/README.md` | Trainingsdaten |
| `zephyr/README.md` | Zephyr: Bluetooth-Anwesenheit, Klatsch-Modell, gemeinsame Firmware `house` |
| `micropython/README.md` | MicroPython, vorbereitet |

## Was erreicht wurde
1. Firmware des Kits und des KitProg3-Debuggers mit OpenOCD und fw-loader von GitHub aktualisiert, Werksfirmware gesichert.
2. Infineons IMU-Gestenbeispiel (DEEPCRAFT-Modell) auf den Arduino-Core portiert, läuft auf dem Board.
3. Klatsch-Schalter zuerst regelbasiert, dann mit selbst trainiertem CNN (25 KB int8, TensorFlow Lite Micro), inklusive Aufnahme-Werkzeug und Trainingspipeline.
4. Lokaler Live-Viewer im Browser, der zeigt, was der Chip sieht und erkennt, mit Menü für alle Funktionen.
5. Unter Zephyr: Anwesenheit des Handys per klassischem Bluetooth ohne App, Klatsch-Modell, beides parallel in einer Firmware (`house`): Doppelklatschen schaltet das Licht, Verlassen des Hauses schaltet es aus.

## Schnellstart für Studierende
Ziel: das Kit mit der Firmware `house` betreiben (Doppelklatschen schaltet LED1, LED2 zeigt, ob eines eurer Handys per Bluetooth erreichbar ist) und die Ausgaben im Browser sehen. Linux (getestet Ubuntu 24.04), kein Infineon-Konto nötig.

1. Repo klonen, Werkzeuge holen: `tools/setup.sh` (OpenOCD, fw-loader, arduino-cli), dann die udev-Regeln installieren (Befehle stehen im Kopf von `tools/setup.sh`, einmalig mit sudo) und euch zur Gruppe `plugdev` hinzufügen.
2. Board an J1 (USB-Buchse neben dem Debugger-Chip), prüfen: `tools/flash.sh info`.
3. Zephyr-Workspace anlegen: `zephyr/setup.sh` (lädt etwa 5 GB, einmalig).
4. Handy-Adressen eintragen: `zephyr/app/house/phones.conf` nach der Vorlage `phones.conf.example` anlegen. Android: Einstellungen > Über das Telefon > Status; iPhone: Einstellungen > Allgemein > Info.
5. Bauen und flashen (siehe `zephyr/README.md`, Abschnitt house): `west build ... -- -DEXTRA_CONF_FILE=phones.conf`, dann `west flash -d build/house`.
6. Viewer starten: `tools/live_viewer.py`, Browser auf http://localhost:8765, Menü links oben auf "Haus".

Alles andere (Arduino-Weg, eigenes Klatsch-Modell trainieren, Aufnahmen machen) steht in den README-Dateien der Unterordner. Die Entstehungsgeschichte mit allen Irrwegen steht in `SETUP-PROTOKOLL.md`. Lizenz: MIT (`LICENSE`) für Code und Dokumentation; die Audioaufnahmen in `data/audio` sind Eigenaufnahmen und dürfen für eigene Experimente genutzt werden.

## Abhängigkeiten
Im Repo liegt nur Eigenes. Alles Fremde ist mit fester Version deklariert und wird per Skript geholt, Downloads werden per SHA-256 geprüft (Protokoll Abschnitt 74).

| Abhängigkeit | Version | Deklariert in | Holen mit |
|---|---|---|---|
| Infineon OpenOCD, fw-loader, KitProg3-Firmware, arduino-cli | 5.20.0, 3.14.0, 2.90.1804, 1.5.1 | `tools/setup.sh` | `tools/setup.sh` |
| Arduino-Core infineon:psoc6, SparkFun BMI270, Chirale_TensorFLowLite | 1.1.0, 1.0.3, 2.0.0 | `sketch.yaml` je Sketch (Build-Profil) | `arduino-cli compile --profile psoc6 <sketch>` |
| Imagimob-Gestenmodell (Infineon-Beispiel, Commit 3fbeb60) | model.c/model.h | `arduino/sketchbook/02-imu-gesture/fetch_model.sh` | dasselbe Skript |
| Zephyr und Module (hal_infineon, cmsis, cmsis_6, picolibc, mbedtls, tf-psa-crypto), TensorFlow Lite Micro (Chirale) | v4.4.2, v2.0.0 | `zephyr/manifest/west.yml` | `zephyr/setup.sh` (west, SDK 1.0.1, Blobs, Patches) |
| DEEPCRAFT-Streaming-Firmware (Imagimob) | Stand 2026-01-07 | `firmware/fetch.sh` | dasselbe Skript |
| MicroPython-Firmware (Infineon) | v1.4.1 | `micropython/fetch.sh` | dasselbe Skript |
| Python: TensorFlow, numpy; pyserial | 2.21.0, 2.5.3; 3.5 | `ml/requirements.txt`, `tools/requirements.txt` | `pip install -r` |

Nur lokal, nicht versioniert: die Sicherung der Werksfirmware (`firmware/backup-*.bin`, mit `tools/flash.sh backup` erzeugt), Build-Verzeichnisse, venvs und die Referenzkopie `examples/deploy-motion` (`git clone` des Infineon-Beispiels).
