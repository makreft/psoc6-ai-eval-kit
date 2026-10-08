# AI_Eval_Kit

Arbeit mit dem Infineon PSOC 6 AI Evaluation Kit (CY8CKIT-062S2-AI) unter Linux, ohne Windows, ohne ModusToolbox, ohne Infineon-Konto.

| Dokument | Inhalt |
|---|---|
| `SETUP-PROTOKOLL.md` | Vollständiges Protokoll aller Schritte mit Befehlen, Ausgaben und Entscheidungen (50 Abschnitte, 5 Teile) |
| `BOARD-UEBERSICHT.md` | Was das Board kann, wofür es nicht taugt, Einstiegswege |
| `ml/clap/README.md` | Klatsch-Erkennung mit eigenem Modell: Daten, Merkmale, Training, Deployment, Ergebnisse |
| `arduino/README.md` | Arduino-Core, Sketches 01 bis 05, Live-Viewer |
| `data/README.md` | Trainingsdaten |
| `micropython/README.md`, `zephyr/README.md` | Alternative Wege, vorbereitet |

## Was erreicht wurde
1. Firmware des Kits und des KitProg3-Debuggers mit OpenOCD und fw-loader von GitHub aktualisiert, Werksfirmware gesichert.
2. Infineons IMU-Gestenbeispiel (DEEPCRAFT-Modell) auf den Arduino-Core portiert, läuft auf dem Board.
3. Klatsch-Schalter zuerst regelbasiert, dann mit selbst trainiertem CNN (25 KB int8, TensorFlow Lite Micro), inklusive Aufnahme-Werkzeug und Trainingspipeline.
4. Lokaler Live-Viewer im Browser, der zeigt, was der Chip sieht und erkennt.

## Nicht im Repo (reproduzierbar)
- `tools/openocd`, `tools/fw-loader`, `tools/kp-firmware`: GitHub-Releases von Infineon (Protokoll Abschnitt 9).
- `tools/arduino-cli`, `arduino/.arduino15`, `arduino/sketchbook/libraries`: `arduino-cli` von GitHub, dann `core install infineon:psoc6` und `lib install "SparkFun BMI270 Arduino Library" Chirale_TensorFLowLite` (Protokoll Abschnitt 29 und 30).
- `ml/.venv`: `python3 -m venv ml/.venv && ml/.venv/bin/pip install numpy tensorflow`.
- `examples/deploy-motion`: `git clone --depth 1 https://github.com/Infineon/mtb-example-ml-deepcraft-deploy-motion examples/deploy-motion` (Referenz für die Portierung, Stand Commit 3fbeb60).
