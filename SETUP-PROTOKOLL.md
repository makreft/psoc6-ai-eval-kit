# Setup-Protokoll: Infineon PSOC 6 AI Evaluation Kit (CY8CKIT-062S2-AI)

Stand: 2026-10-08
Rechner: Ubuntu 24.04.5 LTS, Kernel 7.0.0-31-generic, x86_64, Hostname tinky
Projektordner: ~/Dev/AI_Eval_Kit

Dieses Dokument protokolliert alle Schritte bei der Inbetriebnahme des Boards unter Linux, inklusive Befehle, Ergebnisse und offener Punkte. Es wird fortlaufend ergänzt.

---

## Zusammenfassung (Stand 2026-10-08, 10:30 Uhr)

**Ergebnis Teil 1:** Board auf die aktuelle DEEPCRAFT-Streaming-Firmware 1.2.20251217 (Tensor Streaming Protocol 2.3) gebracht, Debugger KitProg3 auf 2.90.1804. Alles unter Linux, ohne Windows und ohne ModusToolbox-Installation. Seit Abschnitt 34 läuft auf dem Board der Arduino-Gesten-Sketch; die Streaming-Firmware liegt unter `firmware/` und ist jederzeit zurückspielbar.

| Was | Vorher | Nachher | Abschnitt |
|---|---|---|---|
| PSOC 6 Firmware | Werksfirmware "Imagimob Streaming Device", Protokoll v1, keine Konsole | Streaming-Firmware 1.2 / Protokoll 2.3, Shell auf J1, Wi-Fi-fähig | 12, 18, 21, 22 |
| KitProg3-Debugger | 2.70.1480 | 2.90.1804 | 16, 20 |
| Rechte auf USB und seriellen Port | root:dialout bzw. root:root, kein Zugriff | udev-Regeln, Gruppe plugdev, dauerhaft | 5, 13, 15, 16 |
| Sicherung Werksfirmware | keine | `firmware/backup-original-20261008-101411.bin` (2 MB, SHA-256 in Abschnitt 17) | 17 |

**Teil 2 und 3 (ab Abschnitt 25):** Marco will kein Infineon-Konto, also kein ModusToolbox. Stattdessen Infineons Arduino Core for PSOC 6 (kein Login). Das Infineon-IMU-Beispiel wurde nach Arduino portiert (`arduino/sketchbook/02-imu-gesture`), läuft auf dem Board und erkennt Schütteln und Kreisen (Abschnitt 35). MicroPython und Zephyr sind als Alternativen vorbereitet (`micropython/`, `zephyr/`). Ein lokaler Live-Viewer (`tools/live_viewer.py`, Abschnitt 37) zeigt im Browser, was der Chip sieht und erkennt.

**Teil 4 (ab Abschnitt 38):** Klatsch-Schalter ohne ML (`arduino/sketchbook/03-clap-led`): Klatschen schaltet LED1 um, Schnipsen wird über Dauer und Spitzenpegel verworfen. In drei Messreihen eingestellt; bei Umgebungslärm zeigte sich die Grenze des Ansatzes (Abschnitte 41 bis 45).

**Teil 5 (ab Abschnitt 46):** Eigenes Klatsch-Modell, komplett unter Linux ohne Konto: Aufnahme-Werkzeug, 102 Aufnahmen von Marco, Merkmalsberechnung (32 Mel-Bänder, 512-ms-Fenster, pegelnormiert), kleines CNN mit 18.275 Parametern, int8-TFLite mit 25 KB, auf dem Board mit TensorFlow Lite Micro in 12,5 KB Arena. Validierung 20/20 richtig, Praxistest bei Lärm steht aus (Abschnitte 48 bis 50). Pipeline beschrieben in `ml/clap/README.md`.

**Aktuell auf dem Board:** der Modell-Sketch 05-clap-model (seit Abschnitt 49). Alle anderen Sketches und die Streaming-Firmware sind jederzeit wieder aufspielbar (Anhang B).

**Noch offen:** siehe Anhang C.

**Nicht gemacht:** Der Datumscode (YYWW) auf dem Karton wurde nicht abgelesen. Die Einordnung als Protokoll-v1-Firmware stützt sich auf USB-Name, fehlende Shell und Bibliotheksstände von 2022 (Abschnitt 18).

---

## 1. Ausgangslage

- Marco hat das Board physisch vorliegen und die Imagimob-Anleitung verlinkt:
  https://developer.imagimob.com/deepcraft-studio/getting-started/infineon-boards/infineon-ai-evaluation-kit
- Projektordner war leer, kein Git-Repo.
- Ziel noch offen (Gestenerkennung, Audio, Radar, Sensordaten ansehen).

## 2. Recherche: Was ist das Board, was braucht die Anleitung

Quellen gelesen (Imagimob Developer Portal, Infineon-Doku, Codebeispiele):

**Hardware**
- MCU: PSOC 6 (CY8C624ABZI-S2D44), 512 Mbit NOR-Flash
- Wireless: CYW43439 (Wi-Fi + Bluetooth)
- Sensoren: 6-Achsen-IMU (Beschleunigung + Gyro), 3-Achsen-Magnetometer, Luftdrucksensor, 60-GHz-Radar, zwei PDM-Mikrofone (IM72D128)
- Zwei USB-C-Buchsen:
  - J1: KitProg3 (Onboard-Debugger, CMSIS-DAP, UART-Bridge zur Debug-Konsole)
  - J2: PSOC 6 direkt (USB-Device, hier läuft das Sensor-Streaming zu DEEPCRAFT Studio)
- J3: Li-Ion-Akku-Anschluss (JST-PH, 3,6 bis 4,2 V)

**Firmware-Versionen laut Imagimob**
- Kits vor März 2025: Streaming-Firmware mit Tensor Streaming Protocol v1 (veraltet).
- Kits ab Februar 2025: Protokoll v2.
- HEX-Datei vom 2026-01-08: Protokoll v2, seriell UND Wi-Fi. Nur diese Version hat eine Kommandozeile auf der KitProg3-Konsole (J1, 115200 Baud) mit Befehlen wie `help`, `WIFI Setup`, `WIFI start`, `set_boardname`, `passwd` (Standardpasswort "imagimob").
- Herstellungsdatum steht als YYWW auf dem Etikett seitlich am Karton.
- Imagimob empfiehlt in jedem Fall, die neueste Firmware zu flashen.
- Download: https://developer.imagimob.com/Resources/streaming-firmware-ai-eval-kit.zip

**Offizieller Weg laut Anleitung (Windows)**
1. HEX entpacken.
2. Board über J1 anschließen.
3. ModusToolbox Programmer öffnen, Programmer "KitProg3 CMSIS-DAP BULK-...", Board "CY8CKIT-062S2-AI".
4. HEX öffnen, Connect, Program.
5. Kabel auf J2 umstecken.
6. In DEEPCRAFT Studio: Tools > Options > "Use Node Explorer with Protocol Version 2" aktivieren, dann Node Explorer > Boards.

**Sensor-Abtastraten (Streaming-Firmware)**
- Mikrofon: 8 oder 16 kHz (PDM/PCM)
- IMU: 50, 100, 200, 400 Hz (Accel, Gyro oder beides)
- Magnetometer: 50, 100, 200, 400 Hz
- Luftdruck: 8, 16, 32, 64, 128 Hz
- Radar: muss separat erfasst werden, andere Sensoren lassen sich kombinieren

**Problem für Linux**
- DEEPCRAFT Studio gibt es nur als Windows-Installer. Keine Linux- oder Web-Version gefunden.
- ModusToolbox selbst läuft offiziell auf Ubuntu 22.04, 24.04 (empfohlen) und 25.04. Standard-Installationspfad /opt/Tools/ModusToolbox/.
- KitProg3 braucht unter Linux udev-Regeln (Skript `fw-loader/udev_rules/install_rules.sh` aus den Programming Tools) und der Nutzer muss in der Gruppe `dialout` sein.
- Bekanntes Problem: Bei alter KitProg3-Firmware (vor 1.11.243) ist der KitProg3-UART unter Linux ab Kernel 4.15 lesbar, liefert aber keine Daten. Abhilfe: KitProg3-Firmware per fw-loader aktualisieren.

**Linux-taugliche Alternativen**
- Infineon-Codebeispiele (GitHub, Organisation Infineon):
  - mtb-example-ml-deepcraft-deploy-motion (IMU, Bewegungserkennung)
  - mtb-example-ml-deepcraft-deploy-audio (Keyword Spotting)
  - mtb-example-ml-deepcraft-deploy-ready-model
  - mtb-example-ml-deepcraft-streaming-protocol (Streaming-Firmware als Quelltext)
- Tensor Streaming Protocol v2 ist offen (protobuf3): https://bitbucket.org/imagimob/tensor-streaming-protocol
  Ein eigener Python-Client unter Linux ist damit machbar.
- Protokoll v1 (veraltet): https://github.com/Infineon/mtb-example-imagimob-streaming-protocol
- MicroPython-Pfad von Infineon (Hackster-Artikel "Edge AI made easy with DEEPCRAFT MicroPython").
- Modelltraining bleibt der Punkt, an dem Studio fehlt: Windows-VM, Zweitrechner oder eigenes Training in Python mit TensorFlow Lite Micro.

## 3. Bestandsaufnahme des Rechners

Befehl (gekürzt):
```
lsusb; ls /dev/ttyACM* /dev/ttyUSB*; command -v python3 pip3 node git openocd pyocd arm-none-eabi-gcc cmake make picocom minicom screen
```

Ergebnis:
- Board zu diesem Zeitpunkt nicht angesteckt (kein Cypress-Gerät, kein serieller Port).
- Vorhanden: Python 3.12.3 (mit pyserial 3.5), Node 24.12.0, npm, git, cmake, make.
- Fehlt: pip3, pipx, openocd, pyocd, ModusToolbox, arm-none-eabi-gcc, picocom, minicom, screen.
- sudo verlangt ein Passwort (kein passwortloses sudo für den Agenten).
- Gruppen von marco: adm cdrom sudo dip plugdev users lpadmin ollama docker nordvpn. **Nicht in `dialout`.**

## 4. Board an J1 angesteckt, USB-Erkennung

Befehl:
```
lsusb; ls -la /dev/ttyACM*; ls -la /dev/serial/by-id/
```

Ergebnis:
- `Bus 003 Device 093: ID 04b4:f155 Cypress Semiconductor Corp. KitProg3 CMSIS-DAP`
- Serieller Port: `/dev/ttyACM0` (root:dialout, Modus 660)
- by-id: `usb-Cypress_Semiconductor_KitProg3_CMSIS-DAP_1422125A012D2400-if02`
- Seriennummer des KitProg3: 1422125A012D2400

USB-Deskriptor (sysfs und `lsusb -v -d 04b4:f155`):
- bcdDevice 1.01, USB 2.0 Full Speed (12 Mbit/s)
- 4 Interfaces:
  - 1.0: Vendor Specific (CMSIS-DAP Bulk, EP1 OUT / EP2 IN), kein Kernel-Treiber
  - 1.1: Vendor Specific (zweiter Bulk-Kanal, EP6 IN / EP7 OUT), kein Kernel-Treiber
  - 1.2: CDC Communications (cdc_acm)
  - 1.3: CDC Data (cdc_acm), EP4 IN / EP5 OUT
- Kernel-Log: `cdc_acm 3-3:1.2: ttyACM0: USB ACM device`
- Hinweis: bcdDevice 1.01 ist NICHT die KitProg3-Firmwareversion. Die lässt sich nur mit fw-loader über die CMSIS-DAP-Schnittstelle abfragen.

## 5. Zugriff auf den seriellen Port

Erster Versuch als marco:
```
PermissionError: [Errno 13] Permission denied: '/dev/ttyACM0'
```

Marco hat daraufhin ausgeführt:
```
sudo chmod a+rw /dev/ttyACM0
```
Danach: `crw-rw-rw- 1 root dialout 166, 0 /dev/ttyACM0`. Gilt nur bis zum nächsten Umstecken.

Dauerhafte Lösung (noch nicht erledigt, wirkt erst nach Ab- und Anmelden):
```
sudo usermod -aG dialout marco
```

## 6. Firmware-Prüfung über die KitProg3-Konsole (J1)

Idee: Die neue Firmware (2026-01-08) hat eine Kommandozeile auf dem KitProg3-UART bei 115200 Baud. Antwortet `help`, ist die neue Firmware drauf.

### 6.1 Erster Versuch: stdlib termios, 115200 8N1, raw

Skript: `tools/probes/serial_probe.py` (öffnet Port, sendet CRLF, dann `help` + CRLF, liest je 1 bis 2 s).

Ergebnis: Keine Antwort, 0 Bytes.

### 6.2 Zweiter Versuch: pyserial, DTR/RTS gesetzt, mehrere Baudraten und Zeilenenden

Skript: `tools/probes/serial_sweep.py`
- Passives Mithören bei 115200 Baud für 3 s: nichts (Firmware streamt also nicht von selbst auf J1).
- Baudraten 115200, 9600, 921600, 230400, 460800, 57600, 38400, 1000000, 2000000
- Zeilenenden CRLF, CR, LF
- DTR und RTS explizit auf high

Ergebnis: Bei keiner Kombination auch nur ein Byte empfangen.

### 6.3 Interpretation

Zwei mögliche Ursachen:
1. **Wahrscheinlich:** Auf dem PSOC 6 läuft eine Firmware ohne Konsole auf dem KitProg-UART. Das trifft auf die Auslieferungs-Firmware zu (Protokoll v1 oder v2 seriell-only). Die Wi-Fi-Kommandozeile gibt es erst seit der HEX-Datei vom 2026-01-08. Ein Kit, das vor 2026 gebaut wurde und nie neu geflasht wurde, kann diese Konsole nicht haben.
2. **Weniger wahrscheinlich:** Das bekannte KitProg3-UART-Leseproblem unter Linux bei alter KitProg3-Firmware. Lässt sich erst mit fw-loader ausschließen.

In beiden Fällen ist der empfohlene nächste Schritt derselbe: die aktuelle Streaming-Firmware flashen.

## 7. Erster Maßnahmenplan (aufgestellt nach Abschnitt 6, hier mit Ergebnis)

- [ ] Datumscode (YYWW) auf dem Karton-Etikett ablesen. Nicht gemacht, war am Ende nicht nötig.
- [x] Kabel auf J2 umstecken und USB-Produktstring prüfen. Ergebnis: alte Firmware (Abschnitt 12).
- [x] udev-Regeln für KitProg3 installieren. Erledigt mit Erklärung (Abschnitte 13, 15, 16).
- [x] Zugriff auf den seriellen Port dauerhaft lösen. Statt Gruppe `dialout` (hätte Neuanmeldung gebraucht) eine eigene udev-Regel für `plugdev` (Abschnitt 13).
- [x] Flash-Werkzeug ohne Windows: Infineon-OpenOCD und fw-loader von GitHub, kein Login nötig (Abschnitte 9, 10).
- [x] Streaming-Firmware-HEX laden und flashen. Erledigt, verifiziert (Abschnitte 8, 21, 22).
- [ ] Entscheiden, was mit dem Board gebaut werden soll, und ob ein Windows-Rechner für DEEPCRAFT Studio verfügbar ist. Offen.

Die ersten Wegwerf-Skripte zur Konsolenprüfung (`serial_probe.py`, `serial_sweep.py`, `serial_login.py`) liegen zur Nachvollziehbarkeit unter `tools/probes/`. Für den Alltag ersetzt sie `tools/console.py` (Abschnitt 14).

## 8. Streaming-Firmware-Zip von Imagimob untersucht

Download (öffentlich, kein Login):
```
curl -sSL -o streaming-firmware-ai-eval-kit.zip https://developer.imagimob.com/Resources/streaming-firmware-ai-eval-kit.zip
```
Größe 1.006.704 Bytes, HTTP 200. Zunächst isoliert im Session-Scratchpad entpackt (nichts daraus ausgeführt), in Abschnitt 10 nach `firmware/` übernommen.

Inhalt: genau eine Datei
- `mtb-example-ml-deepcraft-streaming-protocol.hex`, 2.716.605 Bytes, Zeitstempel 2026-01-07 11:31, 60.381 Zeilen Intel-HEX
- Dekodierte Binärgröße ca. 966 KB, Startadresse 0x10000000 (PSOC 6 Flash)

Strings aus dem HEX dekodiert (bestätigt, was die Doku verspricht):
- Konsolenbefehle: `version` (Build- und Versionsinfo), `wifi` mit Unterbefehlen `setup`, `start`, `stop`, `set_ssid`, `set_passwd`, `set_security`, `tspd` (Tensor Streaming Protocol Service verwalten)
- Ausgaben: "Firmware Version : %ld.%ld.%ld.%ld", "Protocol Version : %ld.%ld.%ld.%ld"
- USB-Produktname: "DEEPCRAFT Streaming Device"
- UPnP-Discovery für Studio: "SERVER: UPnP/1.0 DEEPCRAFT-Board/1.0", dazu eine kleine eingebettete HTML-Statusseite mit Firmware- und Protokollversion
- WLAN-Firmware CYW43439: WIFI5-v3.3.3, 7.95.88 (2024-06-05)
- Dateisystem littlefs, lwIP + FreeRTOS, Build-Pfad des Imagimob-Entwicklers (Windows)

Fazit: Das ist die Firmware aus dem GitHub-Repo Infineon/mtb-example-ml-deepcraft-streaming-protocol, vorkompiliert. Sie meldet sich am KitProg3-UART (J1, 115200 8N1) mit einer Shell. Dass unser Board dort nicht antwortet, bestätigt: die aktuelle Firmware ist NICHT drauf.

## 9. Flash-Werkzeuge für Linux ohne Infineon-Login gefunden

GitHub-Releases (geprüft über api.github.com):
- Infineon/openocd, release-v5.20.0 vom 2026-10-05: `openocd-5.20.0.5177-linux.tar.gz` (5,4 MB). Infineons OpenOCD-Fork mit PSOC-6- und KitProg3-Unterstützung. Damit lässt sich die HEX-Datei per CMSIS-DAP flashen.
- Infineon/Firmware-loader, 3.14.0 vom 2026-10-05: `fw-loader-3.14.0.2771-kitprog3-package-2.90.0.1804-linux.zip` (24 MB). Liest die KitProg3-Firmwareversion aus, kann KitProg3 auf 2.90.0 aktualisieren, enthält die udev-Regeln.
- Das Streaming-Firmware-Repo selbst hat keine GitHub-Releases, die HEX kommt nur von Imagimob.

Damit ist der komplette Flash-Weg unter Linux ohne ModusToolbox-Installation und ohne Windows möglich:
1. udev-Regeln aus dem fw-loader-Paket installieren (einmalig sudo).
2. fw-loader: KitProg3-Version prüfen, ggf. aktualisieren.
3. OpenOCD: HEX auf den PSOC 6 flashen.
4. Kabel auf J2, prüfen, ob sich "DEEPCRAFT Streaming Device" meldet.
5. Kabel zurück auf J1, Konsole mit `version` abfragen.

## 10. Werkzeuge heruntergeladen, geprüft und ins Projekt übernommen

Beide Pakete aus den GitHub-Releases geladen (HTTP 200), in isolierten Ordnern entpackt und untersucht, bevor etwas ausgeführt wurde.

**Infineon OpenOCD 5.20.0.5177**
- Binary `bin/openocd`, dynamisch gelinkt, bringt eigene `libusb-1.0.so.0` mit. `ldd` meldet keine fehlenden Bibliotheken.
- Relevante Konfigs: `scripts/interface/kitprog3.cfg`, `scripts/interface/cmsis-dap.cfg`, `scripts/target/psoc6_2m.cfg` (der PSOC 6 auf dem Kit hat 2 MB Flash, CY8C624ABZI-S2D44).
- udev-Regeln im Paket (`60-openocd.rules`, `66-wiced-JTAG.rules`) enthalten KEINE Regel für KitProg3 (04b4:f155). Dafür braucht es die Regel aus dem fw-loader-Paket.

**Infineon fw-loader 3.14.0.2771 mit KitProg3-Firmware 2.90.1804**
- Binary `fw-loader/bin/fw-loader`, Qt6-basiert, alle Bibliotheken im Paket (`lib/`), keine fehlenden Abhängigkeiten.
- Startet ohne Installation: `--help` und `--device-list` laufen. Letzteres meldete "No connected devices" (zu dem Zeitpunkt war das Board aber bereits vom J1-Port getrennt, siehe Abschnitt 11).
- Enthält `kp-firmware/kitprog3.hex` und `.cyacd` (KitProg3 2.90.1804) für ein Update des Debuggers.
- `udev_rules/install_rules.sh` geprüft, Inhalt harmlos: kopiert `57-cypress_programmer.rules` nach `/etc/udev/rules.d/`, dann `udevadm control --reload-rules` und `udevadm trigger --action=add`.
- `57-cypress_programmer.rules` setzt für KitProg2/3 und MiniProg4 (u. a. 04b4:f154, f155, f166) MODE 660, GROUP plugdev, TAG uaccess. Marco ist in `plugdev`, damit reicht die Regel für CMSIS-DAP-Zugriff. Der serielle Port `/dev/ttyACM*` bleibt davon unberührt (tty-Subsystem, Gruppe dialout).

**Ins Projekt kopiert**
```
~/Dev/AI_Eval_Kit/
  tools/openocd/          Infineon OpenOCD 5.20.0.5177 (17 MB)
  tools/fw-loader/        fw-loader 3.14.0.2771 (59 MB)
  tools/kp-firmware/      KitProg3-Firmware 2.90.1804 (2,9 MB)
  firmware/streaming-firmware-2026-01-07.hex   (2,6 MB)
  firmware/streaming-firmware-ai-eval-kit.zip  (Original-Zip)
```
SHA-256:
- HEX: 84c7b7a287145cad87f06644f327b951a35ba50a6a1f39868dc7cb13e0a722ed
- ZIP: e6ec0d26c929dad628e6388dfd374e024e4b0a6b94eb86cd47197179ba58cd9f

**Erkenntnis aus der Firmware-README (Infineon/mtb-example-ml-deepcraft-streaming-protocol)**
- Nach dem Start zeigt die Firmware auf dem KitProg3-UART ein Banner und fragt nach einem Login-Passwort (Standard `imagimob`). Erst danach sind `help`, `version`, `wifi setup`, `wifi start` verfügbar.
- Über J2 (USB-Device) meldet sich das Board als serielles Gerät für Studio. LED1 blinkt bis USB verbunden ist, dann Dauerlicht.
- TSP-Server über Wi-Fi läuft auf Port 14325.
- Mein `help` ohne vorheriges Passwort hätte also selbst bei neuer Firmware nur eine Fehlermeldung ergeben. Da aber gar nichts zurückkam (nicht einmal ein Prompt oder Echo), bleibt der Schluss: keine Konsole auf J1, also alte Firmware.

## 11. Konsolen-Test mit Login abgebrochen: Board nicht mehr an J1

Ein erneuter Test mit Passwort `imagimob`, dann `version` und `help` schlug fehl mit `Permission denied` auf `/dev/ttyACM0`. Gleichzeitig: kein Cypress-Gerät mehr unter `/dev/bus/usb/003/`, fw-loader meldet "No connected devices". Das Board wurde also vom J1-Port getrennt (vermutlich auf J2 umgesteckt). Das `chmod a+rw` von vorhin ist damit hinfällig, bei jedem Neuanstecken legt udev den Port wieder mit 660 an.

## 12. Board an J2: Firmware identifiziert

Marco hat das Kabel auf J2 (PSOC 6 USB-Device) umgesteckt. Kernel-Log 09:50:27 Disconnect von KitProg3, 09:50:31 neues Gerät.

Ergebnis `lsusb` und sysfs:
```
Bus 003 Device 094: ID 058b:027d Infineon Technologies Imagimob Streaming Device
  manufacturer: Infineon Technologies
  product:      Imagimob Streaming Device
  serial:       (unlesbare Bytes, nicht initialisiert)
  bcdDevice:    1.00
  Interface 1.0: cdc_acm -> /dev/ttyACM0 (root:dialout, 660)
  by-id: usb-Infineon_Technologies_Imagimob_Streaming_Device-if00
```

**Bewertung: Auf dem Board läuft die alte Auslieferungs-Firmware.**
- Die aktuelle Firmware (HEX vom 2026-01-07) nennt sich laut dekodierten Strings "DEEPCRAFT Streaming Device". Das Board meldet sich dagegen noch mit dem alten Markennamen "Imagimob Streaming Device".
- Der Seriennummern-String ist Datenmüll, auch das passt zur alten Firmware.
- Keine Konsole auf J1 (Abschnitt 6) deckt sich damit.
- Ob Protokoll v1 oder v2 (seriell-only) läuft, lässt sich am USB-Namen nicht sicher ablesen. Die Imagimob-Namensgebung spricht für die Zeit vor der Umbenennung zu DEEPCRAFT, also Protokoll v1 (Kits vor März 2025). Für den weiteren Weg ist das egal: Imagimob empfiehlt in jedem Fall das Update auf die aktuelle Firmware.

## 13. Vorbereitung für das Flashen unter Linux

Eigene udev-Regel angelegt: `tools/58-psoc6-ai-kit-tty.rules`. Sie gibt die tty-Knoten von KitProg3 (J1) und dem Streaming-Device (J2) für die Gruppe `plugdev` frei, in der Marco bereits ist. Damit entfällt das wiederholte `chmod` und auch der Umweg über `dialout` mit Neuanmeldung.

Einmalig mit sudo auszuführen (installiert Infineons Regel für CMSIS-DAP plus unsere tty-Regel):
```
sudo cp ~/Dev/AI_Eval_Kit/tools/fw-loader/udev_rules/57-cypress_programmer.rules ~/Dev/AI_Eval_Kit/tools/58-psoc6-ai-kit-tty.rules /etc/udev/rules.d/ && sudo udevadm control --reload-rules && sudo udevadm trigger --action=add
```

Geplanter Ablauf danach (Kabel wieder an J1):
1. `fw-loader --device-list` und `--info`: KitProg3-Firmwareversion lesen. Falls älter als im Paket (2.90.1804), Update mit `--update-kp3`.
2. OpenOCD: aktuellen Flash-Inhalt (2 MB) als Sicherung nach `firmware/backup-original-<datum>.bin` auslesen, bevor etwas überschrieben wird.
3. OpenOCD: `firmware/streaming-firmware-2026-01-07.hex` programmieren und verifizieren.
4. Konsole auf J1 (115200 8N1): Banner abwarten, Login `imagimob`, `version` ausführen, Passwort mit `passwd` ändern.
5. Kabel auf J2: Prüfen, ob sich "DEEPCRAFT Streaming Device" meldet.

## 14. OpenOCD-Aufruf für dieses Board ermittelt, Hilfsskripte angelegt

Aus `tools/openocd/scripts` und dem mitgelieferten "OpenOCD CLI User Guide" (002-26234 Rev. *V, 2026-09-23):
- `target/psoc6_2m.cfg` ist als veraltet markiert und verweist auf `target/infineon/cy8cxxa.cfg` (PSOC 62xA, 2 MB Flash, Flash-Treiber psoc6_2m, SMIF-Loader CY8C6xxA_SMIF.FLM).
- Offizielles Beispiel im User Guide:
  ```
  openocd -s ../scripts -f interface/kitprog3.cfg -f target/infineon/cy8cxxa.cfg -c "program firmware.hex verify exit"
  ```
- `interface/kitprog3.cfg` sucht fw-loader und kp-firmware relativ zum Installationspfad, um die KitProg3-Version prüfen zu können. In unserer Ordnerstruktur (`tools/openocd`, `tools/fw-loader`, `tools/kp-firmware`) findet es beide über den Fallback `tools_dir/fw-loader` bzw. `tools_dir/kp-firmware`.
- Die Streaming-HEX belegt ausschließlich den internen Hauptflash: Typ-04-Records von 0x1000 bis 0x100E, also 0x10000000 bis unter 0x100F0000 (knapp 1 MB). Kein SFLASH, kein externes QSPI. Eine Sicherung des 2-MB-Hauptflashs reicht daher, um den Auslieferungszustand wiederherstellen zu können.

Hilfsskripte im Projekt:
- `tools/flash.sh info|backup|program [HEX]|reset`: OpenOCD-Wrapper. `backup` liest 0x10000000 bis 0x10200000 per `dump_image` nach `firmware/backup-original-<Zeitstempel>.bin`. `program` nutzt den offiziellen Aufruf mit `verify reset exit`.
- `tools/console.py [PORT] [--listen N] CMD...`: Konsole auf J1 mit pyserial, erkennt den Passwort-Prompt und sendet `imagimob`, führt danach die angegebenen Befehle aus (z. B. `version`, `help`).
- `tools/58-psoc6-ai-kit-tty.rules`: udev-Regel für die seriellen Ports (siehe Abschnitt 13).

## 15. udev-Regeln installieren: Schritt für Schritt mit Erklärung

**Hintergrund: Was udev macht**
udev ist der Teil von Linux, der beim Anstecken eines Geräts die Gerätedateien unter `/dev` anlegt (z. B. `/dev/ttyACM0` oder `/dev/bus/usb/003/094`) und dabei Besitzer, Gruppe und Rechte setzt. Welche Rechte ein Gerät bekommt, steht in Regeldateien:
- `/usr/lib/udev/rules.d/`: Regeln der Distribution, nicht anfassen.
- `/etc/udev/rules.d/`: Regeln des Administrators, hier legen wir unsere ab.
Die Dateien werden in alphabetischer Reihenfolge abgearbeitet, daher die Zahlen am Anfang (57, 58). Eine Regel ist eine Zeile aus Bedingungen (`==`, z. B. Hersteller-ID) und Zuweisungen (`=`, z. B. Gruppe).

Ohne passende Regel bekommt ein serieller Port standardmäßig `root:dialout` mit Rechten 660, und die USB-Gerätedatei `root:root` 664. Deshalb schlugen unsere Zugriffe fehl, und deshalb war das `chmod a+rw` von Abschnitt 5 nach dem Umstecken wieder weg: udev legt die Datei bei jedem Anstecken neu an.

Die drei Zuweisungen in beiden Regeldateien bedeuten:
- `MODE="0660"`: Besitzer und Gruppe dürfen lesen und schreiben, alle anderen nichts.
- `GROUP="plugdev"`: Die Gruppe ist `plugdev`. Marco ist Mitglied (siehe Abschnitt 3), muss sich also nicht neu anmelden.
- `TAG+="uaccess"`: Zusätzlich setzt systemd-logind eine ACL für den gerade lokal angemeldeten Benutzer. Doppelter Boden, falls jemand nicht in `plugdev` ist.

**Schritt 1: Infineons Regel für den Debugger kopieren**
```
sudo cp ~/Dev/AI_Eval_Kit/tools/fw-loader/udev_rules/57-cypress_programmer.rules /etc/udev/rules.d/
```
Kopiert die Regeldatei aus dem fw-loader-Paket nach `/etc/udev/rules.d/`. Sie betrifft USB-Geräte mit Hersteller-ID 04b4 (Cypress, heute Infineon) und den Produkt-IDs von KitProg2, KitProg3 (f154, f155, f166, f168, f169, f16a) und MiniProg4. Damit dürfen OpenOCD und fw-loader über libusb mit der CMSIS-DAP-Schnittstelle des Debuggers sprechen, ohne root. Nur so lässt sich der PSOC 6 flashen. Inhalt der Datei wurde in Abschnitt 10 vollständig gelesen, sie enthält nur solche Rechtezuweisungen.

**Schritt 2: Unsere Regel für die seriellen Ports kopieren**
```
sudo cp ~/Dev/AI_Eval_Kit/tools/58-psoc6-ai-kit-tty.rules /etc/udev/rules.d/
```
Infineons Regel deckt nur die USB-Gerätedateien ab, nicht die seriellen Ports (`/dev/ttyACM*`), die im tty-Subsystem leben. Unsere Datei ergänzt das für:
- die UART-Bridge des KitProg3 (J1, Konsole der Firmware),
- das USB-Device des PSOC 6 (J2, Streaming-Port, Hersteller-ID 058b Infineon, alte wie neue Firmware).
Ergebnis: `/dev/ttyACM0` gehört nach dem Anstecken `root:plugdev` statt `root:dialout`, und wir können die Konsole ohne `chmod` lesen.

**Schritt 3: Regeln neu einlesen**
```
sudo udevadm control --reload-rules
```
Der udev-Dienst läuft dauerhaft im Hintergrund und hat die Regeln beim Start gelesen. Dieser Befehl sagt ihm, dass er die Regeldateien neu einlesen soll. Er ändert noch nichts an bereits angesteckten Geräten.

**Schritt 4: Regeln auf bereits angesteckte Geräte anwenden**
```
sudo udevadm trigger --action=add --subsystem-match=usb --subsystem-match=tty
```
Spielt für alle USB- und tty-Geräte das Ereignis "Gerät hinzugefügt" noch einmal ab, sodass udev die neuen Regeln auf schon verbundene Geräte anwendet. Die Geräte werden dabei nicht neu erkannt oder getrennt, nur Rechte und Gruppen werden neu gesetzt. Infineons eigenes `install_rules.sh` macht dasselbe ohne die `--subsystem-match`-Einschränkung, also für alle Gerätetypen; die eingeschränkte Form reicht uns und ist weniger Rauschen. Alternativ hätte auch Abziehen und Neuanstecken des Boards denselben Effekt.

**Kontrolle (ohne sudo)**
```
ls -la /etc/udev/rules.d/57-cypress_programmer.rules /etc/udev/rules.d/58-psoc6-ai-kit-tty.rules
ls -la /dev/ttyACM0
getfacl /dev/ttyACM0
```
Erwartet: beide Dateien vorhanden, `/dev/ttyACM0` mit Gruppe `plugdev`, und in der ACL ein Eintrag `user:marco:rw-` (das ist der uaccess-Effekt).

**Rückgängig machen**
```
sudo rm /etc/udev/rules.d/57-cypress_programmer.rules /etc/udev/rules.d/58-psoc6-ai-kit-tty.rules
sudo udevadm control --reload-rules
```

## 16. udev-Regeln installiert, Debugger identifiziert

Marco hat die vier Befehle aus Abschnitt 15 ausgeführt und das Kabel zurück auf J1 gesteckt.

**Kontrolle**
```
-rw-r--r-- root root /etc/udev/rules.d/57-cypress_programmer.rules
-rw-r--r-- root root /etc/udev/rules.d/58-psoc6-ai-kit-tty.rules
crw-rw----+ root plugdev /dev/ttyACM0            (vorher root dialout)
crw-rw----+ root plugdev /dev/bus/usb/003/095    (vorher root root)
ACL auf /dev/ttyACM0: user:marco:rw-             (uaccess-Effekt)
```
Beide Regeln wirken. Der Debugger ist jetzt Gerät 095 (neu enumeriert durch das Umstecken).

**Zwei Fehler in meinen Skripten behoben**
- Der fw-loader lag unter `tools/fw-loader/bin/fw-loader`, ich hatte `tools/fw-loader/fw-loader/bin/` angenommen (Kopierebene vertauscht).
- `tools/console.py`: Der optionale Positionsparameter für den Port hat den ersten Befehl ("version") als Portnamen verschluckt. Jetzt ist der Port eine Option `--port`, Standard bleibt die automatische Suche nach dem KitProg3-Port unter `/dev/serial/by-id`.

**fw-loader `--device-list`**
```
1: KitProg3 CMSIS-DAP BULK-1422125A012D2400   FW Version 2.70.1480 [outdated]
Warning: The firmware on the KitProg3 ... is outdated. Use --update-kp3 to update.
```
Das Paket enthält KitProg3 2.90.1804. Ein Update ist möglich, aber eine eigene Entscheidung (siehe Abschnitt 17).

**fw-loader `--info`**
```
The name: CY8CKIT-062S2-AI
Mbed Board ID: 1914
Silicon ID: E4531202
Target Info: PSoC6 2M Not secure
Programming Properties: KitProg3 SWD, DAPLink Mode, Reset Mode Programming
Bridging Properties: Primary UART, I2C Bridging
QSPI Properties: S25HS512T
Connectivity options: WIFI CYW43439
```
Bestätigt: Es ist tatsächlich das AI-Kit, der PSOC 6 hat 2 MB Flash und ist nicht als "Secure" konfiguriert, Programmierung über SWD ist freigegeben. Externer QSPI-Flash ist ein S25HS512T (512 Mbit).

**Konsole auf J1 (mit korrigiertem Skript)**
4 Sekunden passiv mitgehört, dann Enter, `version`, `help`: weiterhin 0 Bytes. Jetzt mit funktionierenden Rechten eindeutig: Die laufende Firmware hat keine Konsole auf dem KitProg3-UART.

## 17. OpenOCD-Verbindung getestet und Werksfirmware gesichert

**`tools/flash.sh info`** (nur verbinden und Chip erkennen, nichts verändern)
```
** Flash Boot version: 3.1.0.378
** SFlash version: 0x47530
** Chip Protection: NORMAL
Info : [psoc6.cpu.cm0] Examination succeed
Info : [psoc6.cpu.cm4] Cortex-M4 r0p1 processor detected
    TargetName         Type       Endian TapName            State
 0* psoc6.cpu.cm0      cortex_m   little psoc6.cpu          unknown
 1  psoc6.cpu.cm4      cortex_m   little psoc6.cpu          unknown
```
Die Debug-Kette KitProg3 -> SWD -> PSOC 6 funktioniert. Beide Kerne (Cortex-M0+ und Cortex-M4) werden erkannt, Chip-Schutz steht auf NORMAL (nicht gesperrt). OpenOCD warnt nur, dass die KitProg3-Firmware 2.70.1480 veraltet ist und 2.90.1804 in `tools/kp-firmware` liegt. Das Flashen funktioniert trotzdem.

**`tools/flash.sh backup`**
OpenOCD hat den Chip per "Test Mode"-Acquire (XRES-Reset) angehalten und den kompletten Hauptflash gelesen:
```
dumped 2097152 bytes in 5.643945s (362.867 KiB/s)
firmware/backup-original-20261008-101411.bin   2.097.152 Bytes
SHA-256: a7107f1e5605a30be8ac347c1ad369fcdde15f4dd7716a955fa9741421a4177b
```
Damit lässt sich der Auslieferungszustand jederzeit wiederherstellen:
```
tools/openocd/bin/openocd -s tools/openocd/scripts -f interface/kitprog3.cfg -f target/infineon/cy8cxxa.cfg \
  -c "program firmware/backup-original-20261008-101411.bin 0x10000000 verify reset exit"
```
Hinweis: Nach dem Auslesen bleibt der Chip angehalten, deshalb anschließend `tools/flash.sh reset` ausgeführt (siehe unten).

## 18. Analyse der gesicherten Werksfirmware

Strings aus `backup-original-20261008-101411.bin` extrahiert (nur gelesen, nichts ausgeführt):

**Belegung**
- Letzte belegte Adresse 0x1007B4F7, also rund 493 KB der 2 MB genutzt. Rest ist leer (0x00/0xFF).
- Vektortabelle: Initial-Stack 0x08002000 (SRAM), Reset-Handler 0x1000011B. Normale, unverschlüsselte Anwendung ab Flash-Anfang.

**Identität**
- USB-Produktname "Imagimob Streaming Device", Board-String "PSoC 6 AI (CY8CKIT-06S2-AI)" (Tippfehler im Original).
- Ausgabeformate "Firmware Version : %ld.%ld.%ld.%ld" und "Protocol Version : %ld.%ld.%ld.%ld" sind vorhanden, werden aber offenbar nur auf Anfrage über das Streaming-Protokoll ausgegeben, nicht über eine Konsole.
- Keine Shell-Befehle (`help`, `version`, `passwd`, `tspd`, `wifi setup`) im Binärabbild. Das erklärt die stumme Konsole endgültig.
- Wi-Fi-Code ist enthalten (WHD 2.6.0.16847, CYW43439-Firmware vom 2022-07-27, lwIP 2.1.2, Wi-Fi-Connection-Manager mit als "deprecated" markierten Befehlen), aber ohne Bedienschnittstelle. Streaming war also nur über USB (J2) nutzbar.
- USB-Stack: SEGGER emUSB-Device V3.64. Toolchain: GCC ARM 10.3-2021.07. Build-Zeitstempel in den Bibliotheken 2022-07 bis 2022-12.

**Sensortreiber in der Werksfirmware**
- BMI270: Beschleunigung und Gyroskop (125 bis 2000 dps)
- BMM350: Magnetometer
- DPS368: Luftdruck und Temperatur
- BGT60TRxx: 60-GHz-Radar (XENSIV)
- PDM/PCM: Mikrofon mit einstellbarem Gain in dB

**Einordnung**
Das ist die ursprüngliche Imagimob-Streaming-Firmware aus der Zeit vor der Umbenennung zu DEEPCRAFT, mit Bibliotheksständen von 2022. Zusammen mit dem alten USB-Namen und der fehlenden Shell ist das mit hoher Sicherheit die Protokoll-v1-Firmware (Kits vor März 2025). Im Vergleich dazu bringt die neue HEX (Abschnitt 8) Protokoll v2, Wi-Fi-Streaming mit Shell, WHD-Stand 2024 und littlefs für die Konfiguration.

**Chip zurückgesetzt**
`tools/flash.sh reset` ausgeführt (Acquire per XRES, dann `reset run`). Der PSOC 6 läuft wieder mit der Werksfirmware, nichts wurde verändert.

## 19. Offene Entscheidungen vor dem Flashen

1. **Neue Streaming-Firmware flashen?** Überschreibt die Werksfirmware. Sicherung liegt vor (Abschnitt 17), Wiederherstellung ist mit einem Befehl möglich.
2. **KitProg3 von 2.70.1480 auf 2.90.1804 aktualisieren?** Betrifft nur den Debugger-Chip (PSoC 5LP), nicht den PSOC 6. OpenOCD und fw-loader empfehlen es, das Flashen funktioniert aber auch ohne (Abschnitt 17 zeigt erfolgreiche Verbindung und Dump). Befehl wäre `tools/fw-loader/bin/fw-loader --update-kp3 all`; währenddessen verschwindet der Debugger kurz vom USB und meldet sich neu an.

## 20. KitProg3-Debugger auf 2.90.1804 aktualisiert

Marcos Entscheidung: beides machen, KitProg3 zuerst.

Befehl:
```
tools/fw-loader/bin/fw-loader --update-kp3 all
```
Ablauf laut Ausgabe:
1. Verbunden mit "KitProg3 CMSIS-DAP BULK-1422125A012D2400", erkannt: 2.70 b1480, Upgrade-Datei `tools/kp-firmware/kitprog3.cyacd` mit 2.90 b1804.
2. Debugger wechselt in den Bootloader ("KitProg3 Bootloader-5A01021222142D24", Bootloader 1.1.60).
3. KitProg-Firmware geschrieben und verifiziert, dann DAPLink-Anteil geschrieben und verifiziert.
4. Debugger meldet sich wieder als CMSIS-DAP BULK, jetzt mit FW Version 2.90.1804.
Dauer insgesamt wenige Sekunden, der Debugger war nach etwa einer Sekunde wieder am USB (jetzt Gerät 097). `/dev/ttyACM0` wurde neu angelegt, die udev-Regel hat wieder `root:plugdev` gesetzt.

Kontrolle `--device-list`: `FW Version 2.90.1804`, keine Veraltet-Warnung mehr.

## 21. Streaming-Firmware geflasht

Befehl:
```
tools/flash.sh program
```
entspricht
```
tools/openocd/bin/openocd -s tools/openocd/scripts -f interface/kitprog3.cfg -f target/infineon/cy8cxxa.cfg \
  -c "program firmware/streaming-firmware-2026-01-07.hex verify reset exit"
```

Ausgabe (gekürzt):
```
** Silicon: 0xE453, Family: 0x102, Rev.: 0x12 (A1)
** Detected Device: CY8C624ABZI-S2D44
** Detected Main Flash size, kb: 2048
** Chip Protection: NORMAL
** Device acquired successfully
** Programming Started **
auto erase enabled
Info : Flash write discontinued at 0x100018d0, next section at 0x10002000
wrote 966144 bytes from file .../streaming-firmware-2026-01-07.hex in 7.559775s (124.805 KiB/s)
** Programming Finished **
** Verify Started **
verified 965792 bytes in 1.665752s (566.204 KiB/s)
** Verified OK **
** Resetting Target **
```
- Zwei Abschnitte im Image: ein kleiner Block bis 0x100018D0 (CM0+-Startcode) und der Hauptteil ab 0x10002000 bis 0x100EC3D0. OpenOCD hat die Lücke und die Enden auf Schreibgrenzen aufgefüllt (304 bzw. 48 Bytes Padding), das ist normal.
- Löschen und Programmieren dauerten zusammen etwa 8 Sekunden, Verifikation 1,7 Sekunden, Ergebnis "Verified OK".
- Der Chip wurde danach zurückgesetzt und startet die neue Firmware. Mit KitProg3 2.90 gab es keine Veraltet-Warnung mehr.

## 22. Neue Firmware bestätigt: Konsole auf J1 antwortet

Befehl:
```
tools/console.py --listen 2 --wait 2 version help
```

Ablauf: Nach dem Öffnen des Ports kamen zunächst einige unlesbare Bytes (Rest des Boot-Banners, vermutlich noch während die UART-Taktung einschwang), dann der Prompt "Enter password:". Das leere Enter des Skripts zählte als falsches Passwort, danach hat das Skript den Prompt erkannt und `imagimob` gesendet. Login erfolgreich:
```
Running script /system/login
Welcome to Imagimob TSP Shell.
Board Name             : PSOC 6 AI (CY8CKIT-06S2-AI)
Board Serial           : 290DE5CB-460B-41BF-1F19-8D0D173DAFF7
Firmware Version       : 1.2.20251217.132358
Protocol Version       : 2.3.20241218.0
GCC 11.3.1 / STDC 201710
```
- Firmware 1.2 vom 2025-12-17, Tensor Streaming Protocol 2.3. Das ist die Version aus der HEX vom 2026-01-07.
- Die Board-Seriennummer ist jetzt eine echte UUID (vorher Datenmüll im USB-Deskriptor).
- Die Shell nutzt ANSI-Steuersequenzen (Cursor, Farben), in Rohausgaben erscheinen die als `[?25l` usw. Für interaktive Nutzung eignet sich ein Terminalprogramm wie `picocom -b 115200 /dev/ttyACM0` (nicht installiert) oder `python3 -m serial.tools.miniterm /dev/ttyACM0 115200` (pyserial ist da).

`help` listet 44 Befehle. Wichtige davon:
- `dlist`, `dset`, `dapply`, `dsave`: Sensoren ("devices") anzeigen, konfigurieren, Konfiguration als Boot-Skript speichern
- `dcap`, `dhalt`: Daten direkt auf das Dateisystem des Boards aufzeichnen, ohne Studio
- `tsp2wav`: aufgezeichnete TSP-Dateien in WAV wandeln
- `tspd`: Tensor-Streaming-Dienst, `httpd`, `telnetd`, `upnpd`: Netzdienste für Studio-Discovery
- `wifi`: Unterbefehle `setup`, `start`, `stop`, `set_ssid`, `set_passwd`, `set_security`
- `passwd`: Shell-Passwort ändern (Standard `imagimob`, Firmware mahnt zur Änderung)
- Dateisystem-Befehle (`ls`, `cat`, `cp`, `mv`, `rm`, `mkdir`, `edit`, `format`, `mount`, `disk`), `ps`, `kill`, `mem`, `loglevel`, `gpio`, `sysclk`, `reboot`, `set_boardname`

Damit sind beide Updates abgeschlossen: KitProg3 2.70 -> 2.90.1804 (Abschnitt 20) und PSOC 6 von der Werks-Firmware (Protokoll v1, Sicherung in Abschnitt 17) auf Streaming-Firmware 1.2 / Protokoll 2.3 (Abschnitt 21).

## 23. Zustand der neuen Firmware abgefragt

Befehl: `tools/console.py --wait 2.5 dlist disk "ls /system" "cat /system/login" ps`

**Sensoren (`dlist`)**: alle fünf Geräte im Status "Ready"
```
0  BOARD         Board    Board configurations.
1  Microphone    Sensor   PCM Microphone
2  IMU           Sensor   Accelerometer and Gyroscope (BMI270)
3  Magnetometer  Sensor   Magnetometer (BMM350)
4  DPS           Sensor   Pressure and Temperature (DPS368)
5  Radar         Sensor   Radar device (bgt60trxx)
```
`dlist <Gerät>` zeigt Details und Optionen je Sensor.

**Dateisystem (`disk`)**: littlefs auf dem externen QSPI-Flash (S25HS512T), 64 MB gesamt, 3 MB belegt, 61 MB frei, Blockgröße 256 KB. Hier landen Aufzeichnungen mit `dcap` und die Konfiguration.

**Systemdateien (`ls /system`)**: `devices/`, `web/` (Statusseite für den HTTP-Dienst), Skripte `boot`, `login`, `net-up`, `net-down`, Datei `password` (32 Byte, vermutlich ein Hash des Shell-Passworts, nicht geprüft). `/system/login` enthält nur `version` und `echo`, daher das Banner beim Login.

**Dienste**: `tspd` kennt `start [-p <port>]` und `stop`, `wifi` kennt `setup`, `set_ssid`, `set_passwd`, `set_security`, `start`, `stop`, `save`. Ein Statusbefehl existiert nicht; den Zustand sieht man an `ps` und an der Wi-Fi-LED (LED2).

**Tasks (`ps`)**: IDLE bei 98 %, daneben tcpip_thread, usbd, WHD (Wi-Fi-Treiber), WCM-Worker, sys_shell, initd, Timer. Kein TSP-Task sichtbar, solange weder USB an J2 noch Wi-Fi verbunden ist.

**Noch offen**
- Kabel auf J2 stecken und prüfen, ob sich das Board als "DEEPCRAFT Streaming Device" meldet.
- Shell-Passwort mit `passwd` ändern (Standard `imagimob`). Nur relevant, sobald Wi-Fi aktiv ist, weil dann Telnet und HTTP erreichbar sind.
- Optional `wifi setup` für kabelloses Streaming, braucht SSID und WLAN-Passwort. Läuft interaktiv in der Shell.

---

## Anhang A: Projektstruktur

```
~/Dev/AI_Eval_Kit/
  SETUP-PROTOKOLL.md                        dieses Dokument
  BOARD-UEBERSICHT.md                       Was das Board kann, wofür es nicht taugt, Einstiegswege
  firmware/
    streaming-firmware-2026-01-07.hex       aktuelle Streaming-Firmware (Imagimob, geflasht in Abschnitt 21)
    streaming-firmware-ai-eval-kit.zip      Original-Download
    backup-original-20261008-101411.bin     Sicherung der Werksfirmware, 2 MB Hauptflash
  tools/
    openocd/                                Infineon OpenOCD 5.20.0.5177 (GitHub-Release, Linux)
    fw-loader/                              Infineon fw-loader 3.14.0.2771 (GitHub-Release, Linux)
    kp-firmware/                            KitProg3-Firmware 2.90.1804 (Teil des fw-loader-Pakets)
    arduino-cli/                            arduino-cli 1.5.1 (GitHub-Release)
    flash.sh                                OpenOCD-Wrapper: info | backup | program [HEX] | reset
    console.py                              Konsole auf J1 mit automatischem Login (Streaming-Firmware)
    live_viewer.py                          Browser-Viewer für die Arduino-Sketches (Gesten- und Klatsch-Modus)
    58-psoc6-ai-kit-tty.rules               udev-Regel für die seriellen Ports (installiert nach /etc/udev/rules.d/)
    probes/                                 erste Diagnose-Skripte, nur zur Nachvollziehbarkeit
  arduino/                                  Option 1 (aktiv): arduino-cli.yaml, .arduino15/ (Core, ca. 600 MB), sketchbook/
    sketchbook/01-blink-serial/             erster Test
    sketchbook/02-imu-gesture/              Gestenerkennung (Portierung des Infineon-Beispiels), Testlauf-Log
    sketchbook/03-clap-led/                 Klatsch-Schalter regelbasiert, drei Messreihen als CSV
    sketchbook/04-audio-stream/             Mikrofon als Rohaudio streamen (fuer Aufnahmen)
    sketchbook/05-clap-model/               Klatsch-Erkennung mit eigenem Modell (TFLM), generierte Header
    sketchbook/libraries/                   SparkFun BMI270, Chirale TensorFlowLite
  ml/                                       Trainingsumgebung (.venv mit TensorFlow, nicht versionieren)
    clap/features.py, dataset.py, train.py  Merkmale, Datensatz, Training und Export
    clap/out/                               Modell (tflite), generierte Header, Trainingsbericht
    clap/README.md                          Pipeline-Beschreibung (praesentationstauglich)
  data/audio/                               Marcos Aufnahmen (clap, snap, other, background) und index.csv
  examples/deploy-motion/                   Infineon-Original (ModusToolbox) als Referenz
  micropython/                              Option 2: Firmware v1.4.1 und README
  zephyr/                                   Option 3: README und ungetestetes setup.sh
  docs/                                     Screenshots des Viewers
  BOARD-UEBERSICHT.md                       Was das Board kann, Einstiegswege
  clap-test*.csv                            Marcos Mitschnitte aus den Klatschtests (Kopien im Sketch-Ordner)
```
Systemweit geändert: `/etc/udev/rules.d/57-cypress_programmer.rules` und `/etc/udev/rules.d/58-psoc6-ai-kit-tty.rules` (Abschnitt 15, inklusive Rückgängig-Anleitung).

## Anhang B: Schnellreferenz

Alle Befehle aus dem Projektordner, Board an J1 (KitProg3-Buchse), sofern nicht anders angegeben.

| Zweck | Befehl |
|---|---|
| Debugger und Board erkennen | `tools/fw-loader/bin/fw-loader --device-list` und `--info` |
| OpenOCD-Verbindung testen | `tools/flash.sh info` |
| Hauptflash sichern | `tools/flash.sh backup` |
| Firmware flashen | `tools/flash.sh program [pfad/zur/datei.hex]` |
| Board zurücksetzen | `tools/flash.sh reset` |
| Konsole, Befehle absetzen | `tools/console.py version dlist` |
| Konsole interaktiv | `python3 -m serial.tools.miniterm /dev/ttyACM0 115200` (Passwort `imagimob`, Beenden mit Strg+]) |
| Werksfirmware wiederherstellen | `tools/openocd/bin/openocd -s tools/openocd/scripts -f interface/kitprog3.cfg -f target/infineon/cy8cxxa.cfg -c "program firmware/backup-original-20261008-101411.bin 0x10000000 verify reset exit"` |
| Streaming zu Studio per USB | Kabel an J2, Board erscheint als "DEEPCRAFT Streaming Device" (Abschnitt 24) |
| Arduino-Sketch bauen | `tools/arduino-cli/arduino-cli compile --fqbn infineon:psoc6:cy8ckit_062s2_ai --config-file arduino/arduino-cli.yaml arduino/sketchbook/<name>` |
| Arduino-Sketch hochladen (J1) | gleicher Aufruf mit `upload -p /dev/ttyACM0` statt `compile` |
| Live-Viewer | `tools/live_viewer.py [--log datei.csv]`, dann http://localhost:8765; beenden mit Strg+C oder `fuser -k 8765/tcp` |
| Gesten-Sketch wieder aufspielen | `upload ... arduino/sketchbook/02-imu-gesture` (Build liegt unter `build/`) |
| Audio aufnehmen | Sketch 04 aufspielen, dann `tools/record_audio.py session` bzw. `background --seconds 60` |
| Modell neu trainieren | `cd ml && .venv/bin/python clap/train.py --data ../data/audio --out clap/out`, dann Header nach `arduino/sketchbook/05-clap-model/` kopieren und Sketch 05 bauen |

Wichtige Shell-Befehle der Firmware (Abschnitt 22, 23): `dlist`, `dlist <Gerät>`, `dset`, `dapply`, `dsave`, `dcap`, `dhalt`, `tsp2wav`, `wifi setup`, `wifi start`, `wifi save`, `tspd start`, `passwd`, `set_boardname`, `reboot`.

## Anhang C: Offene Punkte

1. Klatsch-Schalter: Nulldurchgangsrate als drittes Merkmal, Doppelklatsch-Muster, Schwellen an Raumlautstärke koppeln (Abschnitt 43). Nur bei Bedarf.
2. Gestenerkennung: eigenes Modell mit eigenen Daten (braucht DEEPCRAFT Studio unter Windows oder einen eigenen Trainingsweg, BOARD-UEBERSICHT.md Abschnitt 6).
3. Streaming-Firmware: falls wieder aufgespielt, Shell-Passwort mit `passwd` ändern, bevor Wi-Fi aktiviert wird; optional `wifi setup`, `wifi start`, `wifi save`.
4. MicroPython (`micropython/README.md`) und Zephyr (`zephyr/setup.sh`) ausprobieren.
5. Optional Git-Repo anlegen. Auszuschließen: `tools/openocd`, `tools/fw-loader`, `tools/arduino-cli`, `arduino/.arduino15`, `arduino/sketchbook/libraries`, `*/build/` (zusammen über 700 MB); alles davon ist über die im Protokoll genannten Quellen reproduzierbar.

## Anhang D: Quellen

- Imagimob, Getting Started AI Evaluation Kit: https://developer.imagimob.com/deepcraft-studio/getting-started/infineon-boards/infineon-ai-evaluation-kit
- Imagimob, Datenerfassung mit dem Kit: https://developer.imagimob.com/deepcraft-studio/data-preparation/data-collection/collect-data-infineon-boards/data-collection-ai-kit
- Imagimob, Tensor Streaming Protocol: https://developer.imagimob.com/getting-started/tensor-streaming-protocol
- Imagimob, Studio-Installation: https://developer.imagimob.com/install-download-studio
- Imagimob, Streaming-Firmware-Download: https://developer.imagimob.com/Resources/streaming-firmware-ai-eval-kit.zip
- Protokoll-v2-Repository: https://bitbucket.org/imagimob/tensor-streaming-protocol
- Infineon, Firmware-Quelltext: https://github.com/Infineon/mtb-example-ml-deepcraft-streaming-protocol
- Infineon, OpenOCD-Releases: https://github.com/Infineon/openocd/releases
- Infineon, fw-loader-Releases: https://github.com/Infineon/Firmware-loader/releases
- Infineon, ModusToolbox-Installationsanleitung (Linux-Hinweise): https://documentation.infineon.com/modustoolbox/docs/uqy1738949056062
- Infineon, Codebeispiele Deploy Motion / Audio / Ready Model: https://github.com/Infineon/mtb-example-ml-deepcraft-deploy-motion (analog -audio, -ready-model)
- Infineon, MicroPython-Pfad (Hackster): https://www.hackster.io/Infineon_Team/edge-ai-made-easy-with-deepcraft-micropython-part-ii-fa5edf
- Produktseite: https://www.infineon.com/cms/en/product/evaluation-boards/cy8ckit-062s2-ai/
- OpenOCD CLI User Guide (lokal): `tools/openocd/docs/OpenOCD CLI User Guide.pdf`, fw-loader und KitProg3 User Guides unter `tools/fw-loader/docs/` und `tools/kp-firmware/docs/`

## 24. Abnahme an J2: neue USB-Kennung bestätigt

Marco hat das Kabel auf J2 (PSOC 6 USB-Device) gesteckt. Kernel-Log 10:29:40 Disconnect KitProg3 (Gerät 097), 10:29:43 neues Gerät 098.

```
Bus 003 Device 098: ID 058b:027d Infineon Technologies DEEPCRAFT Streaming Device
  manufacturer:  Infineon Technologies
  product:       DEEPCRAFT Streaming Device          (vorher: Imagimob Streaming Device)
  serial:        290DE5CB-460B-41BF-1F19-8D0D173DAFF7 (vorher: unlesbare Bytes)
  bcdDevice:     1.00, 2 Interfaces (CDC Control + CDC Data), Treiber cdc_acm
  Port:          /dev/ttyACM0, root:plugdev, ACL user:marco:rw-
  by-id:         usb-Infineon_Technologies_DEEPCRAFT_Streaming_Device_290DE5CB-...-if00
```

Bewertung:
- Produktname passt zur neuen Firmware (Abschnitt 8, 12). Die USB-Seriennummer ist identisch mit der "Board Serial" aus der Konsole (Abschnitt 22), das Board ist damit eindeutig identifizierbar, auch bei mehreren Kits.
- Vendor- und Product-ID (058b:027d) sind unverändert gegenüber der Werksfirmware, deshalb greift unsere udev-Regel (Abschnitt 13) hier genauso. Der Port ist ohne weitere Rechteanpassung nutzbar.
- Der stabile by-id-Pfad eignet sich für Skripte, weil er die Seriennummer enthält und nicht von der Reihenfolge der ttyACM-Nummern abhängt.

Damit sind alle Punkte der Firmware-Prüfung und -Aktualisierung abgeschlossen. DEEPCRAFT Studio würde dieses Gerät unter Node Explorer > Boards finden (Option "Use Node Explorer with Protocol Version 2" aktivieren).

Ergänzung: 3 Sekunden passives Mithören auf dem Streaming-Port (115200 Baud, DTR/RTS gesetzt) ergab 0 Bytes. Die Firmware sendet nichts unaufgefordert, der Tensor-Streaming-Dienst wartet auf einen Client (Studio oder ein eigener TSP-v2-Client). Die Baudrate ist bei USB-CDC ohnehin nur nominell.

---

# Teil 2: IMU-Beispiel (Bewegungserkennung)

## 25. Ziel und Voraussetzungen geklärt

Marcos Entscheidung: mit dem IMU-Beispiel von Infineon anfangen (`mtb-example-ml-deepcraft-deploy-motion`), siehe BOARD-UEBERSICHT.md Abschnitt 7.

**Was das Beispiel macht (laut README)**
- Liest den BMI270 über I2C, sammelt mindestens 50 IMU-Frames und lässt dann ein von DEEPCRAFT Studio erzeugtes Modell (`model.c`/`model.h`) laufen.
- Erkennt zwei Gesten: Schütteln ("shaking") und Kreisen ("circling"), gibt pro Geste einen Konfidenzwert auf der KitProg3-Konsole aus (115200 8N1). Startbanner "DEEPCRAFT IMU Model Example".
- Das mitgelieferte Modell ist ausdrücklich nur Referenz, nicht produktionsreif. Es wurde mit dem Tensor Streaming Protocol v2 auf genau diesem Kit trainiert.
- Bibliotheken: mtb-pdl-cat1, mtb-hal-cat1, retarget-io, psoc6-middleware. Kein ML-Configurator.

**Werkzeuge, die dafür nötig sind**
- ModusToolbox 3.4 oder neuer (README getestet mit 3.7), Toolchain GNU Arm Embedded 11.3.1 (GCC_ARM, Standard).
- Board-ID `CY8CKIT-062S2-AI` ist das Standard-TARGET des Beispiels, keine Anpassung nötig.
- Unter Ubuntu 24.04 ist ModusToolbox offiziell unterstützt. Installation über das Setup-Programm (`ModusToolboxSetupInstaller_<version>_linux_x64.deb`, installiert nach `/opt/Tools`), das dann Tools-Paket, GCC und optional Programming Tools nachlädt.
- Ergebnis der Prüfung auf dem Rechner: kein ModusToolbox vorhanden, kein Installer in `~/Downloads`, 67 GB frei (reicht; MTB braucht grob 3 bis 5 GB). Typische Laufzeitbibliotheken (libusb, libgl1, libxkbcommon0, libfontconfig1) sind da, `libncurses5` fehlt (wird von alten ARM-GCC-Builds gebraucht; ob die 11.3.1 aus MTB sie braucht, zeigt sich nach der Installation).

**Download braucht ein Infineon-Konto**
Die Download-Seite https://softwaretools.infineon.com/tools/com.ifx.tb.tool.modustoolboxsetup ist eine JavaScript-Anwendung, die dahinterliegende API (`itoolspriv.infineon.com/itbhs/api/...`) antwortet ohne Anmeldung mit HTTP 401. Der Installer muss also im Browser mit Infineon-Login geladen werden. Automatisch geht das nicht und soll es auch nicht.

**Was ohne ModusToolbox schon geht**
- Quellcode des Beispiels per `git clone` holen und lesen (siehe Abschnitt 26).
- Flashen des fertigen Builds wäre mit unserem OpenOCD möglich; ModusToolbox wird nur zum Bauen gebraucht (Bibliotheken auflösen mit `make getlibs`, Kompilieren mit `make build`).

## 26. Quellcode des IMU-Beispiels geholt

```
git clone --depth 1 https://github.com/Infineon/mtb-example-ml-deepcraft-deploy-motion examples/deploy-motion
```
Stand des Repos: Commit 3fbeb60 vom 2026-06-05.

**Inhalt**
- `main.c` (20 KB): initialisiert BMI270 über I2C mit 50 Hz und 500 dps Gyro-Bereich, füttert die Samples in das Modell (`IMAI_enqueue`), holt Ergebnisse mit `IMAI_dequeue` und druckt pro Label einen Score plus das beste Label ("Output: ...") auf die Konsole. Löscht dabei den Bildschirm per ANSI-Sequenz, ist also für ein echtes Terminal gedacht.
- `models/model.c` und `models/model.h`: das von DEEPCRAFT Studio generierte Modell als C-Code. Austauschbar gegen ein selbst trainiertes Modell aus dem Studio-Projekt "Motion Detection".
- `Makefile`: `TARGET=CY8CKIT-062S2-AI`, `TOOLCHAIN=GCC_ARM`, `CONFIG=Debug`, `COMPONENTS=ML_TFLM CMSIS_DSP ML_FLOAT32`, `DEFINES=ARM_MATH_DSP ARM_MATH_LOOPUNROLL TF_LITE_STATIC_MEMORY`. Das Modell läuft also über TensorFlow Lite Micro mit CMSIS-DSP-Beschleunigung in Float32.
- `deps/*.mtb`: die Bibliotheken, die `make getlibs` aus dem Infineon-Asset-Repo zieht: cmsis 5.x, ml-middleware 3.x, ml-tflite-micro 3.x, retarget-io 1.x, sensor-motion-bmi270 0.x. Dazu kommt das BSP für das Board, das ModusToolbox über die TARGET-Angabe selbst nachlädt.
- `images/gestures.gif` zeigt die beiden Gesten, `images/terminal.png` die erwartete Ausgabe.

**Wie der Build später abläuft (ModusToolbox-CLI)**
```
cd examples/deploy-motion
make getlibs        # lädt BSP und Bibliotheken nach libs/ und mtb_shared/
make build          # kompiliert mit GCC_ARM, Ergebnis build/<TARGET>/Debug/*.hex
make program        # flasht über OpenOCD + KitProg3 (alternativ unser tools/flash.sh program <hex>)
```
Das Makefile sucht ModusToolbox über `CY_TOOLS_PATHS`, standardmäßig aber nur unter `$HOME/ModusToolbox/tools_*` (und Windows/macOS-Pfaden), nicht unter `/opt/Tools`, wohin das Linux-Setup installiert. Nach der Installation also entweder `export CY_TOOLS_PATHS=/opt/Tools/ModusToolbox/tools_3.x` setzen oder einen Symlink `~/ModusToolbox` auf `/opt/Tools/ModusToolbox` anlegen. Solange nichts gefunden wird, bricht `make` sofort mit "Unable to find any of the available CY_TOOLS_PATHS" ab.

**Das mitgelieferte Modell** (`models/model.h`, generiert 2025-02-18 von Imagimob): 6 Eingänge (Beschleunigung x/y/z, Gyro x/y/z bei 50 Hz), 3 Ausgangsklassen `none`, `circle`, `shaking`. `model.c` ist 340 KB generierter C-Code.

## 27. Entscheidungspunkt: Marco will kein Infineon-Konto

Frage von Marco: Warum ist das Konto überhaupt nötig?

**Antwort:** Infineon stellt die ModusToolbox-Werkzeuge (Setup-Programm, Tools-Paket) nur hinter einem Login auf softwaretools.infineon.com bereit. Das Build-System der Beispiele braucht aus diesem Paket proprietäre Binaries, vor allem `mtbgetlibs` (löst die `deps/*.mtb`-Abhängigkeiten auf) und `mtbsearch` (findet beim Bauen automatisch Quelldateien und Komponenten). Ohne diese bricht `make` ab. Offen und ohne Login verfügbar sind dagegen: alle Bibliotheken (PDL, HAL, BSP, TFLite Micro, Sensortreiber), alle Codebeispiele, OpenOCD und fw-loader. Nur die Build-Orchestrierung ist zu. Ein Umgehen des Logins kommt nicht in Frage.

**Ohne Konto geprüfte Alternativen (Stand 2026-10-08):**

1. **Arduino Core for PSOC 6** (github.com/Infineon/arduino-core-psoc6): Version 1.1.0 vom 2026-05-21, aktiv gepflegt (letzter Push 2026-10-07). Das CY8CKIT-062S2-AI ist das Hauptboard des Cores. Installation per Boards-Manager-URL `https://github.com/Infineon/arduino-core-psoc6/releases/latest/download/package_psoc6_index.json` (HTTP 200 geprüft), funktioniert mit Arduino IDE 2.x und `arduino-cli`. Toolchain und Flash-Werkzeug kommen mit. Kein Login. Für das IMU-Beispiel müsste die Logik nach Arduino portiert werden (BMI270-Treiber plus TensorFlow Lite Micro als Arduino-Bibliothek, Modell aus `models/`).
2. **MicroPython für PSoC 6** (github.com/Infineon/micropython): Release v1.4.1 vom 2026-01-15 enthält `mpy-psoc6_CY8CKIT-062S2-AI.hex`, flashbar mit unserem `tools/flash.sh program`. Kein Login. Sensoren per I2C in Python, ML-Modelle laut Infineon-Hackster-Artikel über einen Konverter einbindbar. Schnellster Einstieg, aber nicht der Weg des Infineon-C-Beispiels.
3. **Zephyr RTOS**: Board `cy8ckit_062s2_ai` ist im Zephyr-Hauptzweig enthalten, Bauen mit `west`, zusätzlich `west blobs fetch hal_infineon` für CM0+-Images. Zephyr hat TFLite-Micro-Modul und BMI270-Treiber. Kein Login, aber die steilste Lernkurve.
4. **Eigenes Build-System** für das Infineon-Beispiel (CMake oder Makefile, alle Bibliotheken per git von GitHub): technisch möglich, weil alles offen ist, aber TFLite Micro, CMSIS-DSP, PDL, HAL und BSP von Hand zu verdrahten ist Tage an Arbeit. Nicht empfohlen.

Keine dieser Alternativen ist der 1:1-Weg der Infineon-Doku. Entscheidung steht aus.

---

# Teil 3: Wege ohne Infineon-Konto

## 28. Entscheidung und Struktur

Marcos Entscheidung: alle drei Optionen sind interessant. Anfangen mit Option 1 (Arduino-Core), Optionen 2 und 3 dokumentieren und in eigenen Verzeichnissen vorbereiten.

Neue Verzeichnisse im Projekt:
```
arduino/        Option 1: arduino-cli-Konfiguration, Core, Sketchbook (aktiv)
micropython/    Option 2: Firmware v1.4.1 und README mit Flash- und REPL-Anleitung (vorbereitet)
zephyr/         Option 3: README mit Fakten aus der Zephyr-Doku und ungetestetes setup.sh (vorbereitet)
examples/       Infineon-Originalbeispiel deploy-motion als Referenz für die Portierung
```

## 29. Option 1: Arduino-Core installiert

**arduino-cli** (Werkzeug von Arduino, GitHub-Release, kein Konto): Version 1.5.1 vom 2026-06-05, entpackt nach `tools/arduino-cli/`.

**Konfiguration bewusst projektlokal**, damit nichts in `~/.arduino15` landet und das Projekt in sich geschlossen bleibt: `arduino/arduino-cli.yaml` mit
- `board_manager.additional_urls`: `https://github.com/Infineon/arduino-core-psoc6/releases/latest/download/package_psoc6_index.json`
- `directories.data`: `arduino/.arduino15` (Cores, Toolchains)
- `directories.user`: `arduino/sketchbook` (eigene Sketches und Bibliotheken)
Jeder Aufruf braucht deshalb `--config-file arduino/arduino-cli.yaml`.

**Core-Installation** `core install infineon:psoc6`:
- `infineon:mtb-gcc-arm-none-eabi` 11.3.1.67 (203 MB), dieselbe Toolchain wie in ModusToolbox
- `infineon:openocd` 5.2.1.3248 (4,4 MB), Infineons OpenOCD für KitProg3
- `infineon:psoc6` 1.1.0 (122 MB), der Core selbst
Ergebnis `board listall`: `CY8CKIT-062S2-AI` mit FQBN `infineon:psoc6:cy8ckit_062s2_ai` (und CY8CPROTO-063-BLE).

Alle Downloads kamen von GitHub bzw. dem Arduino-Paketindex, ohne Anmeldung.

## 30. Arduino-Core untersucht, erster Sketch kompiliert

**Aufbau des Cores** (`arduino/.arduino15/packages/infineon/hardware/psoc6/1.1.0/`):
- Baut auf ModusToolbox-Bibliotheken auf, die als vorkompiliertes Archiv in der Board-Variante liegen (`variants/CY8CKIT-062S2-AI/mtb-libs.a`, BSP-Stand release-v4.3.2, dazu FreeRTOS- und lwIP-Konfiguration, mbedTLS). Infineon hat also genau den proprietär-gebauten Teil hier vorkompiliert mitgeliefert, deshalb geht es ohne ModusToolbox.
- Upload: `tools/upload.sh` ruft Infineons OpenOCD mit `psoc6_2m.cfg` über KitProg3 auf. Board muss an J1 hängen. Maximale Programmgröße 2.097.152 Bytes.
- Mitgelieferte Bibliotheken: Wire (I2C), SPI, PDM (Mikrofon, mit Beispiel `raw_data_output`), WiFi (Beispiele für Scan, Verbindung, Webserver, UDP, NTP). Kein Sensortreiber, kein ML.

**Pinbelegung der Variante** (`pins_arduino.h`):
- `LED_BUILTIN` = LED1 = Arduino-Pin 12 = P5_3; LED2 = Pin 13 = P5_4; `USER_BUTTON` = Pin 14 = P5_2
- I2C1: SDA Pin 9 = P0_3, SCL Pin 8 = P0_2 (Standard `Wire`); I2C2: SDA Pin 1 = P9_1, SCL Pin 0 = P9_0
- SPI auf P9_x, PDM-Mikrofon CLK P10_4 / DATA P10_5

**Erster Sketch** `arduino/sketchbook/01-blink-serial/`: LED blinkt, Konsole an J1 zählt hoch.
```
arduino-cli compile --fqbn infineon:psoc6:cy8ckit_062s2_ai --config-file arduino/arduino-cli.yaml arduino/sketchbook/01-blink-serial
Sketch uses 48068 bytes (2%) of program storage space. Maximum is 2097152 bytes.
Global variables use 7044 bytes of dynamic memory.
```
Kompiliert in 4,5 Sekunden, Ergebnis `build/01-blink-serial.ino.hex`. Noch nicht hochgeladen (Board hängt an J2).

**Bibliotheken für das IMU-Ziel** (Arduino-Bibliotheksindex, kein Konto):
- BMI270: "SparkFun BMI270 Arduino Library" 1.0.3 (auf Boschs offizieller Sensor-API), außerdem "Arduino_BMI270_BMM150", "7Semi BMI270", "BMI270_Sensor".
- TensorFlow Lite Micro: "Chirale_TensorFLowLite" 2.0.0 (generische TFLM-Portierung, laut Beschreibung für mbed- und ESP32-Boards, muss auf dem PSOC6-Core geprüft werden), dazu Wrapper "ArduTFLite" und "MicroTFLite".

## 31. Machbarkeit der Portierung geprüft

**Aufbau des Infineon-Modells** (`examples/deploy-motion/models/model.c`, 3.700 Zeilen):
- Zeilen 58 ff.: `_K4`, ein `uint32_t`-Array mit dem TensorFlow-Lite-Modell als FlatBuffer (103.144 Bytes). Dazu `_K3` als Arena (16.384 Bytes), `_K1`/`_K2` als Fensterpuffer (1.200 bzw. 17.800 Bytes).
- Vorverarbeitung in reinem C: Ringpuffer (`cbuffer_*`) und festes Fenster (`fixwin_*`) mit 50 Frames à 6 Float-Werten, Schrittweite 10 Frames. Pro Fenster gehen 300 Floats ins Modell, heraus kommen 3 Floats (none, circle, shaking).
- Die Inferenz läuft über Infineons ML-Zwischenschicht (`mtb_ml_model.h`). Benutzt werden genau: `mtb_ml_model_init`, `mtb_ml_model_run`, `mtb_ml_model_deinit`, die Typen `mtb_ml_model_t` (mit Feld `output`), `mtb_ml_model_bin_t`, `mtb_ml_model_buffer_t` und `CY_RSLT_SUCCESS`. Keine direkten TensorFlow-Aufrufe.
- Folgerung: `model.c`/`model.h` können unverändert in den Arduino-Sketch, dazu ein kleiner Adapter (`mtb_ml_model.h` plus eine `.cpp`), der diese drei Funktionen mit der TFLM-Bibliothek nachbildet.

**Testkompilate auf dem PSOC6-Core** (ohne Board, nur Toolchain):
- `Chirale_TensorFLowLite` 2.0.0, Beispiel hello_world: kompiliert, 315.652 Bytes Flash, 17.908 Bytes RAM, 2 Minuten Build (215 Quelldateien, inkl. CMSIS-NN-Kernel). Nur eine Warnung, dass die Bibliothek offiziell für mbed/ESP32 gedacht ist. Einzige architekturspezifische Stelle ist `system_setup.cpp` mit einem `#ifndef ARDUINO_ARCH_MBED`, das auf dem PSOC6 den generischen Zweig nimmt.
- `SparkFun BMI270 Arduino Library` 1.0.3, Beispiel BasicReadingsI2C: kompiliert, 78.628 Bytes. API `beginI2C(adresse, Wire)`, Standardadresse 0x68 wie im Infineon-Beispiel (`BMI2_I2C_PRIM_ADDR`).

**I2C-Verdrahtung des BMI270** laut BSP in der Arduino-Variante (`mtb-bsp/config/GeneratedSource/cycfg_pins.h`): SCL P0_2, SDA P0_3, Interrupts P1_5 und P0_4. Das ist exakt das Standard-`Wire` des Cores (I2C1, Arduino-Pins 8/9). Magnetometer-Interrupt auf P1_0, also hängen die Sensoren am selben Bus.

Damit ist der Weg frei: Sketch `02-imu-gesture` mit SparkFun-Treiber, originalem `model.c` und einem TFLM-Adapter.

## 32. Sketch 02-imu-gesture: Portierung geschrieben und kompiliert

Verzeichnis `arduino/sketchbook/02-imu-gesture/`:
| Datei | Herkunft | Zweck |
|---|---|---|
| `02-imu-gesture.ino` | neu | Sensor einlesen, skalieren, Modell füttern, Ausgabe, LEDs |
| `model.c`, `model.h` | unverändert aus `examples/deploy-motion/models/` | DEEPCRAFT-Modell mit Vorverarbeitung |
| `mtb_ml_model.h` | neu | Nachbildung der Infineon-ML-Schnittstelle (gleiche Signaturen) |
| `mtb_ml_shim.cpp` | neu | Implementierung auf TensorFlow Lite Micro |
| `model.tflite` | aus `model.c` extrahiert | das reine TFLite-Modell (103.144 Bytes), für Netron oder andere Runtimes |

**Modell-Operatoren** (aus dem FlatBuffer gelesen): RESHAPE, CONV_2D, MAX_POOL_2D, UNIDIRECTIONAL_SEQUENCE_LSTM, MEAN, FULLY_CONNECTED, SOFTMAX. 18 Operator-Aufrufe, 53 Tensoren, Eingang Tensor 0, Ausgang Tensor 52. Der Adapter registriert genau diese sieben Operatoren (`MicroMutableOpResolver<7>`), das hält den Code klein.

**Adapter-Logik:** `mtb_ml_model_init` lädt das Modell, versucht zuerst die 16-KB-Arena aus `model.c`; schlägt `AllocateTensors` fehl, wird eine interne 96-KB-Arena benutzt und das im Sketch gemeldet. `mtb_ml_model_run` kopiert 300 Floats in den Eingabetensor, ruft `Invoke` und setzt `model->output` auf den Ausgabetensor. Damit funktioniert `model.c` ohne jede Änderung.

**Sensor wie im Original:** `beginI2C(0x68, Wire)`, 400 kHz, Accel 50 Hz / 8 g / NORMAL_AVG4, Gyro 50 Hz / 500 dps / NORMAL_MODE. Skalierung: SparkFun liefert g und dps, das Modell erwartet m/s² und dps, also Accel mal 9,80665. Abtastung per `micros()` auf 20 ms getaktet (das Original wartet auf das Data-Ready-Bit; für die Demo gleichwertig).

**Drei Kompilierfehler, alle an der C/C++-Grenze:**
1. `cy_rslt_t` doppelt definiert: der Arduino-Core bringt Infineons `cy_result.h` mit. Lösung: diesen Header einbinden statt eigener Definition.
2. `restrict` in `model.h`: C99-Schlüsselwort, in C++ unbekannt. Lösung: im Sketch `#define restrict __restrict` vor dem Include. `model.h` bleibt unverändert.
3. `undefined reference to IMAI_init()`: `model.h` hat keine `extern "C"`-Klammer, der Linker suchte C++-dekorierte Namen. Lösung: Include im Sketch in `extern "C" { }` einklammern.

**Ergebnis:**
```
Sketch uses 273504 bytes (13%) of program storage space. Maximum is 2097152 bytes.
Global variables use 127996 bytes of dynamic memory.
```
Erster Vollbuild 1:38 min (TFLM-Kernel), danach 5 s. Noch nicht auf dem Board, weil es an J2 hängt.

## 33. Upload-Weg des Arduino-Cores bestätigt (Blink-Sketch)

Board wieder an J1 (KitProg3, jetzt USB-Gerät 099, `/dev/ttyACM0` mit plugdev-Rechten dank udev-Regel).

**Wie der Upload intern läuft** (`tools/upload.sh` des Cores, gelesen): ruft das mitgelieferte OpenOCD mit `interface/kitprog3.cfg`, `adapter serial <Seriennummer des Ports>`, `target/psoc6_2m.cfg`, `psoc6 allow_efuse_program off`, `psoc6 sflash_restrictions 1` und `program <hex> verify reset exit` auf. Die Seriennummer kommt aus den Eigenschaften des mit `-p` angegebenen Ports, deshalb muss `-p /dev/ttyACM0` mitgegeben werden, obwohl die Übertragung nicht über die serielle Schnittstelle läuft. Ohne `-v` landet die OpenOCD-Ausgabe in `build/<sketch>.openocd.log`.

```
arduino-cli upload --fqbn infineon:psoc6:cy8ckit_062s2_ai --config-file arduino/arduino-cli.yaml \
  -p /dev/ttyACM0 --input-dir arduino/sketchbook/01-blink-serial/build arduino/sketchbook/01-blink-serial
```
Log: `wrote 48640 bytes ... in 0.760272s`, `** Verified OK **`, `** Resetting Target **`. Gesamtdauer 2,8 s.
Konsole danach: `tick 11`, `tick 12`, ... im Sekundentakt. LED1 blinkt. Der Weg Core -> OpenOCD -> KitProg3 -> PSOC 6 funktioniert.

## 34. Gesten-Sketch auf dem Board

```
arduino-cli upload ... -p /dev/ttyACM0 --input-dir arduino/sketchbook/02-imu-gesture/build arduino/sketchbook/02-imu-gesture
wrote 274432 bytes from file .../02-imu-gesture.ino.hex in 3.045208s (88.007 KiB/s)
** Verified OK **
```
Upload 5,3 s. Danach auf der Konsole bei ruhig liegendem Board alle 200 ms eine Inferenz:
```
#39 none=1.00 circle=0.00 shaking=0.00 -> none
#40 none=1.00 circle=0.00 shaking=0.00 -> none
```
Modell, Adapter, Vorverarbeitung und Sensorpfad arbeiten also zusammen: 50 Frames Fenster, 10 Frames Schritt, Softmax-Ausgabe plausibel (Ruhe = "none" mit 1,00). Der Gestentest mit Bewegung folgt in Abschnitt 35.

## 35. Gestentest mit Bewegung: Ergebnis

Mitschnitt der Konsole über 73 s (365 Inferenzen à 200 ms), Marco hat geschüttelt und gekreist. Rohdaten: `arduino/sketchbook/02-imu-gesture/testlauf-2026-10-08.log`.

**Verteilung:** none 70 %, shaking 22 %, circle 8 %.

**Zeitstrahl** (ein Zeichen = eine Inferenz = 0,2 s; `.` none, `S` shaking, `C` circle):
```
 #18  ..................................................
 #68  ..................................................
#118  .......SSSSSSSSSS..................SSSSSSSSSSSS...
#168  ................................CCCSSSSCSSSSCSCSSS
#218  SSCSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSCSSCSSC
#268  SSCCCCCCCCCCCCCCCCCC..............................
#318  ..................................................
```

**Zusammenhängende Abschnitte (mindestens 0,6 s):**
| Inferenzen | Klasse | Dauer | mittlere Konfidenz |
|---|---|---|---|
| #125 bis #134 | shaking | 2,0 s | 0,90 |
| #153 bis #164 | shaking | 2,4 s | 0,95 |
| #200 bis #202 | circle | 0,6 s | 0,94 |
| #203 bis #260 | überwiegend shaking, mit circle-Einsprengseln | 11,6 s | 0,75 bis 0,87 |
| #270 bis #287 | circle | 3,6 s | 0,91 |

**Bewertung**
- Beide Gesten werden erkannt, Ruhe wird zuverlässig als "none" gemeldet (keine Fehlauslösung im Stillstand, über 50 s Ruhe ohne einen einzigen Ausreißer).
- Schütteln: zwei saubere Blöcke mit hoher Konfidenz, Ein- und Ausschwingen jeweils eine Inferenz.
- Kreisen: der lange Block am Ende ist eindeutig. Davor eine Phase von rund 12 s, in der das Modell überwiegend "shaking" sieht, mit kurzen "circle"-Treffern. Plausible Erklärung: schnelle, kleine oder unrunde Kreise ähneln für den Sensor dem Schütteln; das mitgelieferte Modell ist laut Infineon nur eine Referenz und nicht produktionsreif. Ein großer, gleichmäßiger Kreis mit flach gehaltenem Board wird sicher erkannt.
- 6 von 365 Inferenzen hatten keinen Score über 0,6, alle in den Übergängen zwischen Gesten (circle und shaking je etwa 0,5).

**Fazit:** Die Portierung des Infineon-Beispiels auf den Arduino-Core funktioniert vollständig: Sensor, Vorverarbeitung aus `model.c`, TFLite-Micro-Inferenz über den Adapter, LED-Anzeige. Ohne ModusToolbox, ohne Infineon-Konto, ohne Windows.

**Mögliche Verbesserungen (nicht umgesetzt):**
- Mehrheitsentscheid über die letzten 3 bis 5 Inferenzen, damit Einzelausreißer die LEDs nicht flackern lassen.
- Abtastung über das Data-Ready-Interrupt des BMI270 (P1_5) statt per Timer, näher am Original.
- Eigenes Modell mit eigenen Kreis-Daten trainieren (braucht DEEPCRAFT Studio unter Windows oder einen eigenen Trainingsweg, siehe BOARD-UEBERSICHT.md Abschnitt 6).

**Randnotiz:** Der Hintergrund-Logger für den Mitschnitt endete nach 73 s statt der geplanten 120 s (letzte Schreibung 12:35:57). Ursache nicht untersucht, der Mitschnitt deckt den Test trotzdem vollständig ab. Der OpenOCD-Reset aus Abschnitt 34 hat das Board doch neu gestartet, nur verzögert: Der Zähler begann im Mitschnitt wieder bei #18.

## 36. Startmeldung des Sketches und Rohdaten-Stream für einen Live-Viewer

**Startmeldung** (beim erneuten Upload mitgeschnitten, Zähler sprang von #669 auf #31, also echter Neustart):
```
DEEPCRAFT IMU Model Example (Arduino-Portierung)
BMI270 bereit: 50 Hz, 8 g, 500 dps
Modell geladen, Arena-Verbrauch: 7720 Bytes (Arena aus model.c)
```
Die 16-KB-Arena aus `model.c` reicht also für TensorFlow Lite Micro, die 96-KB-Reserve-Arena im Adapter bleibt ungenutzt. Sie kostet nur statisches RAM (davon ist 1 MB da) und bleibt als Sicherheitsnetz für größere Modelle drin.

**Marcos Wunsch:** sehen, was der Chip erkennt, nicht nur Textzeilen. Dafür zwei Änderungen:
1. Sketch gibt pro Sensor-Frame zusätzlich eine Zeile `D,ax,ay,az,gx,gy,gz` aus (m/s² und dps, zwei Nachkommastellen), schaltbar über `STREAM_RAW_FRAMES`. Das sind 50 Zeilen pro Sekunde, etwa 2 KB/s, bei 115200 Baud (ca. 11 KB/s) unkritisch. Die Inferenzzeilen bleiben unverändert. Neu kompiliert (273.556 Bytes) und hochgeladen, Ausgabe geprüft: rund 60 Zeilen pro Sekunde.
2. Ein lokaler Live-Viewer `tools/live_viewer.py` (nur Python-Standardbibliothek plus pyserial): liest die Konsole, hält die letzten Sekunden im Speicher und liefert sie per Server-Sent Events an eine eingebettete HTML-Seite auf `http://localhost:8765`. Keine Cloud, keine Installation. Gestaltung nach den Dataviz-Richtlinien (validierte Palette, eine Achse pro Diagramm, Legende plus Direktbeschriftung, Dunkelmodus).

## 37. Live-Viewer gebaut und geprüft

`tools/live_viewer.py` (491 Zeilen, Python-Standardbibliothek plus pyserial), Start:
```
tools/live_viewer.py            # Board an J1, dann http://localhost:8765 im Browser
tools/live_viewer.py --log aufzeichnung.csv   # zusätzlich alles als CSV mitschreiben
```

**Aufbau der Seite** (Form vor Farbe, nach den Dataviz-Richtlinien):
- Kopfzeile: Verbindungsstatus mit Portname, Frames/s, Inferenzen/s, Gesamtzahl; Knöpfe für Tabellenansicht und Hell/Dunkel.
- Hero-Wert: die aktuell erkannte Geste groß (52 px, Systemschrift), daneben ein Farbfeld der Klasse, darunter Konfidenz und Inferenznummer. Zwei LED-Chips zeigen, was die Board-LEDs gerade tun.
- Score-Balken: drei horizontale Balken (24 px, abgerundetes Datenende, Wert an der Spitze) in den Klassenfarben, Legende darüber.
- Zeitstreifen: erkannte Klasse der letzten 60 s, zusammenhängende Läufe als Blöcke mit 2-px-Lücke, Hover zeigt Zeitpunkt und alle Scores.
- Zwei Liniendiagramme mit je einer Achse: Beschleunigung (m/s²) und Drehrate (°/s) der letzten 10 s, 2-px-Linien, Endpunkte mit Oberflächenring, Endbeschriftung mit Achsname und Wert, Fadenkreuz mit Tooltip für alle drei Werte.
- Tabellenansicht der letzten 25 Inferenzen (zuschaltbar), Meldungsfenster für Textzeilen des Boards (z. B. Startbanner).

**Farben** (Referenzpalette, mit `validate_palette.js` geprüft, alle Paare, hell und dunkel):
- Klassen none / circle / shaking: Slots 1 bis 3 (Blau, Orange, Aqua). Alles PASS; Aqua liegt im Hellmodus bei 2,74:1 Kontrast, deshalb tragen Balken Wertlabels und es gibt die Tabellenansicht.
- Sensorachsen x / y / z: Slots 6 bis 8 (Grün, Violett, Rot), bewusst andere Farben als die Klassen, damit Blau nicht zweimal etwas bedeutet. Rot/Grün liegt für Protanopie im Hellmodus bei ΔE 7,2 (Band 6 bis 8, nur mit Zweitkodierung zulässig), deshalb steht an jedem Linienende der Achsname.
- Text immer in Texttoken, nie in Serienfarbe; Status-Farben nur für den Verbindungspunkt.

**Technik:** Ein Thread liest den Port und parst Zeilen (`D,...` Frames, `#n ... -> label` Inferenzen, Rest als Meldung). Ein Broadcaster bündelt alle 50 ms und schickt JSON per Server-Sent Events an alle offenen Browser-Tabs. Frames bekommen einen eigenen 20-ms-Takt, weil serielle Daten paketweise ankommen und sonst mehrere Frames denselben Zeitstempel hätten. Die Seite zeichnet mit Canvas, DPI-skaliert, und hält 10 s Frames und 60 s Inferenzen im Speicher.

**Render-Prüfung im Browser** (Playwright-Screenshot, 1200 px breit): Layout stimmt, Hero zeigte "circle" mit LED2, Zeitstreifen und Kurven plausibel. Drei Korrekturen danach: kollidierende Endbeschriftungen bei zusammenlaufenden Linien (jetzt auseinandergeschoben mit Leader-Linien zum Endpunkt), Kopfzeile brach wegen des langen by-id-Portnamens um (gekürzt, Knöpfe gruppiert), fehlendes Favicon (inline als SVG).

**Stolperer am Rande:** Ein `pkill -f live_viewer.py` hat zweimal meine eigene Shell beendet, weil das Muster auch im laufenden Befehl vorkam. Seitdem wird der alte Viewer über den Port beendet (`fuser -k 8765/tcp`).

Nachtrag: Beim zweiten Playwright-Durchlauf (Hell und Dunkel über `?theme=dark`) keine Konsolenfehler mehr, Endbeschriftungen getrennt (z. B. "z 8,4 / y 4,7 / x 1,2" mit Leader-Linien), Kopfzeile einzeilig. Dabei noch einen Routing-Fehler behoben: `/?theme=dark` lieferte 404, weil der Query-String beim Pfadvergleich nicht abgeschnitten wurde. Screenshots liegen unter `docs/viewer-light.png` und `docs/viewer-dark.png`. Der Viewer läuft seitdem im Hintergrund auf http://localhost:8765 (Prozess über `fuser -k 8765/tcp` beendbar).

---

# Teil 4: Klatschen erkennen

## 38. Ansatz und Sketch 03-clap-led

Marcos Wunsch: über Klatschen eine LED ein- und ausschalten. Entscheidung: kein ML-Modell, sondern klassische Signalverarbeitung. Ein Klatscher ist ein kurzer, lauter Impuls mit steiler Flanke; das lässt sich mit Lautstärkemessung und ein paar Regeln zuverlässig erkennen, ohne Trainingsdaten und ohne Studio.

**Mikrofon-Bibliothek des Arduino-Cores (PDM)**, aus `PDM.h`/`PDM.cpp` gelesen:
- `PDM.setBufferSize(n)` muss vor `begin` kommen (sonst gibt `begin` -1 zurück), `PDM.begin(kanaele, rate)`, `PDM.onReceive(cb)`, `PDM.available()`, `PDM.read(buf, n)` liefert 16-Bit-Samples in `int32_t`-Containern.
- Mono = linker Kanal, Dezimation 64, Standardverstärkung 20 (HAL-Einheiten), `setGain(g)` zum Nachstellen.
- DMA-Blöcke von 128 Samples, bei 16 kHz also alle 8 ms ein Callback.

**Erkennungslogik** (`arduino/sketchbook/03-clap-led/03-clap-led.ino`, 60 KB Flash):
1. RMS je 8-ms-Block (Gleichanteil abgezogen).
2. Grundrauschen als gleitender Mittelwert (Zeitkonstante ca. 0,4 s), der nur nachgeführt wird, solange kein Impuls läuft.
3. Impulsbeginn, wenn das Verhältnis RMS/Grundrauschen >= 6 und absolut >= 300 ist, der Block davor aber unter 2 lag (steile Flanke) und seit dem letzten Klatscher >= 400 ms vergangen sind.
4. Impulsende, wenn das Verhältnis wieder unter 2 fällt. Dauer <= 150 ms: Klatscher, LED1 wird umgeschaltet, LED2 blitzt 80 ms. Länger laut: verworfen (Sprechen, Musik, Dauerlärm).
5. Ausgabe: `KLATSCH #n  Dauer d ms  Pegel xR  LED1 an|aus`, dazu 10-mal pro Sekunde `L,<rms>,<floor>` zum Einstellen der Schwellen (abschaltbar über `SERIAL_LEVELS`).

Kompiliert und hochgeladen (60.928 Bytes, verifiziert).

## 39. Zwei Leser auf einer seriellen Schnittstelle

Beim Mitschnitt für den Klatschtest kam nichts an: Marco hatte inzwischen seinen eigenen Viewer gestartet (`tools/live_viewer.py` in seinem Terminal), und der hält `/dev/ttyACM0` offen und liest alle Daten weg. Unter Linux können mehrere Prozesse einen tty öffnen, aber jedes Byte bekommt nur einer. Mein Logger ging deshalb leer aus. Nachgewiesen über `/proc/*/fd`.

Konsequenz: Der Viewer wird um einen Klatsch-Modus erweitert, statt den Nutzer zum Beenden zu bitten. Merke für später: Nur ein Programm pro Port; Uploads sind davon nicht betroffen (laufen über den Debugger).

## 40. Viewer-Erweiterung: Klatsch-Modus

Der Viewer erkennt am Datenstrom, welcher Sketch läuft: Inferenzzeilen `#n ... -> label` schalten in den Gesten-Modus, Pegelzeilen `L,rms,floor` und `KLATSCH ...` in den Klatsch-Modus. Karten werden je Modus ein- und ausgeblendet.

Klatsch-Modus:
- Hero: Zustand von LED1 ("an"/"aus") mit Farbfeld, darunter Nummer, Dauer und Pegelverhältnis des letzten Klatschers. LED1-Chip.
- Lautstärke der letzten 10 s als Liniendiagramm mit logarithmischer y-Achse (100, 1.000, 10.000 auf der 16-Bit-Skala): Pegel in Slot 1 (Blau) als Betonung, Grundrauschen in Grau (Emphasis-Form: eine Serie betont, Kontext grau), Klatscher als senkrechte Markierungen in Slot 2 (Orange). Fadenkreuz-Tooltip mit Pegel, Grundrauschen und Verhältnis.
- Streifen der letzten 60 s: Klatscher als Striche, dazwischen der LED1-Zustand als Band (Blau, 35 % Deckung).
- `--log` schreibt auch `L`- und `K`-Zeilen in die CSV.

## 41. Erster Klatschtest: Schnipsen wird mit erkannt

Marco hat den Viewer mit `--log clap-test.csv` neu gestartet, geklatscht und geschnipst. Rückmeldung: "Schnipsen mit dem Finger wurde teilweise auch erkannt." Mitschnitt: 257 s, 23 Ereignisse, Grundrauschen im Raum sehr niedrig (RMS etwa 50). Kopie unter `arduino/sketchbook/03-clap-led/testlauf-1-2026-10-08.csv`.

**Auswertung der Ereignisdauern** (Blöcke sind 8 ms, Dauern daher Vielfache davon):
| Zeitbereich | Dauern | Einordnung |
|---|---|---|
| 226 bis 238 s (8 Ereignisse) | 80 bis 120 ms | Klatschen: lang, weil das Klatschen breitbandig ist und im Raum nachhallt; Pegelspitzen im 100-ms-Raster bis 885 |
| 245 bis 253 s (7 Ereignisse) | 8 bis 16 ms | Schnipsen: ein bis zwei Blöcke, sehr kurz; Pegelspitzen kaum sichtbar |
| 14 bis 26 s (8 Ereignisse) | 8 bis 64 ms | gemischt, vermutlich erste Versuche mit beidem |

Die Dauer trennt beide Geräusche sauber: Schnipsen 8 bis 16 ms, Klatschen 48 bis 120 ms. Nichts dazwischen außer zwei Ereignissen mit 23/24 ms aus der ersten Gruppe.

**Zwei Schwächen des ersten Sketches**
- Die Zeile pro Klatscher nannte das Pegelverhältnis des letzten Blocks vor dem Abklingen, nicht die Spitze. Dadurch sahen Klatscher wie "x2,0" aus.
- Die Pegelzeilen (10 Hz) zeigten den Momentanwert eines einzelnen 8-ms-Blocks; die kurzen Spitzen fielen meist zwischen zwei Ausgaben und fehlten im Diagramm.

**Änderungen (Sketch neu kompiliert und hochgeladen)**
1. Mindestdauer `MIN_CLAP_MS 20`: ein Impuls muss mindestens drei Blöcke (24 ms) laut bleiben. Schnipsen (8 bis 16 ms) fällt damit heraus, Klatschen (ab 48 ms) bleibt.
2. Spitzenwert und Spitzenverhältnis werden während des Impulses mitgeführt und ausgegeben: `KLATSCH #n  Dauer d ms  Peak p  xR  LED1 an|aus`.
3. Verworfene Impulse werden strukturiert gemeldet: `VERWORFEN  Dauer d ms  Peak p  xR  Grund: zu kurz (Schnipsen, Klicken) | zu lang | Dauerlaerm`. So sieht man beim Einstellen, was knapp durchfiel.
4. Die Pegelzeile meldet das Maximum des 100-ms-Fensters statt eines Einzelblocks, damit Spitzen im Diagramm erscheinen.

**Viewer:** neues Zeilenformat, verworfene Impulse als graue Markierungen in Pegeldiagramm und Streifen, Spitze im Hero-Untertitel, `R`-Zeilen in der CSV.

**Grenze des Ansatzes:** Ein sehr lautes Schnipsen direkt am Mikrofon oder ein leises Klatschen aus der Ferne können sich weiterhin ähneln. Die nächste Stufe wäre ein Frequenzmerkmal (Schnipsen hat mehr hohe Anteile), etwa die Nulldurchgangsrate pro Block. Erst nötig, wenn die Dauer allein nicht reicht.

Kontrolle nach dem Upload (3 s Konsole, Port war frei): Pegelzeilen zeigen RMS 4 bis 30 bei Grundrauschen 50 (Untergrenze im Sketch), ein Klatscher wurde als `KLATSCH #1  Dauer 32 ms  Peak 406  x8.1  LED1 an` gemeldet. Zweiter Test mit Schnipsen steht aus (Abschnitt 42).

## 42. Zweiter Klatschtest: Dauer und Spitze trennen Klatschen von Schnipsen

Marco: "ein bisschen besser, aber #22 bis #24 waren Schnipser und #25 Klatschen." Mitschnitt `clap-test-2.csv` (51 s, 11 erkannt, 5 verworfen), Kopie unter `arduino/sketchbook/03-clap-led/testlauf-2-2026-10-08.csv`.

| Nr. | Dauer | Spitze (RMS) | Verhältnis | laut Marco |
|---|---|---|---|---|
| #17 | 104 ms | 8.703 | x174 | Klatschen |
| #18 | 64 ms | 4.118 | x82 | Klatschen |
| #25 | 104 ms | 6.755 | x135 | Klatschen |
| #22 | 24 ms | 3.373 | x67 | Schnipsen |
| #23 | 24 ms | 1.711 | x34 | Schnipsen |
| #24 | 24 ms | 1.232 | x24 | Schnipsen |
| #15, #16, #19, #20, #21 | je 24 ms | 788 bis 1.272 | x16 bis x25 | nicht kommentiert, gleiches Profil wie Schnipsen |
| verworfen (5) | 8 bis 16 ms | 333 bis 902 | x7 bis x18 | zu kurz, korrekt |

**Erkenntnis:** Zwei Merkmale trennen sauber, und zwar mit großem Abstand:
- Dauer: Schnipsen 8 bis 24 ms, Klatschen 64 bis 104 ms. Die bisherige Mindestdauer von 24 ms lag genau auf der Schnipser-Dauer (drei Blöcke) und ließ sie durch.
- Spitze: Schnipsen 330 bis 3.400, Klatschen 4.100 bis 8.700.
Das Pegelverhältnis zum Grundrauschen ist dagegen kein gutes Merkmal, weil das Grundrauschen im Raum bei 50 liegt (Untergrenze) und selbst ein Schnipser so auf x67 kommt.

**Änderungen (Sketch neu kompiliert und hochgeladen):**
1. `MIN_CLAP_MS 40`: mindestens fünf Blöcke laut. Sicherheitsabstand nach beiden Seiten (Schnipsen max. 24, Klatschen min. 64).
2. `MIN_PEAK_RMS 1500`: zweite Hürde, Grund "zu leise". Liegt zwischen den gemessenen Schnipsern und Klatschern, nimmt aber leise Klatscher aus der Ferne in Kauf.
3. Nulldurchgangsrate (ZCR) des lautesten Blocks wird in jeder Ereigniszeile mitgeschrieben, aber noch nicht zur Entscheidung benutzt. Schnipsen hat mehr Hochfrequenzanteile und sollte höhere Werte zeigen. Falls Dauer und Spitze irgendwann nicht reichen, liegt damit das nächste Merkmal schon in den Mitschnitten vor.
4. Viewer: liest das ZCR-Suffix mit, schreibt es in die CSV, Grund "zu leise" erscheint wie die anderen.

Dritter Test steht aus.

## 43. Abnahme Klatsch-Schalter

Marco: "ok das passt so erstmal." Stand der Erkennung: Mindestdauer 40 ms, Mindestspitze 1.500, Totzeit 400 ms, Dauerlärm wird verworfen. Zwei Messreihen liegen als CSV im Sketch-Ordner, die Schwellen stehen mit Begründung im Sketch-Kopf.

Offen gelassen, bei Bedarf:
- Nulldurchgangsrate als drittes Merkmal einsetzen (wird bereits mitgeschrieben).
- Doppelklatschen als Muster (zwei Klatscher innerhalb von 300 bis 800 ms), um zufällige Einzelgeräusche auszuschließen.
- Schwellen automatisch an die Raumlautstärke koppeln, falls das Board in lauterer Umgebung läuft.

## 44. Dritter Klatschtest: bei Umgebungslärm werden Klatscher verworfen

Marco: "im letzten Test war das Umgebungsrauschen relativ hoch und deswegen hat der das Klatschen nicht gut erkannt." Mitschnitt `clap-test-3.csv`, 718 s, 38 erkannt, 69 verworfen; Kopie unter `arduino/sketchbook/03-clap-led/testlauf-3-2026-10-08.csv`.

**Befund**
- Grundrauschen: Median 50 (Untergrenze), zeitweise bis 351. Der Pegelverlauf zeigt ab Sekunde 480 lange Abschnitte mit Spitzen über dem Achtfachen des Grundrauschens.
- Verworfene Klatscher: 10 Ereignisse mit "zu lang" (151 bis 240 ms) und 2 mit "Dauerlärm" (255 ms), alle mit Spitzen von 2.500 bis 15.160, also eindeutig Klatscher. Dazu 6 "zu leise" mit 48 bis 240 ms und Spitzen 540 bis 1.260 (unklar, vermutlich entfernte Klatscher oder Geräusche).
- Die 38 erkannten Klatscher: Dauer 40 bis 143 ms, Spitzen 1.560 bis 14.000. Viele davon dicht an der 150-ms-Grenze.
- "zu kurz" (8 bis 32 ms): 50 Ereignisse, davon einige mit Spitzen bis 6.100 (Schnipsen nah am Mikrofon). Die Dauergrenze hält diese sauber fern.

**Ursache:** Das Impulsende war definiert als "Pegel fällt unter das Doppelte des Grundrauschens". Bei Nachhall oder Hintergrundgeräuschen bleibt der Pegel länger über dieser niedrigen Schwelle, die gemessene Dauer wächst über 150 ms, und der Klatscher gilt als "zu lang". Die Dauer hing damit vom Raum ab, nicht nur vom Geräusch.

**Umbau des Detektors (Sketch neu kompiliert und hochgeladen):**
1. Impulsende jetzt relativ zur eigenen Spitze: Ende, wenn der Pegel unter 10 % der Spitze (-20 dB) fällt, oder früher, falls er unter das Doppelte des Grundrauschens fällt. Die Dauer beschreibt damit die Form des Geräuschs, nicht den Raum.
2. Flankenkriterium relativ zum Vorblock: Der Block davor muss mindestens dreimal leiser gewesen sein (vorher: unter dem Doppelten des Grundrauschens, was bei Lärm selten zutrifft).
3. Abbruch bei Dauerlärm erst nach 400 ms über 10 % der Spitze.
4. `MIN_CLAP_MS` vorläufig auf 24 ms gesenkt, weil die neue Dauer kürzer ausfällt als die alte. Die alte Abklingzeit bis zum Doppelten des Grundrauschens wird als "Abkl." in jeder Ereigniszeile mitgeschrieben (Viewer und CSV), damit die Schwelle nach dem nächsten Messlauf aus beiden Werten abgeleitet werden kann.
5. Spitzen-Mindestwert 1.500 und Obergrenze 150 ms unverändert.

Vierter Test steht aus; danach Feinjustierung von `MIN_CLAP_MS` anhand der Spalten Dauer (neu) und Abkl. (alt) für Schnipsen und Klatschen.

## 45. Entscheidungspunkt: Regeln oder trainiertes Modell für Klatschen?

Marco: "ich frage mich gerade, ob es dazu nicht schon ein trainiertes Modell gibt bzw. kann man nicht einfach ein kleines Modell für die Erkennung der Klatscher trainieren. Irgendwie dachte ich, dass der Chip / das Kit genau dafür gedacht ist."

**Fertige Modelle geprüft (Imagimob Ready Models und Starter Models, Stand 2026-10-08):** Audio-Modelle gibt es für Babyschreien, Husten, Schnarchen, Sirene, Fabrikalarm, Schallrichtung; in den Starter-Projekten außerdem Kettensäge, Schuss, Schlüsselwörter, "Home Sounds". Kein Klatsch-Modell. Die Ready Models brauchen zudem DEEPCRAFT Studio (Windows, Imagimob-Konto) und eine Lizenz.

**Einordnung:** Der regelbasierte Detektor (Abschnitte 38 bis 44) war der schnelle Weg und hat seine Grenze gezeigt: Die Schwellen hängen vom Raum ab. Ein kleines Klassifikationsmodell auf Spektrogrammen ist genau der Anwendungsfall, für den das Kit gedacht ist, und lässt sich mit den vorhandenen Bausteinen ohne Studio bauen: TensorFlow Lite Micro läuft bereits auf dem Board (Abschnitt 31), das Mikrofon läuft (Abschnitt 38), Trainingsdaten können über die serielle Schnittstelle aufgenommen werden.

**Vorgeschlagener Weg (Linux, ohne Konto):**
1. Aufnahme-Sketch: 16 kHz Mono-PCM über die KitProg3-Schnittstelle streamen (Infineons PDM-Beispiel macht das mit 1 MBaud). Python-Werkzeug nimmt auf Ansage Klatschen, Schnipsen und Hintergrund auf (je etwa 50 bis 100 Beispiele, plus mehrere Minuten Umgebungsgeräusche, bewusst auch bei Lärm).
2. Merkmale: FFT mit 256 Punkten alle 16 ms, 32 logarithmische Energiebänder, 0,5 s Fenster, also ein 32x32-Bild pro Entscheidung. In Python und in C identisch implementiert.
3. Training: kleines CNN (unter 30.000 Parameter) mit TensorFlow in einer Python-venv, Klassen Klatschen / Schnipsen / Sonstiges, Augmentierung mit Rauschen und Pegelvariation, Export als int8-TFLite (voraussichtlich unter 50 KB).
4. Deployment: Modell als C-Array in den Sketch, Inferenz mit TensorFlow Lite Micro wie beim Gestenmodell, Entscheidung mit kurzer Glättung, LED1 umschalten.

Aufwand grob: Aufnahme etwa 20 Minuten mit Marco, Training Minuten, Implementierung und Abgleich der Merkmalsberechnung der größte Teil. Der regelbasierte Detektor bleibt als Vergleich und Rückfallebene erhalten. Entscheidung steht aus.

---

# Teil 5: Klatsch-Modell trainieren

## 46. Aufnahme-Werkzeug

Marcos Entscheidung: mit dem Aufnahme-Werkzeug anfangen, das Menü-Dashboard danach.

**Sketch `04-audio-stream`** (58 KB): streamt das Mikrofon als Rohaudio, 16 kHz, Mono, 16 Bit, über die KitProg3-Schnittstelle bei 1.000.000 Baud (so auch Infineons PDM-Beispiel). Rahmen je 256 Samples (16 ms): Magic `A5 5A`, 16-Bit-Folgenummer, 512 Byte PCM, 16-Bit-Prüfsumme (Summe der Samples). Damit kann der Empfänger nach Übertragungsfehlern neu aufsetzen und verlorene Rahmen zählen. Steuerzeichen vom PC: `1`/`0` schalten LED1 (Aufnahme läuft), `?` gibt eine Statuszeile aus. LED2 leuchtet dauerhaft, solange gestreamt wird. Datenrate 32 KB/s plus Rahmen, bei 1 MBaud etwa ein Drittel der Kapazität.

**Werkzeug `tools/record_audio.py`** (pyserial und Standardbibliothek):
- `monitor`: Pegel, Spitze und Rahmenstatistik pro Sekunde, als Funktionstest.
- `session --per-class 40 --clip 1.5`: geführte Aufnahme. Pro Durchgang Klatschen, Schnipsen und jedes zweite Mal ein Störgeräusch aus einer Liste (Tischklopfen, Schlüssel, Husten, Wort, Buch zuklappen, Trommeln, Stift fallen lassen, Rufen, Stuhl, Tastatur, Zungenschnalzen, leise Hand). Ablauf je Clip: Ansage, 2 s Countdown, "JETZT" mit LED1 an, 1,5 s Aufnahme, Ablage, Hinweis bei sehr leisem Clip. Abbruch mit Strg+C ohne Datenverlust.
- `background --seconds 60`: Hintergrund am Stück, bewusst auch bei Lärm aufzunehmen.
- Ablage `data/audio/<klasse>/<klasse>_<datum>_<nr>.wav` plus `data/audio/index.csv` (Datei, Klasse, Ansage, Dauer, Spitze, RMS, verlorene Rahmen, Zeit).
- Prüft vor dem Start über `/proc/*/fd`, ob ein anderes Programm den Port hält (Lehre aus Abschnitt 39), und nennt es.

Die 1,5-s-Clips sind länger als das spätere Modellfenster (0,5 s), damit die Reaktionszeit nach "JETZT" keine Rolle spielt. Das Trainingsskript sucht die Energiespitze und schneidet das Fenster darum aus.

**Funktionstest:** `monitor` über 4 s bei 1 MBaud: 251 Rahmen, 0 verloren, 0 defekt, RMS 29 bis 52 (ruhiger Raum). `background --seconds 3` in ein Testverzeichnis: WAV 16 kHz Mono 16 Bit, exakt 3,00 s, Spitze 149, Indexzeile korrekt. Werkzeug ist einsatzbereit; die eigentliche Aufnahme macht Marco, weil sie interaktiv ist (Abschnitt 47).

## 47. Aufnahmen und Trainingsumgebung

**Marcos Aufnahmen** (`data/audio/`, 102 Dateien, null verlorene Rahmen):
| Klasse | Dateien | Dauer | Spitze min / median / max | Bemerkung |
|---|---|---|---|---|
| clap | 40 | 60 s | 763 / 15.156 / 32.768 | drei Clips am Anschlag (32.768), ein sehr leiser |
| snap | 40 | 60 s | 193 / 1.390 / 4.834 | Schnipsen ist leise, 22 Clips unter 1.500; das ist die Realität, nicht ein Fehler |
| other | 20 | 30 s | 37 / 3.018 / 17.733 | 12 verschiedene Ansagen, Buch und Klopfen bis 17.700 |
| background | 2 | 120 s | 833 / 1.054 / 1.276 | RMS 46 (Ruhe) und 89 (Lärm) |

**Umgebung:** `ml/.venv` mit numpy und TensorFlow (CPU), 14 Kerne, 30 GB RAM. Kein systemweites pip nötig, `python3-venv` war vorhanden.

**Pipeline in `ml/clap/`:**
- `features.py`: Rahmen 256 Samples (16 ms), Hann, FFT 256, 32 mel-verteilte Bänder zwischen 125 und 7.500 Hz als Bin-Bereiche, log10 der Bandleistung, Fenster 32 Rahmen (512 ms) mit dem Einsatz bei Rahmen 8. Pegelnormierung: minus Maximum des Fensters, 50 dB Dynamik auf [0, 1]. Gibt die Tabellen für den C-Code aus. Die Parameter sind bewusst so gewählt, dass die C-Fassung ohne Bibliothek auskommt (feste 256-Punkt-FFT, Bin-Summen, keine Mel-Filterbank mit Gewichten).
- `dataset.py`: Fenster am lautesten Rahmen ausrichten (so wie der Sketch nach einem Pegelanstieg), Aufteilung 80/20 nach Dateien, Augmentierung nur im Training: Verstärkung 0,3 bis 1,0, Mischen mit Hintergrund bei 0 bis 25 dB SNR (70 % der Fälle), Verschiebung um bis zu 3 Rahmen. Hintergrund liefert Fenster an den 8 lautesten Stellen und an Zufallspositionen, alle als "other".
- `train.py`: CNN mit drei Conv-Blöcken (8/16/32 Filter, 3x3, MaxPool), Dense 24, Softmax über other / clap / snap. Klassengewichte, EarlyStopping. Export als vollständig int8-quantisiertes TFLite (Eingabe und Ausgabe int8) mit Repräsentativdaten, Prüfung der Genauigkeit nach Quantisierung, Konfusionsmatrizen, C-Array `model_data.h` und `features_config.h` mit Bandtabelle und Quantisierungsparametern, `report.txt`.

## 48. Training des Klatsch-Modells: Ablauf, Ergebnisse, Stolpersteine

**Datensatz nach Aufbereitung** (`ml/clap/dataset.py`, Aufteilung nach Dateien 80/20, Seed 1):
| | Dateien | Fenster | other / clap / snap |
|---|---|---|---|
| Training | 82 | 656 | 208 / 224 / 224 |
| Validierung | 20 | 20 | 4 / 8 / 8 |

Jedes Trainingsereignis liefert ein Originalfenster plus sechs augmentierte (Verstärkung 0,3 bis 1,0; in 70 % der Fälle Mischung mit einem zufälligen Hintergrundausschnitt bei 0 bis 25 dB Rauschabstand; Verschiebung des Einsatzes um bis zu 3 Rahmen). Die beiden Hintergrundaufnahmen liefern je 8 Fenster an den lautesten Stellen und 40 an Zufallspositionen, alle als "other". Validierungsfenster sind unverändert (keine Augmentierung).

**Merkmale** (`ml/clap/features.py`, identisch in C umgesetzt): 16-ms-Rahmen, periodisches Hann-Fenster, FFT 256, Leistungsspektrum, 32 Bänder als zusammenhängende Bin-Bereiche mit mel-verteilten Grenzen von 125 bis 7.500 Hz (Tabelle mit 33 Bin-Indizes), log10 der Bandleistung plus 1e-7. Fenster 32 Rahmen (512 ms), Einsatz des Ereignisses bei Rahmen 8 (128 ms Vorlauf, 384 ms Nachlauf). Pegelnormierung: minus Maximum des Fensters, Dynamik 50 dB auf [0, 1]. Dadurch ist die Eingabe unabhängig von Lautstärke und Abstand, und der Chip braucht keine Mel-Filterbank, nur Bin-Summen.

**Modell** (`ml/clap/train.py`): Eingabe 32x32x1, drei Blöcke Conv2D 3x3 (8, 16, 32 Filter, ReLU) mit MaxPool 2x2, Reshape auf 512, Dropout 0,3, Dense 24 ReLU, Dense 3 Softmax. 18.275 Parameter. Adam 1e-3, Klassengewichte, Batch 32, EarlyStopping (Geduld 8) und Lernratenreduktion.

**Ergebnis** (Training 15 s auf CPU, 14 Kerne):
```
40 Epochen, beste Epoche 39, val_loss 0,097, val_acc 1,000
Konfusionsmatrix Validierung, float und int8 identisch:
               other    clap    snap
  ist other        4       0       0
  ist clap         0       8       0
  ist snap         0       0       8
TFLite int8: 25.312 Bytes, Eingabe int8 (scale 1/255, zero -128), Ausgabe int8 (scale 1/256, zero -128)
Operatoren: CONV_2D, MAX_POOL_2D, RESHAPE, FULLY_CONNECTED, SOFTMAX
```
Einordnung: 20 von 20 richtig ist ein gutes Zeichen, aber die Validierung ist klein (4 Störgeräusche, 8 Klatscher, 8 Schnipser, alle aus demselben Raum und derselben Sitzung). Die eigentliche Prüfung ist der Betrieb auf dem Board in der lauten Umgebung (Abschnitt 49). Vollständiger Bericht: `ml/clap/out/report-2026-10-08.txt`.

**Drei Stolpersteine beim Export, alle in Keras 3 / TensorFlow 2.21:**
1. `Flatten` erzeugt im TFLite-Graphen die Operatoren SHAPE, STRIDED_SLICE und PACK, weil die Batch-Dimension dynamisch ist. Auf dem Chip wären das drei zusätzliche Kernel ohne Nutzen. Ein `Reshape` mit fester Zielform änderte daran nichts.
2. Export über `tf.function(...).get_concrete_function` mit fester Batch-Größe: Die Keras-Gewichte bleiben als Ressourcen-Variablen im Graphen (READ_VARIABLE), die Kalibrierung für int8 bricht ab mit "variable != nullptr was not true".
3. Lösung: `model.export(pfad, input_signature=[TensorSpec([1, 32, 32, 1])])` als SavedModel mit fester Signatur, dann `TFLiteConverter.from_saved_model`. Ergebnis: nur die fünf erwarteten Operatoren, Gewichte eingefroren, 1 KB kleiner.

`train.py` schreibt `model_data.h` (C-Array, 16-Byte-ausgerichtet) und `features_config.h` (Bandtabelle, Fensterparameter, Quantisierungsparameter, Klassennamen). Beides wird unverändert in den Sketch kopiert.

## 49. Deployment: Sketch 05-clap-model

**Aufbau** (`arduino/sketchbook/05-clap-model/05-clap-model.ino`, 180 Zeilen plus generierte Header):
- Eigene 256-Punkt-FFT in C (Radix-2, Bit-Umkehr, Twiddles beim Start berechnet), Hann-Fenster, Bandsummen nach `FEAT_BAND_EDGES`, log10f. Pro 16-ms-Rahmen ein Satz von 32 Log-Bändern im Ringpuffer der letzten 32 Rahmen.
- Auslöser wie im regelbasierten Sketch: RMS springt über das Sechsfache des Grundrauschens, Vorblock mindestens dreimal leiser, 400 ms Totzeit. Dann 24 weitere Rahmen abwarten (384 ms), Fenster aus 8 Rahmen vor und 24 ab dem Einsatz bilden, pegelnormieren, int8-quantisieren, Modell ausführen.
- TensorFlow Lite Micro über die Arduino-Bibliothek Chirale_TensorFLowLite mit `MicroMutableOpResolver`, 40 KB Arena reserviert.
- Entscheidung: Klasse mit höchster Wahrscheinlichkeit; bei "clap" mit mindestens 0,60 schaltet LED1 um, LED2 blitzt. Ausgabe `MODELL other=.. clap=.. snap=.. -> klasse  Inferenz x ms`, bei Klatschen zusätzlich `KLATSCH #n ...` im Format des alten Sketches, dazu die Pegelzeilen. Der Viewer zeigt die Wahrscheinlichkeiten in einer eigenen Karte.

**Zahlen:** Flash 163.532 Bytes (7 %), statisches RAM 61.808 Bytes (davon 40 KB Arena, tatsächlich genutzt 12.484 Bytes, Eingabetensor 1.024 Bytes). Upload verifiziert, Startmeldungen:
```
Klatsch-Modell: TensorFlow Lite Micro auf dem PSOC 6
Modell geladen, Arena 12484 Bytes, Eingabe 1024 Bytes, Klassen other
Mikrofon laeuft: 16 kHz mono, 16-ms-Rahmen, 32 Baender
```
Latenz von der Geste bis zur LED: etwa 0,4 s (Nachlauf des Fensters) plus Inferenzzeit (wird pro Ereignis gemeldet, Messung steht aus).

**Beobachtung beim Einfangen der Startmeldungen:** Der erste Mitschnitt enthielt nur Pegelzeilen, weil der Logger erst nach dem Neustart des Boards las. Ein OpenOCD-Reset mit laufendem Logger lieferte die Meldungen. Das Grundrauschen im Raum lag währenddessen bei 50 bis 125, also deutlich höher als bei den ersten Tests.

Praxistest mit Klatschen und Schnipsen bei Umgebungslärm steht aus (Abschnitt 50).

## 50. Erster Praxistest des Modells

Marco: "funktioniert noch nicht perfekt, aber eigentlich kannst du die Konfidenz auch ein bisschen runtersetzen." Mitschnitt `modell-test.csv` (174 s), Kopie unter `arduino/sketchbook/05-clap-model/testlauf-1-2026-10-08.csv`. Grundrauschen median 51, zeitweise bis 186.

**41 Modell-Entscheidungen, Inferenzzeit konstant 53,0 ms:**
| Entscheidung | Anzahl | Wahrscheinlichkeiten |
|---|---|---|
| snap | 6 | 0,86 bis 0,99, clap dabei höchstens 0,02 |
| other, eindeutig | 11 | other 0,86 bis 1,00, clap höchstens 0,13 |
| other, aber clap im Spiel | 4 | clap 0,31 / 0,38 / 0,43 / 0,33 |
| clap, geschaltet (>= 0,60) | 14 | 0,63 bis 1,00 |
| clap, nicht geschaltet (< 0,60) | 3 | 0,52 / 0,55 / 0,59 |
| other mit snap-Anteil | 3 | snap 0,26 bis 0,42, vermutlich leise Schnipser oder Schnalzen |

**Befund:** Schnipsen und eindeutige Störgeräusche werden sauber getrennt; die einzige Unschärfe liegt zwischen Klatschen und "other", vermutlich bei leisen, entfernten oder von Lärm überlagerten Klatschern. Die Schwelle 0,60 mit Argmax-Bedingung war dafür zu streng: Sieben vermutliche Klatscher gingen verloren, obwohl kein einziges Nicht-Klatsch-Ereignis über clap 0,13 kam.

**Änderung (kompiliert, hochgeladen):** `CLAP_THRESHOLD 0.35`, Entscheidung allein über die clap-Wahrscheinlichkeit, auch wenn "other" knapp vorne liegt. Damit wären in diesem Mitschnitt 19 statt 14 Klatscher geschaltet worden, bei unverändert null Fehlauslösungen durch Schnipsen oder Störgeräusche. Die zwei Fälle mit 0,31 und 0,33 bleiben bewusst draußen; dort ist der Abstand zu möglichen Fehlauslösungen zu klein.

**Nachhaltige Verbesserung:** Die Grauzone entsteht in genau den Situationen, die in den Trainingsdaten unterrepräsentiert sind (Klatschen bei Lärm, aus Entfernung, leise). Vorschlag: eine zweite Aufnahmesitzung mit Sketch 04 bei eingeschaltetem Lärm und größerem Abstand (`session --per-class 20`), dann in 15 Sekunden nachtrainieren. Das Modell lernt dann die schwierigen Fälle statt sie über die Schwelle zu erraten.

## 51. Git-Repository angelegt

Marco: "sehr gut, das funktioniert besser. Setz das mal als Git-Repo auf und mach die entsprechenden Commits inklusive Message."

**Ausgeschlossen per `.gitignore`** (zusammen über 4 GB, alles reproduzierbar, Bezugsquellen in `README.md`): heruntergeladene Werkzeuge (`tools/openocd`, `fw-loader`, `kp-firmware`, `arduino-cli`), Arduino-Core und Bibliotheken (`arduino/.arduino15`, `sketchbook/libraries`), Build-Ordner, Python-venv, SavedModel, das Infineon-Beispiel `examples/` (eigenes Repo mit 51 MB inklusive 25-MB-GIF), Zephyr-Workspace, Mitschnitte im Projektstamm (Kopien liegen bei den Sketches), Playwright-Reste.

**Aufgenommen:** Dokumente, Skripte, Sketches mit generierten Headern, Firmware-Dateien samt Werkssicherung (5,6 MB), MicroPython-Firmware (3,3 MB), die 102 Trainingsaufnahmen (8,4 MB), Modell und Trainingsbericht, Screenshots. 154 Dateien, Repo 9 MB.

**Historie** (thematisch, nicht chronologisch exakt; jeder Commit entspricht einem Teil des Protokolls):
```
4adc68a Projektgrundlage: Protokoll, Board-Übersicht, README, Ignore-Regeln
966ff62 Firmware-Update unter Linux: Flash-Skripte, udev-Regel, Streaming-Firmware, Werkssicherung
00d2623 Arduino-Core: Blink-Test, IMU-Gestenerkennung, regelbasierter Klatsch-Schalter
f87cf3e Live-Viewer: Browser-Anzeige für Gesten- und Klatsch-Modus
72d58bf Alternativen ohne Infineon-Konto: MicroPython-Firmware und Zephyr-Vorbereitung
1422384 Audio-Aufnahme: Streaming-Sketch, geführtes Aufnahme-Werkzeug, Trainingsdaten
2ee61c9 Klatsch-Modell: Merkmale, Training, int8-Export, Inferenz mit TensorFlow Lite Micro
```
Branch `main`, Autor laut globaler Git-Konfiguration (makreft). Kein Remote eingerichtet.

---

# Teil 6: Anwesenheit per WLAN

## 52. Anforderung und Technikprüfung

Marco: Die LED soll ausgehen, sobald sein Handy nicht mehr im Funkbereich des Boards ist. Keine Verbindung des Handys mit einem Board-WLAN gewünscht, lieber passives Scannen; dann: "ich würde es schon viel cooler finden, wenn der Chip der Access Point ist und nicht mein Handy oder ein WLAN-Router dafür gebraucht wird."

**Was ein WLAN-Scan kann und was nicht:** `WiFi.scanNetworks()` liefert nur Access Points (Beacons). Ein Handy, das als Client in einem Heimnetz hängt, taucht in keinem Scan auf. Passive Erkennung von Clients bräuchte einen Monitor-Modus des Funkchips.

**Monitor-Modus geprüft** (mit `arm-none-eabi-nm` auf der vorkompilierten `mtb-libs.a` der Board-Variante und in den WHD-Headern des Cores): Die dafür nötigen Treiberfunktionen `whd_wifi_enable_monitor_mode` und `whd_wifi_set_raw_packet_processor` fehlen sowohl in der Bibliothek als auch im Header dieser Treiberversion (COMPONENT_WIFI6). Vorhanden sind nur generische iovar/ioctl-Zugriffe und `whd_wifi_set_channel`. Selbst wenn sich der Chip damit in den Monitor-Modus schalten ließe, würde der Empfangspfad des Treibers rohe 802.11-Rahmen verwerfen. Dazu kämen MAC-Randomisierung der Handys und die Kanalbindung. Bewertung: mehrere Tage mit ungewissem Ausgang, nicht "eben kurz". Verworfen.

**Alternativen bewertet:**
1. Hotspot des Handys als Beacon, Board scannt: passiv, ohne Zugangsdaten, aber Hotspot muss an sein (iPhone schaltet ihn ohne Clients nach etwa 90 s unsichtbar).
2. Board im Heimnetz, Ping aufs Handy: zuverlässig, braucht Zugangsdaten und feste IP.
3. Bluetooth: ideal, aber keine Bibliothek im Arduino-Core.
4. **Board als Access Point** (Marcos Wunsch): Handy speichert das Netz einmal, verbindet sich automatisch in Reichweite, meldet sich beim Verlassen ab. Anwesenheit = Client angemeldet. Kein Router, keine Zugangsdaten fremder Netze.

**Treiberschicht für Weg 4 geprüft:** Die Arduino-Bibliothek kann `WiFi.beginAP(ssid, passwort, kanal)`, listet aber keine Clients. In `mtb-libs.a` sind vorhanden: `cy_wcm_register_event_callback` mit den Ereignissen `CY_WCM_EVENT_STA_JOINED_SOFTAP` und `CY_WCM_EVENT_STA_LEFT_SOFTAP` (liefern die MAC des Geräts), `cy_wcm_get_associated_client_list(mac_liste, anzahl)`, `cy_wcm_get_whd_interface` (nicht im öffentlichen Header, Prototyp selbst deklariert) und `whd_wifi_get_ap_client_rssi(ifp, &rssi, mac)`. Damit ist alles da. Standard-IP des AP ist 192.168.0.1 mit DHCP für die Clients.

## 53. Sketch 06-presence-ap

Marco: "kannst du bitte probieren, die Funktionalität entsprechend zu erweitern? Wenn das funktioniert, könnte man das ja sogar als PR in das Arduino-Projekt zurückspielen." Plan: zuerst standalone im Sketch beweisen, dann als API in die WiFi-Bibliothek des Cores einbauen und als Patch für `Infineon/arduino-core-psoc6` vorbereiten. Der PR selbst erst nach Marcos Freigabe.

**Sketch** (`arduino/sketchbook/06-presence-ap/`, 496 KB Flash wegen WLAN-Stack, 30 KB RAM):
- Access Point "AI-Eval-Kit", WPA2-Passwort `aievalkit2026`, Kanal 6, LED2 an = AP läuft.
- Ereignis-Callback merkt An- und Abmeldungen (läuft im Treiber-Thread, Ausgabe im `loop()`).
- Jede Sekunde Client-Liste abfragen, je Client die Signalstärke, optional auf eine MAC (`PHONE_MAC`) einschränken. Anwesend = mindestens ein Client, LED1 entsprechend. Optionale Karenzzeit `GRACE_MS`.
- Konsole: `AP ssid=.. ip=.. kanal=..`, `CLIENT join|leave <mac>`, `PRAESENZ an|aus clients=n rssi=.. dBm <mac>`, alle 10 s `STATUS ...`, jede Sekunde `P,<rssi>,<clients>` für den Viewer.

Kompilierfehler unterwegs: `cy_wcm_get_whd_interface` ist in `cy_wcm.h` nicht deklariert, obwohl die Funktion in der Bibliothek liegt; Prototyp im Sketch ergänzt.

**Viewer:** neuer Modus "Anwesenheit per Access Point": Hero "anwesend/abwesend" mit Geräteanzahl, Signalstärke und MAC; Diagramm der Signalstärke der letzten 10 Minuten mit Anwesenheitsband und Markierungen für An-/Abmeldungen; Hinweis mit SSID, IP und Kanal. Mitschnitt-Zeilen `P` (Signal) und `E` (Ereignisse).

## 54. Klarstellung: Erkennung ohne jede Anmeldung des Handys

Marco: "ich will, dass das Board checkt, sobald mein Handy in der Nähe ist, ohne dass ich mich damit connecten muss." Damit ist der Access-Point-Weg (Abschnitt 53) nicht das Gewünschte, auch wenn er technisch funktioniert (AP "AI-Eval-Kit" vom Laptop aus sichtbar, Kanal 6, WPA2). Praktischer Nachteil wäre ohnehin: Das Handy verliert in Reichweite des Boards sein Internet, weil es ins Board-WLAN wechselt.

**Was ein Handy von sich aus funkt, und was das Board davon sehen kann:**
| Signal des Handys | Sichtbar für dieses Board? |
|---|---|
| WLAN-Datenverkehr im Heimnetz | Nein: bräuchte Monitor-Modus, fehlt im Treiber des Arduino-Cores (Abschnitt 52); zudem MAC-Randomisierung |
| WLAN-Probe-Requests | Nein: dito, und seit iOS 14 / Android 10 mit Zufalls-MAC, also nicht dem Gerät zuzuordnen |
| Hotspot des Handys (Beacons) | Ja: `WiFi.scanNetworks()` liefert SSID, BSSID, Signalstärke. Handy muss den Hotspot anhaben |
| BLE-Advertisements (Apple Continuity, Android Nearby, Beacon-Apps) | Nur mit Bluetooth-Stack. Arduino-Core: keiner. MicroPython-Port: keiner (Quickref geprüft, nur WLAN). Zephyr: BT-HCI-Treiber für Infineon-Chips vorhanden, das Board CY8CPROTO-062-4343W hat einen BT-Knoten im Devicetree, das AI-Kit nicht; BT-UART-Pins des Kits laut BSP P3_0 bis P3_3 (SCB2), Power P3_4, Wake P3_5/P4_0. Bring-up als Overlay möglich, aber unsicher und aufwendig |

**Einordnung BLE:** Selbst mit BLE-Scanner ist ein bestimmtes Handy ohne Kopplung nicht sicher zu identifizieren, weil die Adressen alle 15 Minuten wechseln. Zuverlässig wird es, wenn auf dem Handy eine Beacon-App mit fester UUID läuft (dann scannt das Board nach dieser UUID) oder nach einmaliger Kopplung (dann kann das Board die Zufallsadressen auflösen). Beides wäre ein Zephyr-Projekt von mindestens einem Tag mit offenem Ausgang beim Bluetooth-Bring-up.

**Fazit:** Vollständig passiv und ohne Mitwirkung des Handys geht es mit diesem Board und diesem Software-Stand nicht. Der kleinste Schritt, der die Anforderung "nichts verbinden" erfüllt: Hotspot am Handy an, Board scannt (Abschnitt 55). Entscheidung steht aus.

---

# Teil 7: Anwesenheit per Bluetooth (Zephyr)

## 55. Entscheidung und Zephyr-Recherche

Marco: "nee, dann machen wir es mit Bluetooth" und "das Board soll via Bluetooth erkennen, dass mein Handy in der Nähe ist." Zephyr ist der einzige Weg zu einem Bluetooth-Stack auf diesem Board ohne Infineon-Konto (Abschnitt 54).

**Recherche in Zephyr v4.4.2 (Release vom 2026-08-07):**
- Treiber `drivers/bluetooth/hci/hci_uart_infineon.c` mit Binding `infineon,bt-hci-uart`: lädt die Controller-Firmware (HCD-Format) per HCI über UART in den Chip, dann normaler H4-Betrieb. Die Kconfig-Auswahl `AIROC_PART` kennt `CYW43439` ausdrücklich.
- Das Beispiel im Binding verwendet genau die Pins des AI-Kits laut Infineon-BSP: UART auf SCB2 mit P3.1 TX, P3.0 RX, P3.2 RTS, P3.3 CTS (Hardware-Flusskontrolle Pflicht für H4), REG_ON auf P3.4, Firmware-Download mit 3 MBaud.
- Das Schwesterboard CY8CPROTO-062-4343W (gleicher PSOC 6, Murata 1DX mit CYW4343W) hat den Bluetooth-Knoten im Zephyr-Devicetree; das AI-Kit (Murata 1YN mit CYW43439) nicht. Lösung: Overlay in der Anwendung, das `uart2` mit diesen Pins und den `bt_hci_uart`-Knoten definiert.
- Firmware-Blobs über `west blobs fetch hal_infineon` (Hinweis im Binding).

**Angelegt:** `zephyr/app/ble_presence/` mit `CMakeLists.txt`, `prj.conf` (BT Observer, AIROC, CYW43439, H4), `boards/cy8ckit_062s2_ai.overlay` (Bluetooth-Knoten) und `src/main.c`: passiver BLE-Scanner, erkennt das Handy an einer iBeacon-UUID oder einem Advertisement-Namen, glättet die Signalstärke, schaltet LED1 mit Hysterese (-85 dBm an, -92 dBm aus) und 8 s Verlust-Timeout. Konsolenausgabe im Format von Sketch 06, damit der Viewer-Modus "Anwesenheit" direkt passt. Zusätzlich eine ADV-Übersicht aller Sender (ratenbegrenzt), um das eigene Handy zu identifizieren.

**Handy-Seite (noch offen, hängt vom Gerät ab):** Ohne Kopplung wechseln Handys ihre Bluetooth-Adresse etwa alle 15 Minuten, deshalb braucht das Board ein stabiles Erkennungsmerkmal, das das Handy aussendet: eine Beacon-App mit fester UUID (Android: im Hintergrund möglich; iPhone: iBeacon-Advertising nur bei App im Vordergrund). Ob iPhone oder Android, ist noch nicht beantwortet.

**Workspace:** `zephyr/setup.sh` auf Release v4.4.2 gesetzt und gestartet (Log `zephyr/setup.log`). Lädt Zephyr, hal_infineon, CMSIS, picolibc, tflite-micro, die Python-Abhängigkeiten und das Zephyr-SDK (ARM-Toolchain), dann die Blobs.

## 56. Frage: Klatsch-Erkennung und Bluetooth parallel?

Marco: "kannst du das so machen, dass die Clap-Erkennung und die BT-Erkennung parallel funktionieren?"

**Rahmenbedingung:** Bluetooth gibt es nur unter Zephyr, die Klatsch-Erkennung bisher nur unter dem Arduino-Core. Zwei Firmwares können nicht gleichzeitig auf dem Chip laufen (der zweite Kern CM0+ ist in beiden Welten für ein vorgefertigtes Schlaf-Image reserviert). Parallel heißt deshalb: alles in einer Zephyr-Firmware.

**Geprüft in Zephyr v4.4.2:**
- Es gibt `drivers/audio/dmic_infineon.c` mit Binding `infineon,pdm`. Der Treiber benutzt aber `cy_pdm_pcm_v2.h`, DMA und Trigger-Mux, und Devicetree-Knoten dafür existieren nur in `dts/arm/infineon/edge/pse84/` (PSOC Edge). Der PSOC 6 hat den PDM-PCM-Block v1 (`cy_pdm_pcm.h` im PDL), dafür gibt es in Zephyr weder Treiber noch Knoten.
- Die PDL-Funktionen für den v1-Block (`Cy_PDM_PCM_Init`, FIFO, Interrupts) liegen im Modul hal_infineon vor. Ein eigener Minimaltreiber (Takt für den Audio-Block, Init, FIFO-Interrupt, Lesen in Puffer) ist machbar, geschätzt 150 bis 250 Zeilen, Risiko beim Audio-Takt (24,576 MHz über PLL/clk_hf1, in der Arduino-Bibliothek von der HAL erledigt).
- TensorFlow Lite Micro ist im Zephyr-Manifest 4.4 nicht mehr als Modul enthalten. Die Quellen aus der Arduino-Bibliothek Chirale_TensorFLowLite (reiner TFLM-Baum, 215 Dateien) lassen sich als Zephyr-Bibliothek in die Anwendung einbinden.
- Merkmalsberechnung (`features`, eigene FFT) ist reines C und portiert sich direkt.

**Plan, falls gewünscht:**
1. Bluetooth-Anwesenheit unter Zephyr zum Laufen bringen (Abschnitt 55, läuft).
2. PDM-Treiber für den PSOC 6 in Zephyr schreiben, zuerst mit dem regelbasierten Klatsch-Detektor als Test (kein Modell).
3. Merkmale und TFLM-Modell portieren, Klatsch-Modell unter Zephyr.
4. Beides in einer Anwendung mit zwei Threads: Scanner und Audio.
Aufwand: Schritt 1 heute, Schritte 2 bis 4 ein bis zwei Tage, mit dem PDM-Takt als größtem Risiko. Entscheidung steht aus.

**Entscheidung (Marco):** voller Weg, also alles in einer Zephyr-Firmware. Handy ist ein Android. Marco ist vorerst weg; die folgenden Schritte laufen ohne Rückfrage, solange sie keine Hardware-Mitwirkung brauchen: Zephyr-Build und Flash, Bluetooth-Bring-up (Scanner sieht auch ohne Handy fremde BLE-Geräte, das reicht als Nachweis), danach der Mikrofontreiber. Beacon-App für Android: "Beacon Simulator" (iBeacon mit frei wählbarer UUID, läuft im Hintergrund), UUID aus `main.c`.

## 57. Zephyr-Workspace steht, Toolchain auf dem Board verifiziert

`zephyr/setup.sh` ist in etwa 20 Minuten durchgelaufen (Zephyr v4.4.2, Module hal_infineon, cmsis, cmsis_6, picolibc; Python-Abhängigkeiten; Zephyr-SDK 1.0.1 nur mit ARM-Toolchain; Blobs für hal_infineon inklusive Bluetooth- und WLAN-Firmware; OpenOCD aus `tools/openocd` per `west config` eingetragen). Workspace unter `zephyr/` (im Repo ausgeschlossen, nur `app/`, `README.md`, `setup.sh` sind versioniert).

**Testbuild hello_world:**
```
west build -p always -b cy8ckit_062s2_ai/cy8c624abzi_s2d44 -d build/hello zephyr/samples/hello_world
FLASH: 42708 B (2,04 %), RAM: 18636 B (1,78 %), Build 13 s
west flash -d build/hello
wrote 43008 bytes ... in 0.502876s
```
Konsole (über den Ereignisstrom von Marcos laufendem Viewer mitgelesen, weil der den Port hält): `*** Booting Zephyr OS build v4.4.2 ***` und `Hello World! cy8ckit_062s2_ai/cy8c624abzi_s2d44`. Damit funktioniert die komplette Kette: west, SDK, Board-Definition, Flash über unser OpenOCD, Konsole an J1.

Nützlich für später: `tools/flash.sh reset` setzt das Board auch unter Zephyr sauber zurück; die Zephyr-Konsole liegt auf derselben KitProg3-Schnittstelle mit 115200 Baud wie bisher.

## 58. Bluetooth-Anwendung: Build-Hürden und Lösungen

Vier Anläufe bis zum ersten erfolgreichen Build von `app/ble_presence` (am Ende 81.512 Bytes Flash, 41.940 Bytes RAM, Build 21 s):

1. **Devicetree: "undefined node label 'uart2'".** Das Schwesterboard-Beispiel aus dem Binding verwendet `&uart2`, aber die cat1a-Boards in Zephyr 4.4 definieren UARTs anders: Die SCB-Knoten heißen `scb0` bis `scb8`, und das Board weist sie zu, z. B. `uart5: &scb5 { compatible = "infineon,uart"; ... }`. Das Overlay macht es jetzt genauso: `uart2: &scb2 { compatible = "infineon,uart"; ... }` mit den Pinctrl-Knoten `p3_1_scb2_uart_tx`, `p3_0_scb2_uart_rx`, `p3_2_scb2_uart_rts`, `p3_3_scb2_uart_cts` (existieren im SoC-DTS) und `hw-flow-control`.
2. **Kconfig: "PSA_CRYPTO ... direct dependencies 0".** Der Bluetooth-Host wählt `BT_HOST_CRYPTO`, das PSA-Krypto aus dem Modul mbedtls braucht. Das schlanke Manifest hatte mbedtls nicht in der Allowlist. Ergänzt; Hinweis: `west update <projekt>` geht für importierte Projekte nicht, nur ein volles `west update`.
3. **CMake: "tf-psa-crypto ... not an existing directory".** mbedtls 4 in Zephyr 4.4 braucht zusätzlich das Projekt `tf-psa-crypto` (Pfad `modules/crypto/tf-psa-crypto`). Ebenfalls in die Allowlist (auch in `setup.sh` für künftige Neuaufsetzungen).
4. **Compiler: "__device_dts_ord_108 undeclared" in `hci_uart_infineon.c`.** Ordnungsnummer 108 ist laut `devicetree_generated.h` der GPIO-Port 3 (`gpio@40310180`), den der Treiber für die REG_ON-Leitung braucht. Das Board-DTS aktiviert nur die Ports 5 und 10 (LEDs, Taster, I2C). Overlay aktiviert jetzt `gpio_prt3` und `gpio_prt4`.

Zweite LED: Das Board-DTS hat nur den Alias `led0` (P5.3); LED2 (P5.4) existiert als Knoten `user_led1` ohne Alias, im Code daher `DT_NODELABEL(user_led1)`.

## 59. Bluetooth läuft: der Controller braucht nach dem Firmware-Start mehr Zeit

**Erster Flash:** Zephyr bootet, dann `ASSERTION FAIL [err == 0] @ hci_core.c:482` und `Controller unresponsive, command opcode 0x0c03 timeout with err -11`. Der Bluetooth-Host bekam auf das HCI-Reset keine Antwort.

**Diagnose mit Treiber-Debug-Logs** (`CONFIG_BT_HCI_DRIVER_LOG_LEVEL_DBG=y`, zusätzlich `CONFIG_AIROC_AUTOBAUD_MODE=n` ausdrücklich gesetzt und BT_DEVICE_WAKE P3.5 vorsorglich hoch gezogen):
- Power-on und erstes HCI-Reset bei 115200 Baud: Antwort kommt.
- Umschalten auf 3 MBaud und Firmware-Download (HCD, Befehle 0xFC4C Write RAM und 0xFC4E Launch RAM): "Fw downloading complete" nach etwa 100 ms.
- Danach wartet der Treiber `BT_STABILIZATION_DELAY_MS` = 250 ms, stellt die Host-UART auf 115200 zurück und sendet das zweite HCI-Reset. Darauf kam nichts mehr. Der CYW43439 startet seine Firmware nach Launch RAM offenbar langsamer als der CYW4343W, für den der Treiber entwickelt wurde; das Reset ging ins Leere, und der Host wartet ohne Wiederholung 10 s auf Antwort.

**Fix:** `BT_STABILIZATION_DELAY_MS` in `drivers/bluetooth/hci/hci_uart_infineon.c` von 250 auf 1000 ms. Danach:
```
*** Booting Zephyr OS build v4.4.2 ***
Bluetooth-Anwesenheit (Zephyr) auf dem CY8CKIT-062S2-AI
Bluetooth bereit
AP ssid=BLE-Scan ip=none kanal=0
Scanne passiv. Ziel: iBeacon-UUID oder Name "AI-Eval-Kit-Handy". LED1 = anwesend.
ADV 0A:3A:D7:7F:08:E4 (random) -57 other
ADV 79:46:99:2A:28:A8 (random) -61 apple
ADV 49:94:D9:C9:52:77 (random) -73 apple
...
```
Der passive Scanner sah in 30 s rund 25 verschiedene BLE-Sender zwischen -57 und -91 dBm, die meisten mit Apple-Herstellerdaten und Zufallsadressen (das sind iPhones, Watches, AirPods in der Nachbarschaft). Die Zeilen `P,-100,0` laufen im Sekundentakt, der Viewer schaltet in den Anwesenheits-Modus und zeigt die Hinweiszeile. Kein Ziel erkannt, weil noch keine Beacon-App sendet.

Ein Fehlversuch dazwischen: Mein `sed` hatte aus `(250u)` ein `(1000)u)` gemacht, Kompilierfehler, von Hand korrigiert.

**Dauerhaft gesichert:** Die Treiberänderung liegt als `zephyr/patches/0001-bt-hci-uart-infineon-longer-stabilization-delay.patch` im Repo, `setup.sh` wendet alle Patches nach `west update` an. Für Zephyr upstream wäre das ein Kandidat: entweder eine längere Standardzeit für CYW43439 oder eine Kconfig-Option `BT_AIROC_STABILIZATION_DELAY_MS`. Bei 4343W-Boards stört ein längeres Warten nicht, es kostet nur 750 ms beim Start.

**Zahlen:** Anwendung 81.564 Bytes Flash (inklusive BT-Firmware-Blob und Host-Stack), 41.940 Bytes RAM. Build 21 s, Flash 0,8 s, Bluetooth bereit etwa 2,5 s nach Reset.

**Für den Handy-Test (Android):** App "Beacon Simulator" (Vincent Hiribarren, Play Store), neuen iBeacon anlegen mit UUID `A1E7A14B-1700-4A1C-9E5A-0C620000C147`, Major/Minor beliebig, Sendeleistung normal, Beacon starten (läuft im Hintergrund weiter). Alternativ ein Advertisement mit Gerätename `AI-Eval-Kit-Handy`. Beide Kennungen stehen in `zephyr/app/ble_presence/src/main.c`.

## 60. Mikrofon unter Zephyr: Vorbereitung (Stand bei Marcos Aufbruch)

**Weg:** Zephyr hat für den PSOC 6 keinen Mikrofontreiber (Abschnitt 56), aber die Infineon-HAL liegt im Modul hal_infineon vollständig vor: `mtb-hal-cat1/source/cyhal_pdmpcm.c`, `cyhal_audio_common.c` und der PDL-Treiber `mtb-pdl-cat1/drivers/source/cy_pdm_pcm.c`. Zephyr kompiliert diese Dateien für CAT1A nicht von sich aus; die Anwendung bindet sie selbst ein. Takt wie in Infineons Arduino-PDM-Bibliothek: PLL0 auf 24,576 MHz, HF1 (Audio-Subsystem) daraus. Zephyr nutzt die FLL für HF0, PLL0 ist frei. Lesen synchron per `cyhal_pdm_pcm_read` aus dem FIFO (256 Wörter), gepollt alle 2 ms, in 128-Sample-Blöcken verarbeitet; der asynchrone Pfad bräuchte den HAL-DMA-Treiber, der in Zephyr nicht mitgebaut wird.

**Angelegt, noch nicht gebaut:** `zephyr/app/clap_rules/` mit `src/mic.c` (Takt und PDM über HAL, Pins P10.4/P10.5) und `src/main.c` (regelbasierter Klatsch-Detektor, 1:1 aus Sketch 03, gleiche Konsolenzeilen für den Viewer). Offene Punkte beim ersten Build: ob die Pin-Makros `P10_4`/`P10_5` über die vorhandenen Include-Pfade erreichbar sind, ob `cyhal_pdmpcm.c` ohne den DMA-Teil linkt (sonst DMA-Aufrufe per Stub ersetzen oder Lesepfad auf PDL umstellen), ob die HAL-Takt-Reservierung neben Zephyrs Clock-Control funktioniert.

**Danach:** Merkmale und TFLM-Modell nach Zephyr (TFLM-Quellen aus der Arduino-Bibliothek als Zephyr-Bibliothek), dann Zusammenführung mit `ble_presence` in eine Anwendung mit zwei Threads.

**Nachtrag 2026-10-09:** "Beacon Simulator" läuft auf Marcos aktuellem Android nicht mehr (laut Marco inkompatibel, letztes Update 2020). Ersatz: nRF Connect for Mobile von Nordic, Advertiser mit Manufacturer-Data-Record (Company ID 004C, iBeacon-Payload mit der UUID) oder Gerätename `AI-Eval-Kit-Handy`; Details in `zephyr/README.md`. Akku-Optimierung für die App deaktivieren, damit das Advertising im Hintergrund weiterläuft.

## 61. Handy-Test: Erkennung per Bluetooth funktioniert

**App-Seite:** "Beacon Simulator" lief auf Marcos Android nicht; nRF Connect for Mobile stattdessen. Marcos Screenshot zeigte den Grund für den ersten Fehlschlag: Der Advertiser-Eintrag hatte unter "Advertising data" keinen Record, das Paket war leer (der "Display name" ist nur die Bezeichnung in der App). Mit Record "Manufacturer Specific Data", Company ID `004C`, Data `0215A1E7A14B17004A1C9E5A0C620000C14700010001C5`, Legacy-PHY, Adv. Extension aus, Intervall 250 ms, Tx-Power 1 dBm:
```
ADV 42:DD:05:C1:B6:6D (random) -53 ZIEL raw=1aff4c000215a1e7a14b17004a1c9e5a0c620000c14700010001c5
PRAESENZ an  clients=1  rssi=-53 dBm  42:DD:05:C1:B6:6D (random)
P,-50,1  P,-51,1  P,-50,1 ...
```
Das Board erkennt das Handy innerhalb einer Sekunde, Signalstärke neben dem Board etwa -50 dBm, LED1 an. Der Viewer zeigt "anwesend" mit Kurve.

**Zwei Erkenntnisse zum Scannen unterwegs:**
- Aktives Scannen (Scan-Antworten anfordern) akzeptiert der Controller; sinnvoll, weil Android Namen und Herstellerdaten oft in die Scan-Antwort legt.
- Extended Scanning (`CONFIG_BT_EXT_ADV=y`) lehnt der CYW43439-Controller ab: `bt_le_scan_start` liefert -5. Das Handy muss also klassisch (legacy) senden, was in nRF Connect die Voreinstellung ist. Im Code vermerkt.
- Zur Diagnose loggt der Scanner für Sender über -60 dBm die rohen Advertising-Bytes (vor dem Parsen gesichert, weil `bt_data_parse` den Puffer verbraucht). Dabei fiel auf, dass Marcos Handy von sich aus "Find My Device"-Pakete von Google sendet (Service-UUID FCF1), mit wechselnden Zufallsadressen; ein Beleg dafür, warum Erkennung ohne eigenes Kennzeichen nicht geht.

Entfernungstest (Weggehen, Zurückkommen, Zeit bis "aus"/"an") folgt in Abschnitt 62.

## 62. Entfernungstest

Mitschnitt 132 s (08:40 bis 08:43), Marco geht mit dem Handy in einen anderen Raum und kommt zurück. Signalstärke je Sekunde (geglättet, `.` = kein Paket seit mehr als 8 s):
```
   0s  -50 -49 -48 -49 -49 -49 -48 -49 -49 -50 -51 -51 -50 -53 -49 -51 -52 -51 -51 -55
  20s  -55 -57 -57 -68 -68 -60 -64 -68 -70 -64 -64 -60 -62 -61 -70 -74 -79 -76 -82  .
  40s   .   .   .   .   .   .  -84 -83 -76 -71 -64 -58 -41 -38 -36 -37 -38 -39 -46 -51
  60s  -55 -57 -55 -57 -60 -56 -55 -54 -53 -53 -52 -52 -53 -54 -53 -53 -53 -53 -54 -53
```
- Neben dem Board: -50 dBm. Beim Weggehen fällt das Signal über -60 bis -70 (im Raum) auf -82 (Nachbarraum). Der Punkt `.` zeigt "abwesend" (zweiter Wert der P-Zeile), nicht Funkabriss.
- "abwesend" bei Sekunde 39 (`PRAESENZ aus clients=1`): Die geglättete Signalstärke fiel unter die Ausschaltschwelle -92 dBm, während noch Pakete ankamen. Die Glättung (Faktor 0,3) verzögert das um einige Sekunden. LED1 aus.
- Zurückkommen: bei Sekunde 46 wieder über -85 dBm (-84), "anwesend" sofort, LED1 an. Nah am Board bis -36 dBm.
- `CLIENT leave`/`join` erschienen nicht, weil diese Zeilen nur bei Verlust-Timeout bzw. Erstkontakt ausgegeben werden; hier schaltete die Hysterese. Kein Fehler.

**Bewertung:** Erkennung funktioniert in beide Richtungen, auch die Hysterese. Aber -92 dBm liegt fast an der Reichweitengrenze: Marco war schon im Nachbarraum (-75 bis -84), bevor das Board abschaltete. Für "Nachbarraum = weg" sind die Schwellen zu locker.

**Angepasst (gebaut und geflasht):** Einschalten ab -72 dBm, Ausschalten unter -80 dBm, Timeout 5 s (20 Sendeintervalle). Damit zählt der eigene Raum als anwesend, der Nachbarraum nicht mehr, und ein Funkabriss wird nach 5 s erkannt. Zweiter Entfernungstest steht aus.


## 63. Anwendungsfall geschärft: Haus verlassen, nicht Raum verlassen

Marco nach dem ersten Entfernungstest: "nee, das war voll ok, Nachbarraum passt. Die LED soll so lange leuchten, wie das Handy in der Nähe ist. Erst wenn ich quasi das Haus verlasse, soll der Schalter umgelegt werden und dadurch im ganzen Haus das Licht ausgeschaltet werden."

Damit ist die Funkreichweite das Kriterium, nicht die Entfernung innerhalb der Reichweite. Die in Abschnitt 62 verschärften Schwellen (-72/-80) sind dafür falsch und wurden zurückgenommen:
- Jedes empfangene Paket hält "anwesend" (Schwellen auf -100 dBm, die Signalstärke schaltet nie aus).
- "abwesend" erst nach 30 s ohne Paket (bei 250 ms Sendeintervall 120 verpasste Pakete), damit Funklöcher im Haus den Schalter nicht kippen. Zurückkommen schaltet beim ersten Paket wieder ein.
- Die Nähe-Variante bleibt als Kommentar im Code dokumentiert.

Praktische Grenzen: Ein Board deckt mit BLE etwa 10 bis 20 m durch Wände ab (gemessen: Nachbarraum noch -84 dBm). Für eine größere Wohnung braucht es entweder einen zentralen Standort oder mehrere Boards. Außerdem muss nRF Connect auf dem Handy dauerhaft senden dürfen (Akku-Optimierung aus), sonst "verlässt" das Handy beim Sperren des Bildschirms scheinbar das Haus.

Gebaut und geflasht. Schaltlogik für echtes Licht (Relais, Funksteckdose, Smart-Home-Schnittstelle) ist der nächste Schritt nach dem Zusammenführen mit der Klatsch-Erkennung.

## 64. Mikrofon unter Zephyr: erster Build

`app/clap_rules` kompiliert beim ersten Anlauf durch (Pins `P10_4`/`P10_5` und die HAL-Taktfunktionen sind über die vorhandenen Include-Pfade erreichbar). Nur der Linker meckerte: `cyhal_pdmpcm.c` referenziert sechs Funktionen des HAL-DMA-Treibers (`cyhal_dma_free`, `is_busy`, `enable_event`, `configure`, `enable`, `start_transfer`), den Zephyr für CAT1A nicht mitbaut. Die Aufrufe liegen nur im asynchronen Lesepfad, den die Anwendung nicht nutzt. Lösung: `src/cyhal_dma_stub.c` mit leeren Implementierungen, die Fehler zurückgeben. Ergebnis: 64.344 Bytes Flash, 33.496 Bytes RAM.

Noch nicht geflasht, weil Marco gerade die Bluetooth-Anwendung auf dem Board testet; das Board kann nur eine Firmware tragen. Offen bleibt damit die Praxisfrage, ob die HAL-Taktreservierung (PLL0, HF1) neben Zephyrs Clock-Control funktioniert und ob das FIFO-Polling alle 2 ms ohne Verluste durchkommt.

## 65. Richtungswechsel: Anwesenheit per klassischem Bluetooth-Paging, ohne App

Marco: "Ich will nicht permanent einen Beacon senden, sondern das Board soll die Liste aller verfügbaren BT-Geräte scannen; solange mein BT-Gerät in der Nähe ist LED an, ist mein BT aus oder nicht in der Nähe, soll die LED ausgehen."

**Warum nicht BLE-Scan:** Ohne eigene App sendet das Handy nur Pakete mit rotierenden Zufallsadressen und rotierenden Kennungen (Google Find My Device, Abschnitt 61), bewusst nicht zuordenbar. Ein klassischer Inquiry-Scan findet Handys nur, solange sie sichtbar geschaltet sind (Android: nur bei geöffneter Bluetooth-Einstellungsseite).

**Lösung: Paging auf die feste klassische Adresse.** Jedes Handy hat eine feste BR/EDR-Adresse (Android: Einstellungen > Über das Telefon > Status > Bluetooth-Adresse). Das Board versucht alle 15 s eine ACL-Verbindung zu dieser Adresse. Erfolg heißt: Handy in Reichweite und Bluetooth an; die Verbindung wird sofort getrennt. Keine Kopplung, kein Dialog, auf dem Handy nichts zu tun. Zwei Fehlversuche in Folge (je 5 s Page-Timeout) gelten als abwesend. Das ist der Mechanismus, den Heimautomationen mit `l2ping` seit Jahren nutzen.

**Zephyr:** Der klassische Stack (`CONFIG_BT_CLASSIC`, experimentell) ist in 4.4.2 vollständig vorhanden (`subsys/bluetooth/host/classic/`), API `bt_conn_create_br(adresse, BT_BR_CONN_PARAM_DEFAULT)` mit `connected`-Callback (err 0 = erreichbar, 0x04 = Page Timeout). Neue Anwendung `zephyr/app/bt_page_presence/` (gleiches Overlay wie `ble_presence`), 171 KB Flash, 66 KB RAM.

**Test mit Platzhalter-Adresse 11:22:33:44:55:66:**
```
bt_hci_core: HCI: version 5.2 (0x0b) revision 0x0000, manufacturer 0x0131
bt_hci_core: LMP: version 5.2 (0x0b) subver 0x2310
Bluetooth bereit (BR/EDR + LE)
PAGE fail 0x04
PAGE fail 0x04
```
Controller (Cypress, Hersteller-ID 0x0131) meldet sich mit BR/EDR 5.2, eigene Adresse 10:32:2C:65:BB:95. Paging an eine nicht existierende Adresse endet nach 5 s mit Page Timeout, "abwesend" bleibt. Eine Warnung des Hosts (ACL-Puffer 8 != 3) ist harmlos.

Nächster Schritt: Marcos Bluetooth-Adresse eintragen und testen (Handy da, Handy weg, Bluetooth aus).

## 66. Paging mit echter Adresse: der klassische Host friert ein

Marcos Bluetooth-Adresse: AA:BB:CC:DD:EE:FF. Erster Lauf (über den Viewer mitgelesen): Banner, zwei Sekundenzeilen, dann Stille. Danach startete Bluetooth auch nach Neuflashen nicht mehr (Hänger vor "Bluetooth bereit"). Nebenbefund: Marcos Viewer von gestern hing noch am alten USB-Gerät (Board war beim Zuklappen getrennt worden, Kernel-Log 15:42 Disconnect, 08:20 Neuerkennung an anderem Port) und lieferte nur noch sporadisch Daten; beendet, Konsole seither direkt gelesen.

**Strom weg und wieder dran** (Marco hat das Kabel gezogen): Bluetooth startet wieder sauber. Der Software-Reset über den Debugger holt den Funkchip also nicht aus einem verklemmten Zustand; nur der Stromreset. Mit Diagnoseausgaben und sofortigem Logging:
```
Adresse ok, starte Bluetooth-Stack ...
bt_hci_core: HCI: version 5.2 ... manufacturer 0x0131
bt_enable -> 0
Bluetooth bereit (BR/EDR + LE)
PAGE start #1
```
Danach 50 s nichts: kein Callback, nicht einmal der 12-s-Timeout des Hauptthreads, keine Sekundenzeilen, kein Fehlerdump. Mit der Platzhalter-Adresse (keine Antwort vom Gegenüber) kam dagegen sauber "PAGE fail 0x04". Schluss: Der experimentelle klassische Host von Zephyr bleibt genau dann hängen, wenn die ACL-Verbindung zum Handy zustande kommt (vermutlich in der anschließenden Sicherheits-/Link-Key-Abwicklung), und reißt dabei das ganze System mit.

**Ausweg:** HCI-Rohmodus (`CONFIG_BT_HCI_RAW`): Die Anwendung spricht den Controller direkt mit HCI-Kommandos an, ohne Host-Stack: Reset, Create Connection auf die Adresse, auf Command Status und Connection Complete warten (Status 0 = erreichbar, 0x04 = Page Timeout), bei Erfolg sofort Disconnect, Link-Key- und PIN-Anfragen negativ beantworten. Wenige Dutzend Zeilen, deterministisch, und der Firmware-Download läuft im Treiber wie bisher. Umsetzung in Abschnitt 67.

## 67. HCI-Rohmodus: Anwesenheit per Remote Name Request funktioniert

**Weg zum Ziel, in Reihenfolge der Stolpersteine:**
1. Im Rohmodus (`CONFIG_BT_HCI_RAW`) fehlen `bt_addr_from_str` (Host) und die zwei Host-Funktionen `bt_hci_cmd_alloc`/`bt_hci_cmd_send_sync`, die der Infineon-Treiber für den Firmware-Download benutzt. Adressparser selbst geschrieben; die zwei Funktionen in der Anwendung nachgebildet (Kommando bauen, senden, auf Command Complete/Status mit passendem Opcode warten).
2. Im Rohmodus ruft Zephyr die Treiber-Einrichtung (`setup`, darin der Firmware-Download) nicht auf. Die Anwendung ruft `bt_hci_setup(DEVICE_DT_GET(DT_CHOSEN(zephyr_bt_hci)), &params)` selbst. `CONFIG_BT_HCI_SETUP` ist nicht setzbar (wird vom Treiber gewählt), Eintrag in `prj.conf` entfernt.
3. Erster Lauf: `bt_hci_setup -> -5`, der Reset bekam keine Antwort. Treiber-Debug zeigte `h4_send: type 3`: In Zephyr 4.4 legt `bt_buf_get_tx` den H4-Pakettyp als erstes Datenbyte ab; mein `net_buf_reserve` hatte es überschrieben. Ebenso tragen empfangene Rohpuffer das Typbyte vorn. Beides korrigiert (Header hinter dem Typbyte eintragen, beim Empfang Typbyte abstreifen).
4. Danach lief der Firmware-Download durch, dauert im Rohmodus aber 20 s statt 0,1 s (vermutlich bleibt es bei 115200 Baud, weil die Nachbildung den Baudraten-Wechsel des Treibers nicht exakt nachvollzieht). Funktional, Optimierung offen.
5. Create Connection: Command Status kam, dann verlor der H4-Treiber beim nächsten Paket die Synchronisation ("ACL header, payload 0") und blieb stehen. Statt dessen **Remote Name Request** (Opcode 0x0419): pagt die Adresse und holt nur den Gerätenamen, keine ACL-Verbindung, keine Kopplung; genau das Verfahren von `hcitool name`.
6. Die Antwort (Remote Name Request Complete, 255 Bytes mit 248 Byte Name) passte nicht in den Ereignispuffer (70 Bytes): `Not enough space in buffer 255/70`. `CONFIG_BT_BUF_EVT_RX_SIZE=255`, dazu `CONFIG_BT_BUF_EVT_RX_COUNT=10` und `CONFIG_BT_BUF_ACL_TX_COUNT=2` wegen einer Build-Assertion (Ereignispuffer müssen zahlreicher sein als ACL-Sendepuffer).

**Ergebnis:**
```
Bluetooth bereit (Controller direkt)
Ziel: AA:BB:CC:DD:EE:FF, alle 15 s ein Verbindungsversuch, 2 Fehlversuche = abwesend
PAGE ok 296 ms  Name: Marcos-Handy
CLIENT join AA:BB:CC:DD:EE:FF
PRAESENZ an  clients=1  rssi=-50 dBm  AA:BB:CC:DD:EE:FF
PAGE ok 465 ms  Name: Marcos-Handy
```
Das Handy antwortet in 300 bis 470 ms, LED1 an, Viewer-Modus Anwesenheit passt. Anwendung 63.540 Bytes Flash, 36.508 Bytes RAM. Auf dem Handy ist nichts installiert und nichts eingestellt; Bluetooth muss nur an sein.

Offen: Test "Bluetooth am Handy aus" und "außer Reichweite" (Abschnitt 68), Firmware-Download-Dauer, Wiedereinbau des Rohmodus-Wissens in `ble_presence` (nicht mehr nötig, bleibt als Referenz).

## 68. Test: Bluetooth am Handy aus und wieder an

Mitschnitt 195 s plus Live-Lesung danach. Marco schaltet Bluetooth am Handy aus und wieder ein.
```
t= 150 s  PAGE ok 451 ms  Name: Marcos-Handy        (letzte erfolgreiche Abfrage)
t= 165 s  PAGE fail 0x04 nach 5169 ms            (Bluetooth inzwischen aus: Page Timeout)
t= 175 s  PAGE fail 0x04 nach 5169 ms
t= 175 s  CLIENT leave AA:BB:CC:DD:EE:FF
t= 175 s  PRAESENZ aus  clients=0                (LED1 aus)
...       PAGE fail 0x04 ... (alle 15 s, solange Bluetooth aus)
          PAGE ok 466 ms  Name: Marcos-Handy        (Bluetooth wieder an: beim naechsten Versuch erkannt)
          PRAESENZ an, LED1 an
```
- Erkennung "weg": zwischen 20 und 35 s nach dem Ausschalten (Abfrageintervall 15 s, Page-Timeout 5,2 s, zwei Fehlversuche). Für "Haus verlassen, Licht aus" angemessen; wer es schneller will, setzt `PAGE_INTERVAL_MS` auf 5000 bei gleicher Fehlversuchszahl.
- Erkennung "da": spätestens 15 s nach dem Einschalten, Antwortzeit 450 bis 470 ms.
- Bei anwesendem Handy antwortet jede Abfrage; in 150 s kein einziger Aussetzer.
- Nebenbeobachtung: Ohne Leser auf der seriellen Schnittstelle puffert der KitProg3 die Ausgaben und liefert sie beim nächsten Öffnen gesammelt nach.

Damit erfüllt `bt_page_presence` Marcos Anforderung: keine App, keine Kopplung, LED an solange das Handy mit eingeschaltetem Bluetooth in Reichweite ist, aus wenn Bluetooth aus oder außer Reichweite. Der Entfernungstest ("Haus verlassen") folgt im Alltag; der Mechanismus ist derselbe wie beim Ausschalten (Page Timeout).

## 69. Mikrofon unter Zephyr läuft

`app/clap_rules` geflasht (Marco: "ja, mach weiter mit dem Mikrofon"). Erster Lauf zeigte zunächst keine Pegelzeilen, der zweite mit demselben Build dann doch; Ursache des ersten Aussetzers unklar (vermutlich Mitschnitt zu früh beendet, der Start dauert einige Sekunden). Diagnose-Build mit explizit gesetzter Taktquelle (Pfad 1 auf IMO, bevor die PLL konfiguriert wird) und Ausgaben:
```
mic: Pfad1-Quelle 0, Pfad1 8000000 Hz vor PLL
mic: PLL0 24571429 Hz, HF1 24571429 Hz
mic: FIFO nach 50 ms: 255 Woerter, Interrupt-Status 0x00250000
DIAG bloecke=245 fifo=33   (alle 2 s: +251 Bloecke = 125,5 Bloecke/s = 16.064 Samples/s)
```
Takt und Abtastrate stimmen (PLL 24,57 MHz, 16 kHz), das FIFO wird per Polling alle 2 ms geleert (Füllstand 33 bis 34 Wörter), keine Überläufe im Betrieb. Die HAL-Taktreservierung funktioniert neben Zephyrs Clock-Control. Build 64.696 Bytes Flash.

**Pegelmaßstab noch offen:** Die Pegel liegen um 800 bis 1.000 (RMS der 16-Bit-Skala), der Arduino-Sketch zeigte im ruhigen Büro 30 bis 50. Marco war aber während des Mitschnitts im Zug; in 46 s Zuglärm (Grundrauschen 815, Spitzen bis 2.345) hat der regelbasierte Detektor nicht ausgelöst, was für die Robustheit der Regeln spricht. Ob die Skala zum Arduino-Stand passt, zeigt ein Klatschtest in ruhiger Umgebung (ausstehend). Die absoluten Schwellen (`MIN_RMS`, `MIN_PEAK_RMS`) müssten sonst angepasst werden; die Verhältnis-Logik ist davon unabhängig.

## 70. Klatsch-Modell unter Zephyr: TensorFlow Lite Micro als Teil der Anwendung

`app/clap_model`: Mikrofon (HAL, wie `clap_rules`), Merkmalsberechnung und Modellaufruf aus Sketch 05 nach C++ übertragen, TFLM-Quellen direkt aus der Arduino-Bibliothek Chirale_TensorFLowLite (`arduino/sketchbook/libraries/.../src`, per CMake-Variable `TFLM_DIR` überschreibbar). 215 Quelldateien (146 .cpp, 69 .c inklusive CMSIS-NN), ausgelassen: `system_setup.cpp` und `peripherals/` (einzige Dateien mit `Arduino.h`).

**Drei Hürden:**
1. Ohne das Makro `ARDUINO` kompiliert `kernel_util.cpp` Code für dynamische Tensor-Arrays, der im statischen TFLM nicht existiert. Die Bibliothek ist für Arduino-Builds aufbereitet, bei denen `ARDUINO` immer gesetzt ist. Lösung: `ARDUINO=100` als Compiler-Define, die Arduino-Header werden trotzdem nicht gebraucht.
2. `zephyr_library_named(tflm)` baute ein 24-MB-Archiv mit allen Symbolen, das aber nicht in der finalen Link-Zeile auftauchte (nur `libapp.a` und `libzephyr.a`). Lösung: TFLM-Quellen per `target_sources(app ...)` direkt ans Anwendungsziel, Warnungen per `set_source_files_properties` gedämpft.
3. `M_PI` ist im strikten C++17-Modus nicht definiert; selbst definiert.

**Ergebnis:** 136.524 Bytes Flash, 104.408 Bytes RAM (davon 40 KB Arena-Reserve, 12.308 genutzt; TFLM-Build etwa 80 s). Start:
```
Klatsch-Modell (TensorFlow Lite Micro) unter Zephyr
Modell geladen, Arena 12308 Bytes, Eingabe 1024 Bytes
mic: PLL0 24571429 Hz, HF1 24571429 Hz
Mikrofon laeuft: 16 kHz mono, 16-ms-Rahmen, 32 Baender
```
Dieselbe Arena-Größe wie unter Arduino (12.484) bestätigt, dass dasselbe Modell mit denselben Kerneln läuft. Klatschtest steht aus (Marco im Zug, Grundrauschen um 950; der Auslöser verlangt das Sechsfache, in 30 s keine Inferenz).

## 71. Gemeinsame Firmware "house": Klatschen und Bluetooth parallel

`zephyr/app/house` vereint `bt_page_presence` und `clap_model` in einer Anwendung mit zwei Threads:

| Thread | Prioritaet | Aufgabe |
|---|---|---|
| main (Zephyr-Hauptthread) | 7 (`CONFIG_MAIN_THREAD_PRIORITY`) | Bluetooth: Firmware-Download, HCI Reset, alle 15 s ein Remote Name Request an das Handy, Ausgabe `P,`/`PAGE`/`PRAESENZ`/`CLIENT` |
| audio (eigener Stack 16 KB) | 5 (hoeher) | Mikrofon alle 2 ms lesen, Merkmale, bei Einsatz das Modell, Ausgabe `L,`/`MODELL`/`KLATSCH` |

Der Audio-Thread liegt ueber dem Bluetooth-Thread, weil das Mikrofon-FIFO nur 256 Woerter (16 ms) fasst; waehrend des 20-sekuendigen Firmware-Downloads und der Paging-Wartezeiten darf es nicht ueberlaufen. Die Hauslogik (`main.c`) ist der einzige Ort, der die LEDs anfasst, geschuetzt durch einen Mutex:

- Klatschen schaltet das Licht (LED1) um, Zeile `LICHT an|aus  (Klatschen)`.
- Wird das Handy unerreichbar und das Licht ist an, geht es aus: `LICHT aus  (Handy weg)`. Kommt das Handy zurueck, bleibt das Licht aus, bis wieder geklatscht wird (kein automatisches Einschalten, das waere beim Heimkommen am Tag laestig).
- LED2 zeigt die Anwesenheit.

Die Quellen sind unveraendert aus den beiden Einzelanwendungen uebernommen; aus `main()` wurde jeweils `bt_presence_thread()` bzw. `clap_thread()`, die LED-Aufrufe wurden durch `house_set_presence()` und `house_toggle_light()` ersetzt. Zwei Stolperer beim Zusammenfuehren: die Skript-Ersetzung von `return 0;` traf auch das Ende des Adress-Parsers (Folge: "Adresse ungueltig"), und `#include "house.h"` landete unter der ersten Verwendung. Beides in wenigen Minuten behoben.

**Ergebnis** (Build 148.200 Bytes Flash, 118.364 Bytes RAM): in einem 90-s-Mitschnitt 821 `L,`-Zeilen (nominal 10 pro Sekunde, kein Aussetzer waehrend des Firmware-Downloads), 6 erfolgreiche `PAGE ok ... Name: Marcos-Handy` und durchgehend `P,-50,1`. Beide Funktionen laufen also gleichzeitig, ohne dass eine die andere stoert. Zusaetzlich wiederholt die Firmware die Zeile `AP ssid=BT-Paging ...` jede Minute (auch in `bt_page_presence`), damit ein spaeter gestarteter Viewer den Modus erkennt.

Offen: Klatschtest mit dem Modell unter Zephyr (ruhige Umgebung), Test Bluetooth aus/an mit der gemeinsamen Firmware.

## 72. Viewer: Funktionsmenue und Haus-Ansicht

`tools/live_viewer.py` bekam links oben ein Burger-Menue (☰):

- **Automatisch** (Standard): Die Ansicht folgt den Daten. Kommen innerhalb von 30 s sowohl Mikrofon- (`L,`) als auch Bluetooth-Zeilen (`P,`), ist es die gemeinsame Firmware, Ansicht "Haus"; sonst gilt die Funktion, von der zuletzt Daten kamen.
- **Gesten**, **Klatschen / Schnipsen**, **Anwesenheit per Bluetooth**, **Haus**: Ansicht fest einstellen, unabhaengig davon, was gerade ankommt. Die Wahl bleibt im Browser gespeichert (localStorage), `?mode=gesture|clap|presence|house|auto` in der URL setzt sie vorab.
- Gruener Punkt je Eintrag: von dieser Funktion kamen in den letzten 5 s Daten.
- Ausgegraut, "noch ohne Firmware": Radar (BGT60TR13C), Luftdruck/Hoehe (DPS368), Magnetfeld (BMM350). Das beantwortet die Frage "was gibt es noch": die drei Sensoren sind auf dem Board, haben aber noch keine Anwendung.

Ansicht "Haus": Kachel "Licht im Haus" mit Lichtzustand (aus `LICHT`-Zeilen), Handy anwesend/abwesend (LED2), dazu die Pegelkurve, die Erreichbarkeitsleiste und der Klatsch-Streifen. Fuer die Paging-Variante zeigt die Bluetooth-Kachel kein dBm mehr (die Firmware liefert keinen echten RSSI, nur erreichbar/nicht erreichbar), sondern nur das blaue Band und die Wechsel. Der Anwesenheitszustand wird auch aus den `P,`-Zeilen abgeleitet, damit ein Viewer, der nach dem Board gestartet wurde, nicht auf die naechste `PRAESENZ`-Meldung warten muss.

Getestet mit Playwright gegen das laufende Board: Automatik springt nach etwa 15 s auf "Haus", Handy anwesend, LED2 an; Menue oeffnen, "Klatschen" waehlen, Ansicht wechselt und bleibt; "Automatisch" stellt zurueck. Keine Fehler in der Browser-Konsole.

## 73. Doppelklatschen: zweimal klatschen schaltet, einmal nicht

Marcos Wunsch (2026-10-09): Der Schalter soll erst bei zwei Klatschern umgelegt werden, damit sich Absicht von Zufall (Tuer, Gegenstand faellt, einzelner Knall) besser trennen laesst. Umgesetzt in `zephyr/app/house/src/clap.cpp`.

**Zwei Aenderungen am Ablauf:**
1. **Eigener Modell-Thread.** Bisher lief die Inferenz (53 ms) im Audio-Thread; so lange las niemand das Mikrofon-FIFO (256 Woerter = 16 ms), etwa 37 ms Audio gingen je Inferenz verloren. Bei einem Doppelklatscher koennte der zweite Klatscher genau in diese Luecke fallen. Jetzt quantisiert der Audio-Thread (Prioritaet 5) das fertige 32x32-Fenster und legt es in eine Message-Queue (4 Plaetze je 1.040 Bytes); der Modell-Thread (Prioritaet 6) rechnet, der Audio-Thread liest derweil ununterbrochen weiter.
2. **Ueberlappende Kandidaten.** Bisher wurde waehrend der 24 Rahmen Wartezeit kein neuer Einsatz angenommen. Jetzt duerfen bis zu vier Kandidaten gleichzeitig laufen (Mindestabstand `ONSET_GAP_MS` 100 ms, damit das Abklingen desselben Klatschers nicht als neuer Einsatz zaehlt). Der Ringpuffer mit 32 Rahmen reicht, weil jedes Fenster genau in dem Moment quantisiert wird, in dem sein letzter Rahmen eintrifft.

**Logik im Modell-Thread:** Jeder vom Modell bestaetigte Klatscher (p_clap >= 0,35) ist entweder der erste (merken, `KLATSCH #n  einzel  Konfidenz 0.72`) oder, wenn sein Einsatz 120 bis 800 ms (`DOUBLE_MIN_MS`, `DOUBLE_MAX_MS`) nach dem gemerkten liegt, der zweite: Licht umschalten, `KLATSCH #n  doppel  Abstand 310 ms  Konfidenz 0.80  LED1 an`. Kommt kein zweiter, verfaellt der erste nach 800 ms plus Inferenzlatenz: `VERWORFEN einzel  Grund: kein zweiter Klatscher innerhalb 800 ms`. Gerechnet wird mit den Einsatzzeiten aus dem Audio-Thread, nicht mit dem Zeitpunkt der Inferenz. Der Viewer zeigt den ersten Klatscher als halbhohen Strich und meldet "1. Klatscher erkannt, warte auf den zweiten".

**Offene Frage vorab geklaert: stoert der zweite Klatscher das Fenster des ersten?** Das Modell wurde nur mit Einzelklatschern trainiert; das Fenster des ersten Klatschers (128 ms davor, 384 ms danach) enthaelt bei einem Doppelklatscher auch den zweiten. `ml/clap/simulate_double.py` mischt aus je zwei von Marcos Aufnahmen Doppelklatscher mit 150 bis 650 ms Abstand und fragt das int8-Modell fuer beide Fenster (11 Paare, Mittel p_clap Fenster 1 / Fenster 2):

| Abstand | 150 ms | 200 ms | 300 ms | 400 ms | 500 ms | 650 ms |
|---|---|---|---|---|---|---|
| p_clap Fenster 1 / 2 | 0,98 / 0,89 | 0,88 / 0,88 | 0,84 / 0,85 | 0,88 / 0,88 | 0,87 / 0,92 | 0,87 / 0,86 |
| beide ueber 0,35 | 91 % | 82 % | 91 % | 82 % | 82 % | 82 % |

Das Urteil ueber Fenster 1 aendert sich durch den zweiten Klatscher praktisch nicht (das Modell schaut auf die Einsatzregion). Die Ausfaelle stammen von zwei einzeln schwachen Aufnahmen (eine hat allein p_clap 0,03), nicht von der Ueberlappung. Doppel-Schnipser: p_clap 0,00 bis 0,04 in allen Faellen. Die strenge Regel "beide Fenster muessen Klatscher sein" ist also tragbar; sie multipliziert allerdings die Trefferquote je Klatscher (bei 90 % je Klatscher etwa 81 % je Doppelklatscher). Sollte das im Alltag zu oft daneben gehen, waere die mildere Variante: ein bestaetigter Klatscher plus ein kraeftiger Einsatz. Firmware 149.520 Bytes Flash, 131.228 Bytes RAM; Start und beide Threads im Mitschnitt unauffaellig, Klatschtest steht weiter aus.
