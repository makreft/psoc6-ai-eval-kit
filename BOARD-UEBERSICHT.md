# Wofür kann ich das PSOC 6 AI Evaluation Kit verwenden?

Stand: 2026-10-08. Ergänzt das SETUP-PROTOKOLL.md, das den Weg bis zur aktuellen Firmware beschreibt. Dieses Dokument erklärt, was das Board ist, was es kann, was es nicht kann und wie man damit anfängt.

---

## 1. Kurzfassung

Das CY8CKIT-062S2-AI ist ein Mikrocontroller-Board für Edge AI: kleine Machine-Learning-Modelle laufen direkt auf dem Chip und werten Sensordaten in Echtzeit aus, ohne PC und ohne Cloud. Es ist kein kleiner Computer wie ein Raspberry Pi, sondern eher ein sehr gut ausgestatteter Sensorknoten in der Klasse eines ESP32.

Der typische Zyklus:
1. Sensordaten aufnehmen und labeln (z. B. "Schütteln", "Kreis", "Ruhe").
2. Am PC ein kleines Modell trainieren.
3. Das Modell als C-Code auf den Chip bringen.
4. Der Chip erkennt die Muster live, stromsparend, offline.

---

## 2. Der Chip: PSOC 6 (CY8C624ABZI-S2D44)

| Eigenschaft | Wert | Was das praktisch heißt |
|---|---|---|
| Kerne | Cortex-M4 (bis 150 MHz) + Cortex-M0+ (bis 100 MHz) | Der M4 rechnet das Modell, der M0+ kann Sensoren und Funk nebenbei bedienen |
| Flash | 2 MB intern | Platz für Firmware plus Modell; die Streaming-Firmware belegt knapp 1 MB |
| RAM | 1 MB | Reicht für Audio-Puffer und Modelle im Bereich von zehn bis einigen hundert KB |
| Externer Flash | 64 MB QSPI (S25HS512T) | Dateisystem für Aufzeichnungen und Konfiguration, von der Firmware als littlefs genutzt |
| Betriebssystem | keins im klassischen Sinn | Firmware läuft direkt auf dem Chip, meist mit FreeRTOS. Kein Linux, kein Python zur Laufzeit (außer MicroPython, siehe Abschnitt 6) |
| Stromverbrauch | Mikrocontroller-Klasse | Batteriebetrieb über Monate ist das Ziel der Chipfamilie |
| Funk | CYW43439: Wi-Fi 4 (2,4 GHz) + Bluetooth 5 / BLE | Derselbe Chip wie auf dem Raspberry Pi Pico W |
| Debugger | KitProg3 an Bord (J1) | Flashen und Debuggen ohne Zusatzhardware, unter Linux mit OpenOCD |

Vergleich zur Einordnung:
- **Arduino Uno:** viel schwächer, kein Funk, kein ML.
- **ESP32:** ähnliche Leistungsklasse und Funk, aber ohne die Sensorausstattung und ohne die Infineon-ML-Werkzeugkette.
- **Raspberry Pi:** ganz andere Klasse. Linux, Kamera, Browser, viel Rechenleistung, aber auch 100-mal mehr Stromverbrauch und Sekunden Bootzeit.

---

## 3. Die Sensoren und ihre Anwendungsfelder

Alle fünf Sensoren sind in der Streaming-Firmware als "Ready" gemeldet (Protokoll Abschnitt 23).

### IMU: Bosch BMI270 (Beschleunigung + Gyroskop, 50 bis 400 Hz)
- Gestenerkennung in der Hand: Schütteln, Kreis, Tippen, Drehen.
- Aktivitätserkennung am Körper: Gehen, Laufen, Treppe, Sturz.
- Vibrationsanalyse an Maschinen: normal vs. Lager defekt vs. Unwucht (Predictive Maintenance).
- Lage- und Bewegungserkennung von Objekten: Paket geöffnet, Tür bewegt, Fahrrad umgefallen.

### Mikrofone: zwei digitale PDM-Mikrofone (8 oder 16 kHz)
- Keyword Spotting: feste Sprachbefehle wie "an", "aus", "hoch", "runter". Infineon liefert dafür ein fertiges Beispiel.
- Geräuscherkennung: Glasbruch, Rauchmelder, Sirene, Hundebellen, Babyweinen.
- Akustische Anomalien: Lüfter klingt anders als sonst, Pumpe kavitiert.
- Zwei Mikrofone erlauben grob eine Richtungsschätzung.

### Radar: Infineon BGT60TRxx (60 GHz, XENSIV)
- Anwesenheitserkennung: grobe Bewegung (Makro) bis hin zu Atmung einer ruhenden Person (Mikro).
- Berührungslose Gestensteuerung: Wischen, Drücken in der Luft.
- Personenzählung und Abstandsmessung im Nahbereich.
- Vorteile gegenüber Kamera: funktioniert im Dunkeln, durch Kunststoffgehäuse hindurch, erfasst keine Bilder (Datenschutz).
- Besonderheit: Radar muss laut Imagimob separat aufgezeichnet werden, nicht zusammen mit anderen Sensoren.

### Magnetometer: Bosch BMM350 (3 Achsen, 50 bis 400 Hz)
- Kompass und Orientierung, zusammen mit der IMU als Sensorfusion.
- Erkennung von Zuständen über Magnete: Tür offen/zu, Deckel auf, Schalterstellung.
- Erkennung großer Metallteile in der Nähe.

### Luftdruck und Temperatur: Infineon DPS368 (8 bis 128 Hz)
- Höhen- und Stockwerkerkennung (Auflösung im Bereich weniger Dezimeter).
- Erkennung von Türschlagen oder Fensteröffnen über kurze Druckstöße in Räumen.
- Einfache Wetter- und Raumklimadaten.

### Kombinationen
Mehrere Sensoren lassen sich gleichzeitig streamen und in ein Modell geben, etwa IMU plus Mikrofon für robustere Aktivitätserkennung oder IMU plus Magnetometer für Orientierung.

---

## 4. Weitere Ausstattung

- **Wi-Fi und Bluetooth:** Ergebnisse weitermelden (MQTT, HTTP), Sensordaten kabellos zu DEEPCRAFT Studio streamen (TSP-Server auf Port 14325), BLE-Verbindung zum Handy.
- **Zwei User-LEDs und ein Taster:** Minimal-UI für Demos (LED zeigt erkannte Geste, Taster startet Aufzeichnung).
- **Akku-Anschluss J3 (JST-PH, 3,6 bis 4,2 V):** Betrieb ohne USB-Kabel, z. B. für Aufzeichnungen unterwegs oder Radar-Tests im Raum.
- **Zwei USB-C-Buchsen:** J1 für Debugger und Konsole, J2 für das Sensor-Streaming.
- **Erweiterungspins:** Das Board hat Lötpads bzw. Stiftleisten für einige GPIO-, I2C- und SPI-Signale. Umfang und Belegung stehen im Board-Handbuch bei Infineon und wurden hier nicht geprüft.

---

## 5. Wofür es nicht taugt

- **Keine Kamera, keine Bildverarbeitung.** Es gibt keinen Kameraanschluss, und die Rechenleistung reicht nicht für Bild-Modelle.
- **Keine großen Modelle.** Kein KI-Beschleuniger (NPU). Realistisch sind Modelle von zehn bis einigen hundert Kilobyte, Inferenzen im Millisekundenbereich. Sprachmodelle, Objekterkennung oder ähnliches laufen hier nicht.
- **Kein Allzweckrechner.** Kein Linux, kein Browser, keine Datenbank. Wer einen Server oder eine Weboberfläche braucht, nimmt einen Raspberry Pi oder PC daneben und lässt das Board nur die Sensorik und die Erkennung machen.
- **Begrenzte Erweiterbarkeit.** Es ist ein kompaktes Evaluationskit mit festen Sensoren, kein Baukasten mit vielen Shields.

Die Stärke ist genau das Gegenteil: ein einzelnes, klar umrissenes Erkennungsproblem jahrelang an einer Batterie lösen.

---

## 6. Drei Wege, es zu benutzen

### Weg A: Der offizielle Weg mit DEEPCRAFT Studio (Windows)
1. Board per USB (J2) oder Wi-Fi mit Studio verbinden. Mit der jetzt installierten Firmware ist das Board dafür bereit.
2. Im Graph UX Sensordaten live aufnehmen und per Label Track beschriften.
3. Modell in Studio trainieren und bewerten (Studio schlägt Architekturen vor).
4. Code generieren (`model.c`/`model.h`), in ein Infineon-Codebeispiel einsetzen, mit ModusToolbox bauen und flashen.

Voraussetzung: ein Windows-Rechner oder eine Windows-VM, denn Studio gibt es nur für Windows. Für die Datenaufnahme reicht es, wenn das Board per Wi-Fi mit dem Windows-Rechner im selben Netz ist.

### Weg B: Der Linux-Weg ohne Studio
1. **Daten aufnehmen:** Entweder direkt auf dem Board mit den Shell-Befehlen `dset`, `dapply`, `dcap` und `dhalt` (landet auf dem 64-MB-Dateisystem, mit `tsp2wav` in WAV wandelbar), oder mit einem eigenen Client für das Tensor Streaming Protocol v2 (protobuf3, offen auf Bitbucket) über J2 oder Wi-Fi.
2. **Trainieren:** In Python mit TensorFlow und TensorFlow Lite Micro oder einem anderen TinyML-Framework.
3. **Deployen:** Infineon-Codebeispiele (`mtb-example-ml-deepcraft-deploy-*`) als Grundlage, Modell einsetzen, mit ModusToolbox unter Ubuntu bauen, mit OpenOCD flashen (`tools/flash.sh program`).

Mehr Handarbeit, aber vollständig ohne Windows und mit voller Kontrolle über Daten und Modell.

### Weg C: Fertige Beispiele ausprobieren
Infineon liefert für genau dieses Kit Beispiele mit bereits trainierten Modellen:
- `mtb-example-ml-deepcraft-deploy-motion`: Bewegungserkennung per IMU (Schütteln, Kreis zeichnen), Anzeige über LED und Konsole.
- `mtb-example-ml-deepcraft-deploy-audio`: Keyword Spotting für Wörter wie "up", "down", "left", "right", "dog", "cat".
- `mtb-example-ml-deepcraft-deploy-ready-model`: Rahmen zum Einsetzen eines fertigen Modells.
- Radar-Demos zu Anwesenheit und Gesten im Infineon-Ökosystem (XENSIV).

Dafür braucht es ModusToolbox (läuft offiziell auf Ubuntu 24.04) und den ARM-GCC, beides noch nicht installiert. Danach ist ein Beispiel in etwa einer Stunde auf dem Board.

### Weg D (Bonus): MicroPython
Infineon pflegt einen MicroPython-Port für PSOC 6. Damit lassen sich Sensoren und LEDs in Python ansprechen und auch DEEPCRAFT-Modelle einbinden (Hackster-Artikel "Edge AI made easy with DEEPCRAFT MicroPython"). Gut zum schnellen Ausprobieren, weniger gut für Stromsparen und Echtzeit.

---

## 7. Einstiegsempfehlung

Reihenfolge nach Aufwand und Lerneffekt:

1. **IMU-Beispiel flashen (Weg C):** Board in die Hand, Gesten machen, LED reagiert. Zeigt in einer Stunde, wie sich Edge AI anfühlt, und testet gleichzeitig die ModusToolbox-Installation unter Linux.
2. **Eigene Daten aufzeichnen:** Mit `dcap` auf dem Board oder per Studio, drei bis fünf eigene Klassen (z. B. drei Gesten plus Ruhe). Hier wird klar, wie viel Datenqualität ausmacht.
3. **Eigenes Modell trainieren und deployen:** Studio (Weg A) oder Python (Weg B), Ergebnis ins Beispielprojekt einsetzen.
4. **Erst danach Radar oder Audio:** Beide brauchen mehr Daten und mehr Vorverarbeitung, sind aber auch die spannenderen Anwendungen des Kits.

Konkrete Projektideen in aufsteigender Schwierigkeit:
- Gestensteuerung für eine Lampe oder einen Medienplayer per IMU und BLE.
- "Ist jemand im Raum?" per Radar, Ergebnis per MQTT an eine Hausautomation.
- Geräuschwächter: erkennt Rauchmelder oder Glasbruch und schickt eine Push-Nachricht über Wi-Fi.
- Vibrationswächter an Waschmaschine oder 3D-Drucker: meldet Programmende oder ungewöhnliche Geräusche.

---

## 8. Quellen und weiterführende Links

- Infineon Produktseite: https://www.infineon.com/cms/en/product/evaluation-boards/cy8ckit-062s2-ai/
- Imagimob Getting Started für dieses Kit: https://developer.imagimob.com/deepcraft-studio/getting-started/infineon-boards/infineon-ai-evaluation-kit
- Imagimob Datenerfassung (Sensoren, Raten, Wi-Fi): https://developer.imagimob.com/deepcraft-studio/data-preparation/data-collection/collect-data-infineon-boards/data-collection-ai-kit
- Tensor Streaming Protocol v2: https://developer.imagimob.com/getting-started/tensor-streaming-protocol und https://bitbucket.org/imagimob/tensor-streaming-protocol
- Infineon Codebeispiele: https://github.com/Infineon/mtb-example-ml-deepcraft-deploy-motion, https://github.com/Infineon/mtb-example-ml-deepcraft-deploy-audio, https://github.com/Infineon/mtb-example-ml-deepcraft-streaming-protocol
- ModusToolbox Installation (Linux): https://documentation.infineon.com/modustoolbox/docs/uqy1738949056062
- MicroPython-Pfad: https://www.hackster.io/Infineon_Team/edge-ai-made-easy-with-deepcraft-micropython-part-ii-fa5edf
- DEEPCRAFT Community-Forum: https://community.infineon.com/t5/Imagimob/bd-p/Imagimob
