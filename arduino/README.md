# Option 1: Arduino Core for PSOC 6 auf dem CY8CKIT-062S2-AI

Stand: 2026-10-08. Aktiver Weg. Einordnung in SETUP-PROTOKOLL.md Teil 3 (ab Abschnitt 28).

## Warum dieser Weg
Infineons offizieller Arduino-Core (github.com/Infineon/arduino-core-psoc6, Version 1.1.0) liefert Toolchain, OpenOCD und die vorkompilierten ModusToolbox-Bibliotheken für dieses Board mit. Kein Infineon-Konto nötig, alles kommt von GitHub und dem Arduino-Paketindex.

## Aufbau (alles projektlokal, nichts unter ~/.arduino15)
```
arduino/
  arduino-cli.yaml       Konfiguration: Boards-URL, Datenverzeichnis, Sketchbook
  .arduino15/            Core, Toolchain (GCC 11.3.1), OpenOCD, Indizes (ca. 600 MB, nicht versionieren)
  sketchbook/
    libraries/           SparkFun BMI270 1.0.3, Chirale_TensorFLowLite 2.0.0
    01-blink-serial/     Erster Test: LED und Konsole
    02-imu-gesture/      Gestenerkennung, Portierung des Infineon-Beispiels
  README.md
```
Das Werkzeug `arduino-cli` liegt unter `tools/arduino-cli/arduino-cli`.

## Befehle (aus ~/Dev/AI_Eval_Kit, Board an J1)
```
ACLI=tools/arduino-cli/arduino-cli
CFG=arduino/arduino-cli.yaml
FQBN=infineon:psoc6:cy8ckit_062s2_ai

$ACLI compile --fqbn $FQBN --config-file $CFG arduino/sketchbook/02-imu-gesture
$ACLI upload  --fqbn $FQBN --config-file $CFG arduino/sketchbook/02-imu-gesture
python3 -m serial.tools.miniterm /dev/ttyACM0 115200        # Konsole, Beenden mit Strg+]
```
Der Upload läuft über Infineons OpenOCD und den KitProg3 an J1 und überschreibt die gerade installierte Firmware (z. B. die DEEPCRAFT-Streaming-Firmware). Zurück zur Streaming-Firmware: `tools/flash.sh program firmware/streaming-firmware-2026-01-07.hex`.

## Live-Viewer
`tools/live_viewer.py` liest die Konsole mit und zeigt im Browser (http://localhost:8765), was der Chip sieht und erkennt: erkannte Geste, Scores, Zeitstreifen der letzten 60 s, Beschleunigung und Drehrate der letzten 10 s, Tabellenansicht, Hell/Dunkel. Voraussetzung: `STREAM_RAW_FRAMES 1` im Sketch (Standard). Beenden mit Strg+C. Details in SETUP-PROTOKOLL.md Abschnitt 37.

## Sketch 02-imu-gesture
Portierung von `examples/deploy-motion` (Infineon, ModusToolbox) auf Arduino:
- `model.c` / `model.h` unverändert übernommen (DEEPCRAFT-Modell: Conv + LSTM, Klassen none / circle / shaking).
- `mtb_ml_model.h` + `mtb_ml_shim.cpp`: Adapter, der Infineons ML-Zwischenschicht (drei Funktionen) auf TensorFlow Lite Micro nachbildet.
- BMI270 über SparkFun-Bibliothek, Konfiguration wie im Original (50 Hz, 8 g, 500 dps), Werte in m/s² und dps.
- Ausgabe: eine Zeile pro Inferenz (alle 10 Frames = 200 ms) mit Scores und bestem Label. LED1 bei "shaking", LED2 bei "circle".
Details und die drei gelösten Kompilierprobleme: SETUP-PROTOKOLL.md Abschnitt 31 und 32.

## Pinbelegung der Variante (Auszug)
LED1 = `LED_BUILTIN` = P5_3, LED2 = P5_4, `USER_BUTTON` = P5_2, I2C (`Wire`) SCL P0_2 / SDA P0_3 (daran hängen IMU, Magnetometer, Drucksensor), PDM-Mikrofon P10_4 / P10_5. Vollständig in `.arduino15/packages/infineon/hardware/psoc6/1.1.0/variants/CY8CKIT-062S2-AI/pins_arduino.h`.

## Sketch 03-clap-led
Klatsch-Schalter ohne ML: LED1 wechselt bei jedem Klatscher den Zustand, LED2 blitzt zur Bestätigung. Erkennung über RMS je 8-ms-Block, mitlaufendes Grundrauschen, Mindestdauer 40 ms und Mindestspitze 1.500 (so eingestellt, dass Fingerschnipsen mit 8 bis 24 ms verworfen wird). Konsole meldet jeden Klatscher und jeden verworfenen Impuls mit Dauer, Spitze, Verhältnis und Nulldurchgangsrate; der Live-Viewer zeigt beides. Zwei Messreihen liegen als CSV daneben. Schwellen stehen oben im Sketch mit Begründung. Details: SETUP-PROTOKOLL.md Abschnitte 38 bis 43.

## Sketch 04-audio-stream
Streamt das Mikrofon als Rohaudio (16 kHz, Mono, 16 Bit) mit Rahmen, Folgenummer und Prüfsumme bei 1 MBaud über J1. Gegenstück ist `tools/record_audio.py` (Aufnahme von Trainingsdaten nach `data/audio/`). LED2 an = Streaming läuft, LED1 an = Aufnahme läuft.

## Sketch 05-clap-model
Klatsch-Erkennung mit dem selbst trainierten Modell aus `ml/clap/`. Merkmalsberechnung in C (FFT 256, 32 Bänder, 512-ms-Fenster, pegelnormiert), Auslösung bei Lautstärkesprung, Inferenz mit TensorFlow Lite Micro, LED1 schaltet bei "clap" ab 60 % um. `model_data.h` und `features_config.h` werden von `ml/clap/train.py` erzeugt und hierher kopiert. Pipeline und Ergebnisse: `ml/clap/README.md`.

## Eigenes Modell einsetzen
In DEEPCRAFT Studio (Windows) ein Modell aus dem Projekt "Motion Detection" generieren und `model.c` / `model.h` im Sketch ersetzen. Enthält das neue Modell andere Operatoren, die Liste in `mtb_ml_shim.cpp` anpassen (Operatoren lassen sich mit dem Python-Schnipsel aus Protokoll-Abschnitt 31 aus `model.c` auslesen).
