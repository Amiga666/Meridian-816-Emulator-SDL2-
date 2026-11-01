# MERIDIAN 816 – Emulator (Stufe 1, Version 0.8.1)

Emulator für den MiSTer-Core [MERIDIAN_MiSTer](https://github.com/Retro-Modern/MERIDIAN_MiSTer),
geschrieben in C mit SDL2. Läuft auf macOS, Linux und Windows (MSYS2).
Lizenz: GPL-3.0-or-later (wie der Core). Die ROMs in `roms/` sind aus `rom/*.hex` des Core-Repos (Stand 30.10.2026, Core 20261030)
umgewandelt (je 16 KB); BASIC stammt von Microsoft (MIT-Lizenz, siehe Core-Repo). `tools/sidkern/` und `tools/sidton/sidton.cpp` sind GPL-3.0-or-later,
`tools/sidton/resid/` ist reSID von Dag Lem (GPL-2.0-or-later, Lizenztext und Herkunft dort in `COPYING` und `QUELLE.txt`).

## Bauen und starten

    macOS:  brew install sdl2         Linux: sudo apt install libsdl2-dev
    make
    ./meridian                        # Optionen: --pal  --no-sdram  --scale N  --fullscreen  --roms DIR  --module DATEI  --version

`make` baut außerdem `dienst/bildwandler` (JPG/PNG/GIF für PICTURES, PAINT, WRITE und den Druck; Quelle in `tools/bildwandler/`, braucht nur einen C-Compiler)
sowie `dienst/sidkern` und `dienst/sidton` für SID JAM (Quellen in `tools/sidkern/` und `tools/sidton/`, auch nur ein C-/C++-Compiler).

Ohne SDL gibt es einen Testlauf: `make headless && ./meridian-headless roms 300 bild.ppm`

Hotkeys (Cmd-Taste auf dem Mac, Windows-Taste am PC): **Cmd+C** Mauszeiger des Betriebssystems zeigen/verbergen (er ist über dem Fenster verborgen, weil der MERIDIAN seinen eigenen zeichnet; `--show-cursor` lässt ihn sichtbar), **Cmd+M** Ton aus/an, **Cmd+R** Reset, **Cmd+P** PAL/NTSC,
**Cmd+F** Vollbild, **Cmd+S** Screenshot (`meridian-screenshot.bmp`), **Cmd+D** Ordner von Laufwerk 3 im Finder zeigen (dort liegen auch die gedruckten PDFs), **Cmd+Q** Beenden.
Die Tasten werden als PS/2-Scancodes (Satz 2) nach Tastenposition weitergereicht, das Kern-ROM
macht daraus das deutsche Layout. Beide Alt/Option-Tasten wirken als AltGr (@, {, [ …).

## Disketten

    ./meridian --disk1 MERIDIAN.DSK --disk2 BLANK1.DSK

Oder ein Image per Drag & Drop auf das Fenster ziehen (Laufwerk 1, mit gedrückter Shift-Taste Laufwerk 2).
**Cmd+1** / **Cmd+2** werfen Laufwerk 1 / 2 aus. Die Namen stehen im Fenstertitel.
Fertige Images liegen im Core-Repo in `extras/MERIDIAN_disks.zip`, eigene legt `tools/disk.py` an
(`disk.py neu SPIELE.DSK --mb 8`, `disk.py rein SPIELE.DSK programm.mer`). In BASIC: `DIR`, `LOAD "NAME"`,
`SAVE "NAME",2`. Änderungen gehen sofort ins Image – am besten mit Kopien arbeiten.
Laufwerk 0 (Speicher eines Moduls) lässt sich mit `--disk0` einhängen, Laufwerk 3 (Ordner) gibt es noch nicht.

## Programme und Module

    ./meridian --run DATEN.MER --run PROGRAMM.MER      # wie „Load program“ im MiSTer-Menü, der Reihe nach
    ./meridian --module counter.mod                    # Modul stecken, Neustart; --no-module-start = „Cartridge start: Off“

Oder Dateien auf das Fenster ziehen: **`.mer`** lädt ein Programm (mit Autostart läuft es sofort an, ohne
meldet die Konsole „LOADED …“), **`.mod`** steckt ein Modul, alles andere kommt in Laufwerk 1 (mit Shift Laufwerk 2).
**Mehrere Dateien gleichzeitig markieren und zusammen auf das Fenster ziehen.** Der Emulator lädt sie dann wie `senden.py`
in einem Rutsch und in der richtigen Reihenfolge: erst Module und Images, dann Daten, zuletzt das Programm mit Autostart.
Einzeln nacheinander geht es nicht gut: `ebene_a.mer` überschreibt mit seinen Bilddaten die Tastentabelle des Kerns
(`$03:3E00`), der Kern hält dann F1–F6 für gedrückt und öffnet die Werkstatt, die dabei `$03:4000–$03:7FFF` löscht. Das
ist am echten Gerät genauso; dort schickt `senden.py` die Dateien nur schnell genug hintereinander. Ist es doch passiert:
Cmd+Shift+R (Kaltstart) und die Dateien gemeinsam neu laden. Die nötigen Daten nennt jedes Programm selbst
Programme, die Daten brauchen, nennen die Reihenfolge selbst (z. B. KOBOLDE: `ebene_a.mer`, `ebene_b.mer` und `kobolde.mer`
aus `programme/` und `programme/daten/` im Core-Repo; die Testdaten für LADETEST erzeugt `programme/ladetest.py`).
Der Speicherstand eines Moduls liegt als `NAME.sav` neben der `.mod`-Datei (Laufwerk 0, wächst beim Schreiben).
**Rückkehr aus einem Modul** (z. B. Esc im TIEFGANG-Titel): Das Kern-ROM vom 25.10.2026 setzt dabei die Anzeige zurück
(`anzeige_zuruecksetzen`), Interrupt-Haken und Klang bleiben. Mit älteren Kern-ROMs fehlte das, das Bild des Moduls blieb stehen
und BASIC lief unsichtbar weiter. Für solche ROMs schiebt der Emulator den Aufruf selbst ein (nur wenn die Kern-Bytes bei `$DDBB`
genau zum alten ROM passen; beim mitgelieferten neuen ROM bleibt das ausgeschaltet; `--no-module-fix` schaltet es ganz ab).
**Cmd+E** wirft das Modul aus, **Cmd+R** ist ein Reset (RAM bleibt wie am echten Gerät), **Cmd+Shift+R** ein Kaltstart (RAM gelöscht, Modul bleibt gesteckt).

## Stand

| Baustein | Stand |
|---|---|
| 65C816 (emulierend und nativ, BCD, Block-Moves, WAI/STP, IRQ/NMI) | fertig, bootet Kern und BASIC |
| Speicherkarte: Chip-RAM, SPIEGEL, Zusatzspeicher 15,7 MB, DRAHT-Bank, ROMs, Modul-ROM | fertig |
| PINSEL: 2 Ebenen, Text 40/80, Kacheln, Bitmap 16/256, Scrolling, Palette, Raster-/Bild-IRQ | fertig; eine Zeile wird im Voraus gezeichnet wie in der Hardware, Palettenwechsel wirken punktgenau |
| PFORTE: Tastatur, Timer A/B, µs-Uhr, Kalenderuhr, Maus | fertig; Joysticks sind verdrahtet, aber noch keine Eingabequelle |
| KRAN (Blitter) | fertig, aber ohne Zeitverbrauch (Auftrag läuft sofort); Bit 2 „rückwärts“ als Zeilenreihenfolge gedeutet – gegen den Verilog-Code prüfen |
| TRUHE (Laufwerke 1 und 2, `.DSK`/`.IMG`/`.HDF`, FAT16, lesen und schreiben) | fertig, getestet mit DIR, LOAD, SAVE; schreibgeschützte Dateien werden als „nur lesen“ eingehängt |
| KOBOLD (32 Sprites, Spiegeln, doppelt groß, hinter Ebene B, Kollisionen, Kontur) | fertig, nach `kobold.sv` |
| LOTSE (Copper: WARTE, SETZE, SPRUNG, SIGNAL, FARBE) | fertig; Befehle laufen ohne Zeitverbrauch, Wartepunkte auf ca. 4 Punkte genau |
| PINSEL-Look: Glanz (SHINE), Leuchten (GLOW), Tusche (INK: Kontur und Schlagschatten) | fertig, nach `pinsel.sv` |
| ORGEL (Ton): 4 Synthesestimmen (SID-Wellenformen, Sync, Ring, Hüllkurve), Filter, 8 Samplekanäle mit Crusher/Verzerrung, Echo, Stereo | fertig, rechnet im 1-MHz-Takt nach `orgel.sv`; Ausgabe 48 kHz über SDL |
| BOTE: `.MER`-Programme laden (Chip-RAM und Zusatzspeicher, Autostart per NMI), Module (`.MOD`, schreibgeschützt ab `$40:0000`, überstehen Resets), Speicherstand `.sav` als Laufwerk 0 | fertig, getestet mit MONTEST, KRANTEST, DOSTEST, LADETEST, KOBOLDE, GRAFIK, BALLETT, COUNTER |
| DRAHT: Fenster Bank `$FD` als gemeinsame Datei, daran der echte Netzdienst 2.0 `dienst/draht.py` (Python 3): **Laufwerk 3 = Ordner**, `NET`-Befehle | fertig für macOS und Linux; Windows noch nicht (Bank `$FD` bleibt dort ein einfacher Speicher) |

Offen: Windows-Start des Netzdienstes, `FETCH` (braucht das BOTE-Postfach, meldet im Simulationsmodus selbst „only on the MiSTer“).

## Laufwerk 3 und Netz (DRAHT)

Der Emulator startet beim Start `python3 dienst/draht.py` (der unveränderte Netzdienst aus dem Core) und gibt ihm das DRAHT-Fenster
(Bank `$FD`) als gemeinsame Datei. Damit gilt wie am MiSTer:

    ./meridian                      # Ordner sdcard neben dem Programm, Laufwerk 3 = sdcard/files (wird angelegt)
    ./meridian --ordner ~/Spiele/sdcard

Was in `sdcard/files` liegt, sieht der MERIDIAN sofort (`DIR 3`, `LOAD "NAME",3`, `SAVE "NAME",3`), was er sichert, liegt dort
sofort für den Mac bereit. Lange Namen erscheinen als 8.3-Kürzel (`Space Debris.mod` → `SPACED~1.MOD`). Dazu die `NET`-Befehle
(`NET "HELP"`, `GET`, `CONNECT`, `LISTEN`, `SEND` … siehe BASIC.md); sie brauchen Internet. Voraussetzung ist Python 3
(gesucht wird `python3` im PATH, dann `/usr/bin`, Homebrew-Orte und `python`; `MERIDIAN_PYTHON=/pfad/python3` wählt eines; mindestens 3.8). Im Fenstertitel steht „D3: Ordner files“, wenn der Dienst läuft, sonst der Grund (kein Python, kein `draht.py`, Dienst beendet,
antwortet nicht), mit Einzelheiten und einem Auszug aus dem Protokoll in der Konsole. Protokoll des Dienstes: `$TMPDIR/meridian-draht.log`. `--no-dienst`
schaltet das alles ab (Laufwerk 3 meldet dann nach 10 s Fehler 4). Der Dienst endet mit dem Emulator, auch nach einem Absturz.

**Prüfen, ob der Dienst erreichbar ist:** `./meridian --dienst-test` schickt ohne Fenster ein `HELP` durch das DRAHT-Fenster und meldet
„Selbsttest OK“ oder den Grund. Dasselbe passiert eine Sekunde nach jedem normalen Start (Ergebnis im Titel: „D3: Ordner files“). Im BASIC-Prompt:

    NET "HELP":PRINT NET(0):PRINT NET$(0)
    NET "GET wttr.in/Berlin?format=3":PRINT NET(0):PRINT NET$(0):PRINT NET$(0)

`NET(0)` ist 0 oder der HTTP-Code (200), negativ bei Fehlern (−3 keine Verbindung). Jeder `NET`-Befehl steht mit Ergebnis im Protokoll des Dienstes
(`tail -f` auf die beim Start genannte Datei); steht dort nichts, ist die Anfrage gar nicht beim Dienst angekommen.
**HTTPS-Zertifikate (macOS):** Pythons von python.org und Homebrew kennen den Schlüsselbund des Systems nicht und scheitern bei
Wetter, Nachrichten und Radio mit `CERTIFICATE_VERIFY_FAILED`. Der Emulator gibt dem Dienst deshalb das Zertifikatspaket des Systems mit
(`/etc/ssl/cert.pem`, sonst Homebrew-Orte; `SSL_CERT_FILE=/pfad/cert.pem` setzt ein eigenes; `certifi` dient als Rückfall). Welches Paket gilt,
steht beim Start in der Konsole („DRAHT: HTTPS-Zertifikate: …“); hat Python gar keine, kommt dort ein Hinweis mit der Abhilfe
(`Install Certificates.command` im Python-Ordner oder `pip3 install certifi`).

**HORIZONT (Modul) und seine Apps:** Der Emulator bringt den Netzdienst 2.0 (HORIZONT 2.4) mit. Damit laufen
WEATHER (und das Wetter in der Menüleiste), NEWS, ATLAS (Browser-Seite und Downloads), TAKTSTOCK (Netz-Jam), RADIO (auch Mitschnitt „REC“, 32 kHz/12 Bit), SID JAM (siehe unten), PICTURES und PAINT mit JPG/PNG/GIF und das **Drucken** (siehe unten). Die Browser-Seite ist nur auf
diesem Rechner erreichbar: `http://localhost:8816/` (`--ablage-port N`, 0 = aus; `MERIDIAN_ABLAGE_LAN=1` öffnet sie wie am MiSTer fürs Heimnetz).
RADIO braucht nichts zu installieren: `make` baut `dienst/bin/mpg123`, einen winzigen MP3-Dekoder (`tools/mpg123mini.c` mit
[minimp3](https://github.com/lieff/minimp3), CC0), den der Dienst wie `mpg123` aufruft. Ist `ffmpeg` installiert, nimmt das Radiomodul auf dem Mac lieber das
(es kann mehr als MP3). Der Dekoder versteht MP3 (MPEG 1/2/2.5, Layer 3) in jeder üblichen Abtastrate, Mono und Stereo, kommt mit ID3-Kopf und
mitten im Strom einsteigenden Sendern zurecht und rechnet auf 22 050 Hz um; AAC-Sender spielt er nicht. **SID JAM läuft**: `make` baut
`dienst/sidkern` und `dienst/sidton` aus `tools/sidkern/` und `tools/sidton/` (im Netzdienst-Paket liegen sie nur als ARM-Programme für den
MiSTer bei). Es fehlt nur noch die HVSC, siehe unten. Die Apps holen Wetter, Nachrichten und Sender selbst aus dem Netz (siehe `dienst/LIESMICH.txt`
und die Hinweise zum Datenschutz im Netzdienst-Paket); sie fragen erst, wenn du die App öffnest. Alle `NET`-Befehle (`GET`, `CONNECT`, `LISTEN`, `SEND`, `FILES`, `DISK` …) und Laufwerk 3 gehen ebenfalls.
Er darf ins Netz: nur auf Befehle aus BASIC hin, ohne die startet nichts.

**Drucken (HORIZONT 2.4):** NOTES, PAINT und die KONTOR-Apps (WRITE, CALC, FILE, DRAW) kennen „Print to PDF …“ und „Print“.
„Print to PDF …“ legt eine A4-Datei `NAME.PDF` auf Laufwerk 3 ab (`sdcard/files`, Cmd+D zeigt den Ordner). „Print“ schickt das PDF per IPP an einen
Netzwerkdrucker, der PDF annimmt (IPP Everywhere/AirPrint): erste Zeile von `sdcard/files/PRINTER.TXT` (`192.168.1.20`, ein Name oder `ipp://…/ipp/print`),
ohne die Datei sucht der Dienst per Bonjour. Das Drucken selbst macht der unveränderte `dienst/druck_dienst.py` des Netzdienstes (reines Python).
Beispiele für FILE und DRAW (`C64GAMES.FIL`, `MERIDIAN.DRW`) legt der Emulator beim Start nach `sdcard/files`, wenn sie dort fehlen.

## Ordner `sdcard` (entspricht `games/MERIDIAN` am MiSTer)

    sdcard/files/                     Laufwerk 3 (DIR 3, LOAD "X",3, SAVE "X",3); hier liegt auch RADIO.TXT (eigene Sender: Name|http://adresse)
    sdcard/wetter.json                Ort und Einstellungen von WEATHER (entsteht beim ersten Öffnen der App)
    sdcard/HVSC/hvsc_index.tsv.gz     Index für SID JAM allein (HVSC #85); legt der Emulator selbst dorthin, wenn er fehlt, und überschreibt nie
    sdcard/HVSC/hvsc.zip              die HVSC selbst: Ordner `C64Music` zippen (`zip -r hvsc.zip C64Music`, Einträge müssen mit `C64Music/` beginnen,
                                      nur Verfahren „gespeichert“ oder „deflate“); `hvsc.zip.liste` entsteht beim ersten Start daneben

Mit einer anderen HVSC-Version den Index selbst bauen: `python3 dienst/hvsc_index.py bauen C64Music sdcard/HVSC/hvsc_index.tsv.gz`.

## Ton

`--no-audio` schaltet ihn ab. Der 1-MHz-Mischer der ORGEL wird für die Ausgabe auf 48 kHz gemittelt (einfacher Kastenfilter);
Samples und Echo lesen direkt aus dem Speicher, das DMA-Timing der Hardware wird nicht nachgebildet (der Klang ändert sich dadurch nicht).
Echo und Samples ab Bank `$04` brauchen den Zusatzspeicher (nicht mit `--no-sdram`). Getestet per Aufnahme und Spektrum:
`SOUND 1,440` ergibt 440 Hz, BALLETT, ORGEL-Demo, FILTERTEST und das Zusammenspiel-Jingle liefern plausible Pegel in Stereo.
Zum Nachmessen ohne Fenster: `make headless`, die Maschine mit `audio_on = true` laufen lassen und `m_audio_read()` abholen. Die Zeitbasis ist auf 8 MHz / 24 MHz-Raster ausgelegt (509 CPU-Zyklen je Zeile).
