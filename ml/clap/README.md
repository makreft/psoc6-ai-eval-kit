# Klatsch-Erkennung mit eigenem Modell auf dem PSOC 6 AI Evaluation Kit

Stand: 2026-10-08. Von der Aufnahme bis zur LED, komplett unter Linux, ohne Infineon-Konto, ohne DEEPCRAFT Studio. Zahlen und Verlauf im Detail: SETUP-PROTOKOLL.md Teil 5 (Abschnitte 46 bis 50).

## 1. Ausgangslage

Ziel: Klatschen schaltet eine LED um, Fingerschnipsen und Umgebungsgeräusche nicht.

Ein regelbasierter Detektor (Lautstärkesprung, Impulsdauer, Spitzenpegel) funktionierte im ruhigen Raum, fiel aber bei Umgebungslärm aus: Die Schwellen hängen vom Raum ab, und Nachhall ließ Klatscher zu lang erscheinen. Drei Messreihen mit 38 bis 107 Ereignissen belegen das (Protokoll Abschnitte 41 bis 44).

Entscheidung: ein kleines Klassifikationsmodell, das die Klangform lernt, mit Trainingsdaten aus genau der Umgebung, in der es laufen soll.

## 2. Pipeline im Überblick

```
Board (Sketch 04)  --Rohaudio 16 kHz-->  tools/record_audio.py  -->  data/audio/*.wav + index.csv
                                                                           |
                                                     ml/clap/dataset.py  (Fenster, Aufteilung, Augmentierung)
                                                     ml/clap/features.py (Spektrogramm-Merkmale)
                                                                           |
                                                     ml/clap/train.py    (CNN, int8-Quantisierung, Export)
                                                                           |
                                              model_data.h + features_config.h  -->  Sketch 05 (TFLM)  -->  LED1
```

## 3. Daten

Aufgenommen mit dem Mikrofon des Kits selbst (dieselbe Hardware wie im Betrieb), 16 kHz, Mono, 16 Bit, geführt durch `tools/record_audio.py`:

| Klasse | Clips | Inhalt |
|---|---|---|
| clap | 40 | Klatschen, 1,5-s-Clips, Abstand 30 bis 100 cm |
| snap | 40 | Fingerschnipsen, 1,5-s-Clips |
| other | 20 | Störgeräusche mit Ansage: Tischklopfen, Schlüssel, Husten, Rufen, Buch, Stift, Stuhl, Tastatur, Schnalzen |
| background | 2 x 60 s | Hintergrund am Stück, einmal Ruhe, einmal Lärm |

Aufteilung 80/20 nach Dateien, damit kein Ereignis gleichzeitig im Training und in der Validierung liegt.

## 4. Merkmale (identisch in Python und C)

- Rahmen von 256 Samples (16 ms), Hann-Fenster, FFT 256, Leistungsspektrum.
- 32 Bänder mit mel-verteilten Grenzen zwischen 125 und 7.500 Hz, als Summen über zusammenhängende FFT-Bins (Tabelle mit 33 Indizes, keine Filterbank mit Gewichten).
- log10 der Bandleistung.
- Fenster von 32 Rahmen (512 ms), Einsatz des Ereignisses bei Rahmen 8.
- Pegelnormierung: minus Maximum des Fensters, 50 dB Dynamik auf [0, 1]. Macht die Eingabe unabhängig von Lautstärke und Abstand.

Ergebnis: ein 32x32-Bild pro Entscheidung. Die Parameter sind so gewählt, dass der Chip mit einer festen 256-Punkt-FFT und Bin-Summen auskommt.

## 5. Augmentierung

Nur im Training, je Ereignis sechs Varianten: Verstärkung 0,3 bis 1,0, Mischung mit einem zufälligen Hintergrundausschnitt bei 0 bis 25 dB Rauschabstand (70 % der Fälle), Verschiebung des Einsatzes um bis zu 3 Rahmen. Der Hintergrund liefert zusätzlich Fenster an den lautesten Stellen und an Zufallspositionen als Klasse "other". Daraus werden 656 Trainingsfenster (208 other, 224 clap, 224 snap).

## 6. Modell

```
Eingabe 32x32x1
Conv2D 8  3x3 ReLU, MaxPool 2x2   -> 16x16x8
Conv2D 16 3x3 ReLU, MaxPool 2x2   -> 8x8x16
Conv2D 32 3x3 ReLU, MaxPool 2x2   -> 4x4x32
Reshape 512, Dropout 0,3
Dense 24 ReLU
Dense 3 Softmax                   -> other, clap, snap
```
18.275 Parameter. Adam, Klassengewichte, EarlyStopping. Training 15 Sekunden auf einer CPU.

## 7. Ergebnisse

| | Wert |
|---|---|
| Validierung (20 Clips, unabhängige Dateien) | 20 von 20 richtig, float und int8 |
| val_loss | 0,097 |
| Modellgröße int8 | 25.312 Bytes |
| Operatoren | CONV_2D, MAX_POOL_2D, RESHAPE, FULLY_CONNECTED, SOFTMAX |
| Arena auf dem Chip | 12.484 Bytes (40 KB reserviert) |
| Flash des gesamten Sketches | 163.532 Bytes (7 % von 2 MB) |
| Latenz | etwa 0,4 s (Fenster-Nachlauf) plus Inferenz |

Vorbehalt: Die Validierung ist klein und stammt aus derselben Sitzung wie das Training. Der aussagekräftige Test ist der Betrieb auf dem Board bei Lärm.

## 8. Deployment

`train.py` erzeugt `model_data.h` (C-Array) und `features_config.h` (Bandtabelle, Fensterparameter, Quantisierungsparameter). Beides wird in `arduino/sketchbook/05-clap-model/` kopiert. Der Sketch rechnet die Merkmale in C (eigene FFT, 20 Zeilen), löst bei einem Lautstärkesprung aus, wartet 384 ms, bildet das Fenster, quantisiert nach int8 und führt das Modell mit TensorFlow Lite Micro aus. Bei "clap" mit mindestens 35 % Wahrscheinlichkeit schaltet LED1 um (ursprünglich 60 %; nach dem ersten Praxistest gesenkt, weil Schnipsen und Störgeräusche nie über 13 % kamen, Klatscher aber bis auf 0,31 herunterreichten).

## 9. Stolpersteine, die Zeit gekostet haben

1. **Keras `Flatten` erzeugt dynamische Shape-Operatoren** (SHAPE, STRIDED_SLICE, PACK) im TFLite-Graphen, weil die Batch-Dimension offen ist. Unnötige Kernel auf dem Chip.
2. **Export über `tf.function` mit fester Batch-Größe** ließ die Gewichte als Variablen im Graphen (READ_VARIABLE); die int8-Kalibrierung brach ab.
3. **Lösung:** `model.export(..., input_signature=[TensorSpec([1, 32, 32, 1])])`, dann `TFLiteConverter.from_saved_model`. Ergebnis: nur die fünf erwarteten Operatoren.
4. **Serieller Port ist exklusiv:** Läuft der Viewer, bekommt kein zweites Programm Daten. Das Aufnahme-Werkzeug prüft das vorher.
5. **`python -I`** hängt das Skriptverzeichnis nicht an den Importpfad; Projektskripte daher mit normalem `python` aus dem venv starten.

## 10. Reproduzieren

```
# Aufnahmen (Board an J1, Sketch 04 geflasht, kein Viewer aktiv)
tools/record_audio.py session
tools/record_audio.py background --seconds 60

# Training
cd ml && .venv/bin/python clap/train.py --data ../data/audio --out clap/out

# Deployment
cp ml/clap/out/model_data.h ml/clap/out/features_config.h arduino/sketchbook/05-clap-model/
tools/arduino-cli/arduino-cli compile --fqbn infineon:psoc6:cy8ckit_062s2_ai --config-file arduino/arduino-cli.yaml arduino/sketchbook/05-clap-model
tools/arduino-cli/arduino-cli upload  --fqbn infineon:psoc6:cy8ckit_062s2_ai --config-file arduino/arduino-cli.yaml -p /dev/ttyACM0 arduino/sketchbook/05-clap-model

# Live ansehen
tools/live_viewer.py --log modell-test.csv     # http://localhost:8765
```

## 11. Was als Nächstes helfen würde

- Mehr Vielfalt in den Daten: andere Räume, andere Personen, Abstände über einen Meter, weitere Störgeräusche (Türen, Geschirr, Musik mit Schlagzeug).
- Fehler aus dem Betrieb gezielt nachaufnehmen (falsch erkannte Geräusche als "other", verpasste Klatscher als "clap") und nachtrainieren. Der Viewer-Mitschnitt zeigt die Zeitpunkte.
- Schwelle und Totzeit im Sketch anhand der Mitschnitte einstellen.

## Doppelklatschen: Simulation mit dem fertigen Modell

Die Zephyr-Firmware `house` schaltet erst bei zwei Klatschern (Abstand 120 bis 800 ms). Da das Modell nur Einzelklatscher kennt, prüft `simulate_double.py`, ob der zweite Klatscher im Fenster des ersten das Urteil verdirbt: aus je zwei Aufnahmen aus `data/audio/clap` entsteht ein Doppelklatscher mit 150 bis 650 ms Abstand, beide Fenster laufen durch `out/clap_model.tflite`. Ergebnis (11 Paare): Fenster 1 bleibt bei p_clap 0,84 bis 0,98 im Mittel, Ausfälle nur bei einzeln schwachen Aufnahmen, Doppel-Schnipser bleiben bei 0,00 bis 0,04.
```
cd ml && .venv/bin/python -I clap/simulate_double.py clap clap/out ../data/audio
```
