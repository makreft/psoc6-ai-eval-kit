# Trainingsdaten

`audio/`: Aufnahmen vom Mikrofon des CY8CKIT-062S2-AI (16 kHz, Mono, 16 Bit), erzeugt mit `tools/record_audio.py` bei laufendem Sketch `04-audio-stream`.

```
audio/clap/        Klatschen, 1,5-s-Clips, Ereignis irgendwo im Clip
audio/snap/        Fingerschnipsen, 1,5-s-Clips
audio/other/       einzelne Stoergeraeusche (Klopfen, Schluessel, Husten, Rufen ...), 1,5-s-Clips
audio/background/  Hintergrund am Stueck (Raum, Reden, Tippen, Laerm), 60-s-Dateien
audio/index.csv    eine Zeile je Datei: Klasse, Ansage, Dauer, Spitze, RMS, verlorene Rahmen, Zeit
```

Aufnehmen (Board an J1, kein anderes Programm am Port):
```
tools/record_audio.py session                 # Klatschen, Schnipsen, Stoergeraeusche im Wechsel, 40 je Klasse
tools/record_audio.py background --seconds 60 # mehrmals, in ruhiger und in lauter Umgebung
tools/record_audio.py monitor                 # Pegeltest
```
Weitere Sitzungen haengen einfach an (fortlaufende Nummern, Index wird ergaenzt).
