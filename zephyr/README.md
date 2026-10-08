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
1. Setup-Skript ausführen (siehe unten): Python-venv mit `west`, schlanker Workspace nur mit den Modulen für Infineon, Zephyr SDK (ARM-Toolchain).
2. Binär-Blob für den CM0+ holen (Pflicht für dieses Board): `west blobs fetch hal_infineon`
3. Bauen: `west build -b cy8ckit_062s2_ai/cy8c624abzi_s2d44 zephyr/samples/hello_world`
4. Flashen über unser Infineon-OpenOCD (Board an J1):
   `west config build.cmake-args -- "-DOPENOCD=$HOME/Dev/AI_Eval_Kit/tools/openocd/bin/openocd"` einmalig, dann `west flash`
5. Konsole: J1, 115200 8N1, erwartet "Hello World! cy8ckit_062s2_ai".

Für das IMU-Ziel: Zephyr hat ein TensorFlow-Lite-Micro-Modul (`modules/lib/tflite-micro`, Sample `samples/modules/tflite-micro/magic_wand` erkennt Gesten per IMU) und den BMI270-Treiber. Das wäre der direkteste Zephyr-Weg zur Bewegungserkennung.

## Setup-Skript
`setup.sh` legt alles unter `~/Dev/AI_Eval_Kit/zephyr/` an. Es lädt mehrere GB (Zephyr-Quellen, HAL, SDK) und ist noch nicht ausgeführt worden. Vorher Skript lesen.

## Bewertung
Mächtigster und am besten dokumentierter Open-Source-Weg, aber steilste Lernkurve (west, Kconfig, Devicetree). Lohnt sich, wenn das Board später produktiv eingesetzt werden soll.
