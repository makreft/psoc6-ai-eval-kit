# Option 3: Zephyr RTOS auf dem CY8CKIT-062S2-AI

Stand: 2026-10-08. Vorbereitet (Anleitung und Setup-Skript), nichts installiert. Siehe SETUP-PROTOKOLL.md Abschnitt 27.

## Was das ist
Zephyr ist ein Open-Source-Echtzeitbetriebssystem der Linux Foundation. Das AI-Kit ist im Zephyr-Hauptzweig enthalten (Board `cy8ckit_062s2_ai`, Zielname `cy8ckit_062s2_ai/cy8c624abzi_s2d44`, Doku zuletzt 2026-05-07 aktualisiert). Kein Infineon-Konto nötig; Toolchain ist das Zephyr SDK von GitHub.
Doku: https://docs.zephyrproject.org/latest/boards/infineon/cy8ckit_062s2_ai/doc/index.html

## Was Zephyr für dieses Board laut Support-Tabelle kann
GPIO, UART (2x), I2C, ADC, DMA, Counter, Watchdog, Flash-Controller, SDHC, LEDs, Taster, DPS310/DPS368 (Druck, Temperatur) und BMI270 (IMU) als Zephyr-Sensortreiber.
**Nicht in der Tabelle, also unbestätigt:** Wi-Fi, Bluetooth, USB, PDM-Mikrofon, SPI, QSPI-Flash, Radar, Magnetometer.
Pins: Konsole UART5 auf P5.1 (TX) / P5.0 (RX), LED0 P5.3, LED1 P5.4, Taster P5.2. Systemtakt 100 MHz.

## Ablauf
1. Setup-Skript ausführen (siehe unten): Python-venv mit `west`, Workspace nach `manifest/west.yml` (Zephyr v4.4.2, nur die Module für Infineon, TensorFlow Lite Micro), Zephyr SDK 1.0.1 (ARM-Toolchain, SHA-256 geprüft).
2. Binär-Blob für den CM0+ holen (Pflicht für dieses Board): `west blobs fetch hal_infineon`
3. Bauen: `west build -b cy8ckit_062s2_ai/cy8c624abzi_s2d44 zephyr/samples/hello_world`
4. Flashen über unser Infineon-OpenOCD (Board an J1):
   `west config build.cmake-args -- "-DOPENOCD=$HOME/Dev/AI_Eval_Kit/tools/openocd/bin/openocd"` einmalig, dann `west flash`
5. Konsole: J1, 115200 8N1, erwartet "Hello World! cy8ckit_062s2_ai".

TensorFlow Lite Micro: Zephyr 4.4 liefert kein tflite-micro-Modul mehr mit. Das Manifest deklariert deshalb die Bibliothek Chirale_TensorFlowLite v2.0.0 (TFLM mit CMSIS-NN, Apache 2.0, dieselbe wie unter Arduino) als west-Projekt unter `modules/lib/chirale-tflm`; `app/clap_model` und `app/house` kompilieren sie mit.

## Stand 2026-10-09: Gemeinsame Firmware "house" (Klatschen + Bluetooth)

`app/house` ist das Ziel des "vollen Wegs": Klatsch-Erkennung mit dem selbst trainierten Modell und Bluetooth-Anwesenheit laufen parallel in zwei Threads. Zweimal klatschen (Abstand 120 bis 800 ms, beide vom Modell bestätigt) schaltet das Licht (LED1) um, ein einzelner Klatscher verfällt; verlässt das Handy das Haus (Bluetooth nicht mehr erreichbar), geht das Licht aus; LED2 zeigt die Anwesenheit. Die Inferenz läuft in einem eigenen Thread, der Audio-Thread liest dabei weiter. Konsole: die Zeilen beider Teile plus `LICHT an|aus  (Grund)`; `tools/live_viewer.py` zeigt dafür die Ansicht "Haus".
```
west build -p always -b cy8ckit_062s2_ai/cy8c624abzi_s2d44 -d build/house app/house
west flash -d build/house
```
Der Build dauert beim ersten Mal etwa 80 s (TensorFlow Lite Micro wird mitkompiliert). Protokoll Abschnitte 71, 73 und 76.

**Handys eintragen (bis zu 8):** Die Adressen stehen nicht im Repo, sondern in `app/house/phones.conf` (Vorlage `phones.conf.example`, Datei ist gitignored):
```
CONFIG_HOUSE_PHONES="AA:BB:CC:DD:EE:01,AA:BB:CC:DD:EE:02,AA:BB:CC:DD:EE:03"
```
```
west build -p always -b cy8ckit_062s2_ai/cy8c624abzi_s2d44 -d build/house app/house -- -DEXTRA_CONF_FILE=phones.conf
west flash -d build/house
```
Anwesend (LED2 an) = mindestens eines der Handys antwortet auf das Paging; abwesend = keines antwortet in zwei Runden hintereinander (bei 15 s Rundenabstand und 5 s Page-Timeout je abwesendem Handy nach 30 bis 45 s). Android zeigt die klassische Bluetooth-Adresse unter Einstellungen > Über das Telefon > Status; Bluetooth muss an sein, eine Kopplung ist nicht nötig. iPhones antworten ebenfalls (getestet mit zwei iPhones; die Adresse steht unter Einstellungen > Allgemein > Info > Bluetooth).

**Für ein anderes Board (z. B. das der Studierenden):** gleiches Kit, gleicher Build. Vorher einmal `tools/fw-loader/bin/fw-loader --device-list` und bei alter KitProg3-Firmware `--update-kp3 kitprog3` (Protokoll Abschnitte 9 bis 12), dann `west flash` mit dem Board an J1.

Bausteine, einzeln lauffähig:
- `app/clap_rules`: regelbasierter Klatsch-Detektor (Port von Sketch 03) mit dem PDM-Mikrofon über die Infineon-HAL (PLL0 auf 24,576 MHz, 16 kHz mono). Protokoll Abschnitte 64 und 69.
- `app/clap_model`: Klatsch-Modell mit TensorFlow Lite Micro (Port von Sketch 05). Die TFLM-Quellen kommen aus der Arduino-Bibliothek `Chirale_TensorFLowLite` (CMake-Variable `TFLM_DIR`), Modell und Merkmalskonfiguration sind Kopien aus `ml/clap/out/`. Protokoll Abschnitt 70.

## Stand 2026-10-09: Anwesenheit ohne App per klassischem Bluetooth

`app/bt_page_presence`: Das Board fragt alle 15 s per HCI Remote Name Request die feste klassische Bluetooth-Adresse des Handys ab (`PHONE_BT_ADDR` in `src/main.c`, Android: Settings > About phone > Status > Bluetooth address). Antwort = Handy in Reichweite und Bluetooth an, LED1 an; zwei Fehlversuche in Folge = LED1 aus. Keine App, keine Kopplung, kein Dialog auf dem Handy. Läuft im HCI-Rohmodus ohne Zephyr-Host, weil der experimentelle klassische Host bei der ersten Verbindung einfriert (Protokoll Abschnitte 65 bis 67).
```
west build -p always -b cy8ckit_062s2_ai/cy8c624abzi_s2d44 -d build/page app/bt_page_presence
west flash -d build/page
```
Start dauert etwa 22 s (Firmware-Download im Rohmodus bei 115200 Baud, Optimierung offen).

## Stand 2026-10-08: Bluetooth läuft (BLE-Scanner, Beacon-App nötig, durch bt_page_presence abgelöst)

Workspace per `setup.sh` aufgesetzt (Zephyr v4.4.2, Module hal_infineon, cmsis, cmsis_6, picolibc, mbedtls, tf-psa-crypto; SDK 1.0.1; Blobs). Anwendung `app/ble_presence` scannt passiv nach BLE-Advertisements und schaltet LED1, wenn das Handy (iBeacon-UUID `A1E7A14B-1700-4A1C-9E5A-0C620000C147` oder Name `AI-Eval-Kit-Handy`) in der Nähe ist.

Bauen und flashen (Board an J1):
```
source zephyr/.venv/bin/activate
export ZEPHYR_SDK_INSTALL_DIR=$(ls -d zephyr/sdk/zephyr-sdk-* | head -1)
cd zephyr
west build -p always -b cy8ckit_062s2_ai/cy8c624abzi_s2d44 -d build/ble app/ble_presence
west flash -d build/ble
```
Konsole: J1, 115200 Baud; `tools/live_viewer.py` zeigt die Ansicht "Anwesenheit" mit Signalstärke.

Handy als Sender (Android): "Beacon Simulator" läuft auf aktuellem Android nicht mehr. Stattdessen **nRF Connect for Mobile** (Nordic Semiconductor, Paket `no.nordicsemi.android.mcp`), Reiter Advertiser, neuer Eintrag:
- Connectable aus, Advertising-Intervall 100 bis 250 ms, Tx-Power hoch.
- Record "Manufacturer data": Company ID `004C`, Data `0215A1E7A14B17004A1C9E5A0C620000C14700010001C5` (iBeacon mit der UUID oben, Major 1, Minor 1, Tx-Power -59 dBm).
- Alternative ohne Hex: Bluetooth-Gerätename des Handys auf `AI-Eval-Kit-Handy` setzen und im Advertiser "Device name" einschließen.
- Akku-Optimierung für nRF Connect ausschalten, sonst stoppt Android das Senden im Hintergrund.

Besonderheiten:
- `app/ble_presence/boards/cy8ckit_062s2_ai.overlay` ergänzt den Bluetooth-Knoten (SCB2 auf P3.0 bis P3.3, REG_ON P3.4), den das Board-DTS von Zephyr nicht hat.
- `patches/0001-bt-hci-uart-infineon-longer-stabilization-delay.patch`: Der CYW43439 braucht nach dem Firmware-Download etwa 1 s statt 250 ms, sonst bleibt das zweite HCI-Reset unbeantwortet. `setup.sh` wendet den Patch an; bei einem bestehenden Workspace `git -C zephyr apply patches/*.patch`.
- Details und Fehlersuche: SETUP-PROTOKOLL.md Abschnitte 55 bis 59.

## Setup-Skript
`setup.sh` legt alles unter `~/Dev/AI_Eval_Kit/zephyr/` an: `west init -l manifest` mit dem versionierten `manifest/west.yml`, `west update`, SDK, Blobs, Patches. Es lädt mehrere GB; Dauer beim ersten Lauf etwa 10 Minuten. Bei einem bestehenden Workspace ist es wiederholbar. Neue Abhängigkeit: Eintrag in `manifest/west.yml`, dann `west update`.

## Bewertung
Mächtigster und am besten dokumentierter Open-Source-Weg, aber steilste Lernkurve (west, Kconfig, Devicetree). Lohnt sich, wenn das Board später produktiv eingesetzt werden soll.
