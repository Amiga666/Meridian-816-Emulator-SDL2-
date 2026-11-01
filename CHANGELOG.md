# Änderungen

## 0.8.1
Nur SID JAM.
- **SID JAM läuft.** `make` baut jetzt auch `dienst/sidkern` und `dienst/sidton`; der Netzdienst findet beide neben `sidjam_dienst.py`.
  Quellen vom Entwickler des Netzdienstes: `tools/sidkern/` (eigener Code, GPL-3.0-or-later) und `tools/sidton/` (`sidton.cpp` GPL-3.0-or-later,
  `resid/` = reSID von Dag Lem, GPL-2.0-or-later, unverändert). Beide brauchen nur einen Compiler und libc/libstdc++, keine Bibliothek. Die ARM-Programme
  des Pakets laufen auf dem Mac nicht, deshalb der eigene Bau — wie bei `bildwandler`.
- Hier geprüft (Linux x86_64, gleiche Quellen wie am Mac; auf macOS selbst noch nicht gebaut):
  - `make` baut beide ohne Warnung. `sidkern --cputest` besteht Klaus Dormanns 6502-Funktionstest ($3469, 30 646 177 Befehle, 96 241 367 Zyklen).
  - Mit einer selbst gebauten SID-Datei (440 Hz, Gate-Wechsel alle 64 Bilder): Kopf, `--text`, `--ab` (Registerabbild), Fehlermeldungen
    („kein PSID/RSID“, „Datei zu kurz“, „Lied 2 gibt es nicht (1-1)“) und der Strom, den `SJELeser` und `orgelwandler` des Netzdienstes lesen.
  - `sidkern | sidton`: Spitzenfrequenz 440,0 Hz.
  - **SID JAM im Fenster von HORIZONT 2.4** (Maus und Tastatur simuliert, HVSC = Zip mit der Testdatei, Ton des Emulators aufgenommen): App öffnet,
    Suche und Wiedergabe laufen, Anzeige „6581 PAL reSID“, Stimme 1 zeigt A-4, Fortschrittsbalken läuft. Ton im Klang ORIGINAL (reSID): 440 Hz mit
    den Oberwellen einer Rechteckschwingung (1320, 2200, 3080 Hz …); im Klang ORGEL: 440 Hz.
  - Nicht geprüft: echte HVSC-Stücke (nur eine Testdatei), Klang „TOGETHER“ (braucht den JAM-Server), Auto-DJ, Mehrfach-SID.
- Laut Entwickler gegen libsidplayfp verglichen (31 Stücke exakt oder fast, 2000 HVSC-Stücke zu 96 % exakt); das habe ich nicht nachgeprüft.
- Zwei Schutzzeilen für Windows (MSYS2), damit `make` dort nicht abbricht: `SIGPIPE` in `sidkern.c` und `M_PI` in `sidton.cpp` nur, wenn der Compiler sie kennt.
  Auf MSYS2 selbst nicht gebaut; der Netzdienst läuft unter Windows ohnehin noch nicht.
- Hinweis: Der Emulator taktet auf 50,3 bzw. 59,95 Bilder/s. Lässt man ihn schneller als Echtzeit rechnen (Testprogramme), hält SID JAM den Netzdienst
  für tot, weil dessen Herzschlag in Echtzeit läuft.

## 0.8.0
- Core 20261030: `kern.rom` und `werkstatt.rom` neu (Fix für den Fernstart unter HORIZONT). Die ROMs sind aus `rom/*.hex` des Core-Repos umgewandelt.
- Netzdienst 2.0 (Stand 31.10.2026) für HORIZONT 2.4: **Drucken** aus NOTES, PAINT und KONTOR (WRITE, CALC, FILE, DRAW) als A4-PDF auf Laufwerk 3 oder
  per IPP an einen Netzwerkdrucker (`druck_dienst.py`); Bilder (JPG/PNG/GIF) für PICTURES und PAINT (`bild_dienst.py`); RADIO 1.2 mit Mitschnitt und 12 Bit
  (`radio_dienst.py` 1.9). Alle Python-Dateien unverändert aus dem Paket.
- Neu: `make` baut `dienst/bildwandler` aus `tools/bildwandler/` (Quelle und stb_image aus dem Netzdienst-Paket) für den Mac/Linux; die ARM-Programme des Pakets laufen hier nicht.
- Neu: Beispiele `C64GAMES.FIL` und `MERIDIAN.DRW` (FILE, DRAW) kommen beim Start nach `sdcard/files`, wenn sie fehlen.
- Neu: Cmd+D zeigt den Ordner von Laufwerk 3 im Dateimanager.
- Geprüft mit `HORIZONT.MOD` 2.4 im Emulator: NOTES „Print to PDF …“ (Text stimmt) und „Print“ an einen Test-Drucker per IPP, PAINT „Print to PDF …“ (Bild); RADIO öffnet ohne Fehler.
- Unverändert: `sidkern` liegt nur als ARM-Programm vor, SID JAM läuft weiter nicht.

## 0.7.3
- Der HVSC-Index für SID JAM (`hvsc_index.tsv.gz`, HVSC #85) liegt jetzt bei und wird beim Start nach `sdcard/HVSC/` gelegt, falls dort
  keiner liegt (auch in einem schon vorhandenen `sdcard`); ein eigener Index wird nie überschrieben.
- Der Mauszeiger des Betriebssystems ist über dem Emulatorfenster verborgen (Cmd+C schaltet um, `--show-cursor` lässt ihn sichtbar).
  In Titelleiste und außerhalb des Fensters bleibt er sichtbar.
- Unverändert: `sidkern`/`sidton` (SID JAM läuft weiter nicht, Rückmeldung vom Entwickler abgewartet).

## 0.7.2
- Neu: eingebauter MP3-Dekoder für RADIO (`tools/mpg123mini.c` + minimp3, CC0). `make` baut ihn als `dienst/bin/mpg123`, der Dienst stellt den
  Ordner seinem Suchpfad voran; kein ffmpeg mehr nötig (ist es da, wird es weiter bevorzugt). Rechnet auf 22 050 Hz Stereo um.
- Getestet: MP3 mit 22,05/32/44,1/48 kHz und Mono (Frequenz und Kanaltrennung stimmen mit ffmpeg überein), langsamer Strom, ID3-Kopf, Müll
  vor dem Strom, Einstieg mitten im Strom, abgeschnittenes Ende, leere und zufällige Eingabe. Ende zu Ende: Radio-Anfrage an den Dienst →
  Zustand „spielt“, Ringpuffer im Fenster mit 440,1 Hz links und 659,5 Hz rechts.

## 0.7.1
- Fix: Wetter, Nachrichten (und Radio) scheiterten auf dem Mac mit `CERTIFICATE_VERIFY_FAILED … unable to get local issuer certificate`,
  weil Python dort ohne Nacharbeit keine Zertifikate hat. Der Emulator gibt dem Dienst jetzt das Zertifikatspaket des Systems mit
  (`SSL_CERT_FILE`, Rückfall `certifi`) und warnt beim Start, wenn Python gar keine Zertifikate hat.

## 0.7.0
- Neu: der komplette Netzdienst 1.7 (Wetter, Nachrichten, Radio, ATLAS-Ablage, TAKTSTOCK-Jam, SID-JAM-Teile) liegt in `dienst/` bei.
  Ohne diese Module bediente `draht.py` nur BASIC-`NET` und Laufwerk 3, die HORIZONT-Apps bekamen keine Antwort.
  Geprüft: WEATHER im HORIZONT-Modul schickt die Anfrage an den Dienst (im Protokoll steht der Versuch, Open-Meteo zu holen).
- Browser-Ablage (ATLAS): Standardport 8816, nur an 127.0.0.1 gebunden, Adresse „localhost“ (`--ablage-port`, `MERIDIAN_ABLAGE_LAN`).
- ORGEL liest Samples auch aus Bank `$FD` (DRAHT-Fenster), wie das DMA der Hardware. Gemessen: 220,8 Hz bei erwarteten 220,5 Hz.
- Der Dienst wird als Modul geladen (draht.py bleibt unverändert; Anpassungen stehen im Start-Skript des Emulators).
- Nicht möglich: SID JAM (braucht `sidkern`/`sidton` für macOS).

## 0.6.2
- Standardordner heißt jetzt `sdcard` (Laufwerk 3 = `sdcard/files`). Vorher `MERIDIAN`: Auf macOS ist das derselbe Name wie das Programm `meridian`
  (Groß-/Kleinschreibung zählt dort nicht), der Ordner ließ sich nicht anlegen. Bei so einer Kollision gibt es jetzt eine klare Meldung.
- Neu: Selbsttest des DRAHT-Kanals (`--dienst-test`, außerdem automatisch beim Start): eine Anfrage `HELP` geht durch das Fenster zum Dienst, die
  Antwort wird geprüft. Ergebnis im Fenstertitel und in der Konsole.
- Getestet: `NET "GET 127.0.0.1:8765/index.txt"` gegen einen lokalen Webserver (Status 200, Inhalt ankommen, Anfrage im Server-Protokoll).

## 0.6.1
- DRAHT robuster und sichtbarer: Der Ordner `MERIDIAN/files` wird vom Emulator selbst angelegt und liegt neben dem Programm (nicht mehr
  im aktuellen Arbeitsverzeichnis). Python 3.8+ wird gesucht (PATH, `/usr/bin`, Homebrew, `MERIDIAN_PYTHON`) und vorab geprüft.
- Nach dem Start prüft der Emulator, ob der Dienst lebt und sein Herzschlag läuft; Ergebnis im Fenstertitel („D3: …“), bei Fehlern
  Grund und Protokollauszug in der Konsole.

## 0.6.0
- Neu: DRAHT. Bank `$FD` ist eine gemeinsam genutzte Datei (mmap); der Emulator startet den echten Netzdienst `dienst/draht.py`
  (Python 3) und kann so **Laufwerk 3** (Ordner `MERIDIAN/files`) und die **NET-Befehle**. Wächter beendet den Dienst mit dem Emulator.
- Optionen `--ordner DIR`, `--no-dienst`; Umgebungsvariable `MERIDIAN_PYTHON`.
- Bank `$FD` ist jetzt auch ohne Zusatzspeicher (`--no-sdram`) vorhanden.
- Getestet: `DIR 3` (mit 8.3-Kürzel), `LOAD`/`SAVE` auf Laufwerk 3 (Datei byte-identisch auf dem Rechner), `NET "FILES"`, `NET "HELP"`.
  Regressionstests unverändert bestanden.

## 0.5.0
- Neu: ORGEL – Ton. Vier Synthesestimmen (Dreieck, Säge, Puls, Rauschen, kombinierbar, Sync/Ring, ADSR wie beim SID),
  Zustandsvariablenfilter, acht Samplekanäle (Chip-RAM und Zusatzspeicher, Schleife, Crusher, Verzerrung), Echo, Panorama und
  Mischpult nach `rtl/orgel.sv`; Ausgabe 48 kHz Stereo über SDL (Vorpuffer 100 ms). `SFX`, `SOUND`, `PLAY`, `SONG`, `SAMPLE` sind hörbar.
- `--no-audio`, Cmd+M (Ton aus/an), Testhilfe `MERIDIAN_AUDIO_STATS=1`.
- Getestet per Aufnahme: `SOUND 1,440` → 440 Hz; BALLETT-Musik, ORGEL-Demo, FILTERTEST und Zusammenspiel liefern Ton in Stereo.

## 0.4.3
- ROMs auf den Stand vom 25.10.2026 aktualisiert (Core 20261025): `kern`, `dos` und `werkstatt` neu, `basic` und `song` unverändert.
  Der Entwickler hat den Modul-Rücksprung im Kern selbst repariert (`jsr anzeige_zuruecksetzen` vor `_basic`).
- Der Ausgleich aus 0.4.2 greift nur noch bei dem alten Kern-ROM und ist beim neuen ROM automatisch aus. Geprüft: neues ROM mit
  TIEFGANG 1.1 und 1.2, altes ROM mit und ohne Ausgleich.
- Regressionstest mit den neuen ROMs: Boot, DIR, MONTEST (0 Fehler), KRANTEST, DOSTEST, LADETEST, COUNTER-Modul, Werkstatt.

## 0.4.2
- Fix: Nach Esc im TIEFGANG-Titel (und überhaupt bei jeder Rückkehr eines Moduls mit RTL) bleibt das Bild des Moduls stehen,
  obwohl BASIC läuft – Eingaben scheinen tot. Ursache: das Kern-ROM setzt die Anzeige auf diesem Weg nicht zurück. Der
  Emulator tut es jetzt vor dem BASIC-Start (`--no-module-fix` schaltet das ab).

## 0.4.1
- Fix: Dateien werden als Satz geladen (Drag & Drop mehrerer Dateien und mehrere `--run`): Module/Images, dann Datendateien,
  zuletzt Programme mit Autostart, alles im selben Augenblick. Vorher öffnete schon `ebene_a.mer` allein die Werkstatt
  (überschriebene Tastentabelle des Kerns) und zerstörte damit die Daten für KOBOLDE und GRAFIK.
- Testhilfe: Umgebungsvariable `MERIDIAN_SHOT_AFTER=N` speichert nach N Bildern einen Screenshot und beendet.

## 0.4.0
- Neu: BOTE – `.MER`-Programme laden (Chip-RAM und Zusatzspeicher, Autostart per NMI wie „REMOTE START“), `.MOD`-Module
  (schreibgeschützt ab `$40:0000`, überstehen Reset und Kaltstart, Auswerfen mit Cmd+E), Speicherstand als `.sav` = Laufwerk 0.
- Neu: `--run` (mehrfach), `--module`, `--no-module-start`; Drag & Drop erkennt `.mer`, `.mod` und Images.
- Reset ist jetzt wie am Gerät (RAM bleibt), Cmd+Shift+R ist der Kaltstart.
- TRUHE: Speicherstand-Laufwerk wächst beim Schreiben, ungeschriebene Blöcke lesen als Nullen.
- Getestet: MONTEST (2×256 Opcodes, 0 Fehler), KRANTEST, DOSTEST, LADETEST (inkl. Grenze Chip-RAM/Zusatzspeicher),
  KOBOLDE, GRAFIK, BALLETT, COUNTER-Modul.

## 0.3.0
- Neu: KOBOLD – 32 Sprites mit Spiegeln, doppelter Größe, „hinter Ebene B“, Kollisionsflags (`HIT`), Kontur.
- Neu: LOTSE (Copper) – `GRADIENT` und eigene Copper-Listen funktionieren.
- Neu: PINSEL-Look – Glanz (`SHINE`), Leuchten (`GLOW`), Tusche (`INK`: Kontur, Schlagschatten).
- PINSEL zeichnet jetzt wie die Hardware eine Zeile im Voraus; Palettenwechsel (CPU oder Copper) wirken punktgenau in der Zeile.
- Interrupt „Bildende“ wird nur noch gemeldet, wenn er erlaubt ist (wie im Verilog-Code).
- Getestet mit STARFALL, REGENBOG, ABEND, GLOWMINE (Titel und Spiel) und einem eigenen Sprite/INK/GLOW-Test.

## 0.2.0
- Neu: TRUHE – Laufwerke 0–2 mit Blocklesen/-schreiben (bis 16 Blöcke je Auftrag), EINGELEGT/WECHSEL-Merker,
  schreibgeschützte Images. `DIR`, `LOAD`, `SAVE` laufen gegen echte `.DSK`-Images.
- Neu: `--disk0/1/2`, Drag & Drop von Images aufs Fenster, Cmd+1/Cmd+2 zum Auswerfen, Laufwerke im Fenstertitel.

## 0.1.1
- Fix: Zifferntasten 1–9 lieferten die jeweils nächste Ziffer (Scancode-Tabelle um eins verschoben).
- Versionsnummer im Fenstertitel, bei `--version` und im Namen des ZIP.

## 0.1.0
- Erste Fassung: 65C816, Speicherkarte, PINSEL, PFORTE, KRAN, SDL2-Frontend.
