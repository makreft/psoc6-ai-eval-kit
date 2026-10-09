# Option 2: MicroPython auf dem CY8CKIT-062S2-AI

Stand: 2026-10-08. Vorbereitet, noch nicht geflasht. Siehe SETUP-PROTOKOLL.md Abschnitt 27 für die Einordnung.

## Was das ist
Infineon pflegt einen offiziellen MicroPython-Port für PSOC 6 (Repo `Infineon/micropython`, Branch `ports-psoc6-ifx`, Doku https://ifx-micropython.readthedocs.io). Das AI-Kit ist unterstützt. Man bekommt eine Python-REPL auf dem Board und spricht LEDs, Taster, I2C-Sensoren und Wi-Fi direkt aus Python an. Kein Infineon-Konto nötig.

## Was hier liegt
- `fetch.sh` holt `firmware/mpy-psoc6_CY8CKIT-062S2-AI.hex` aus Infineons GitHub-Release v1.4.1 (2026-01-15) und prüft die SHA-256 `6b252bbe…`. Die Datei selbst ist nicht versioniert. 3,4 MB Intel-HEX, belegt 0x10000000 bis etwa 0x10120000 (knapp 1,2 MB), also nur den internen Flash.

## Flashen (Board an J1)
Überschreibt die DEEPCRAFT-Streaming-Firmware. Die lässt sich jederzeit zurückspielen (unten).
```
cd ~/Dev/AI_Eval_Kit
micropython/fetch.sh
tools/flash.sh program micropython/firmware/mpy-psoc6_CY8CKIT-062S2-AI.hex
```

## REPL
Laut Doku meldet sich die REPL auf dem KitProg3-UART (J1, 115200 8N1):
```
python3 -m serial.tools.miniterm /dev/ttyACM0 115200      # Beenden: Strg+]
```
Komfortabler ist `mpremote` (Dateien kopieren, Skripte starten, REPL):
```
python3 -m venv ~/Dev/AI_Eval_Kit/micropython/.venv
~/Dev/AI_Eval_Kit/micropython/.venv/bin/pip install mpremote
~/Dev/AI_Eval_Kit/micropython/.venv/bin/mpremote connect /dev/ttyACM0 repl
```
Ob die REPL zusätzlich über den USB-Port J2 erreichbar ist, wurde nicht geprüft.

## Erste Schritte
```python
from machine import Pin, I2C
import time
led = Pin("P5_3", Pin.OUT)          # LED1 laut Zephyr-Pinbelegung, Name ggf. in der Quickref prüfen
for _ in range(5):
    led.toggle(); time.sleep(0.3)
i2c = I2C(0)                        # Bus-Nummer und Pins: siehe Quickref für das AI-Kit
print([hex(a) for a in i2c.scan()]) # erwartet u. a. BMI270 (0x68 oder 0x69), DPS368 (0x77), BMM350 (0x14)
```
Quickref: https://ifx-micropython.readthedocs.io/en/latest/psoc6/quickref.html

## ML-Modelle
Infineon beschreibt im Hackster-Artikel "Edge AI made easy with DEEPCRAFT MicroPython" (Teil I und II), wie DEEPCRAFT-Modelle in MicroPython laufen. Dafür ist ein Konverter-Repo nötig, das in ein DEEPCRAFT-Studio-Projekt gelegt wird. Nicht geprüft, ob das ohne Studio (Windows) geht.
https://www.hackster.io/Infineon_Team/edge-ai-made-easy-with-deepcraft-micropython-part-ii-fa5edf

## Zurück zur Streaming-Firmware
```
tools/flash.sh program firmware/streaming-firmware-2026-01-07.hex
```

## Bewertung
Schnellster Weg, die Sensoren interaktiv kennenzulernen. Für Stromsparen, Echtzeit und das Infineon-C-Beispiel ungeeignet.
