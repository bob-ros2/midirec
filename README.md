# MidiRec - Console MIDI Recorder

Ein leistungsstarker, minimalistischer MIDI-Recorder für die Konsole, entwickelt in C++.
Ideal für das schnelle Festhalten von musikalischen Ideen ohne DAW-Overhead.

## Features

- **Auto-Detect**: Verwendet automatisch das erste verfügbare MIDI-Gerät.
- **Smart Recording**: Startet die Aufnahme automatisch beim ersten Tastendruck (Note-On).
- **Auto-Save**: Speichert die Aufnahme nach einer einstellbaren Inaktivitätszeit (Standard 10s) und wartet sofort auf das nächste Signal.
- **Premium Design**: Übersichtliche, farbige Konsolenausgabe für Statusmeldungen.
- **Cross-Platform**: Kompilierbar unter Linux (Ubuntu) und Windows (MSVC).

## Voraussetzungen

### Linux (Ubuntu/Debian)
```bash
sudo apt-get update
sudo apt-get install build-essential cmake libasound2-dev libjack-jackd2-dev pkg-config
```

### Windows
- Visual Studio 2019 oder neuer (C++ Desktop Entwicklung)
- CMake

## Kompilieren

Das Projekt verwendet `FetchContent`, um Abhängigkeiten (`RtMidi` und `MidiFile`) automatisch herunterzuladen.

```bash
mkdir build
cd build
cmake ..
cmake --build .
```

## Benutzung

Das Programm im `bin` Verzeichnis ausführen:

```bash
./bin/midirec [Optionen]
```

### Optionen
- `-l`, `--list`: Zeigt alle verfügbaren MIDI-Eingabegeräte an.
- `-i`, `--index <id>`: Wählt ein spezifisches Gerät über den Index aus.
- `-t`, `--timeout <s>`: Setzt die Inaktivitätszeit in Sekunden (Standard: 10).
- `-h`, `--help`: Zeigt die Hilfe an.

### Beispiel
Wartet auf Signale vom Gerät 1 und speichert bei 5 Sekunden Inaktivität:
```bash
./bin/midirec -i 1 -t 5
```

## Dateiname-Format
Aufnahmen werden im aktuellen Verzeichnis mit folgendem Format gespeichert:
`YYYYMMDD_HHMMSS_record.mid`
