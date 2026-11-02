# MERIDIAN 816 – Emulator (Stufe 1 / Stage 1, 0.8.3)

**[Deutsch](#deutsch) · [English](#english)**

Emulator für den MiSTer-Core [MERIDIAN_MiSTer](https://github.com/Retro-Modern/MERIDIAN_MiSTer), geschrieben in C mit SDL2 – läuft auf macOS, Linux und Windows (MSYS2).
Emulator for the MiSTer core [MERIDIAN_MiSTer](https://github.com/Retro-Modern/MERIDIAN_MiSTer), written in C with SDL2 – runs on macOS, Linux and Windows (MSYS2).

Lizenz / License: GPL-3.0-or-later (wie der Core / like the core). Fremdcode und Quellen / third-party code and sources: [Quellen und Lizenzen](#quellen-und-lizenzen) · [Sources and licenses](#sources-and-licenses)

---

# Deutsch

Die ROMs in `roms/` sind aus `rom/*.hex` des Core-Repos umgewandelt (Stand 30.10.2026, Core 20261030, je 16 KB).

## Bauen und starten

    macOS:  brew install sdl2         Linux: sudo apt install libsdl2-dev
    make
    ./meridian                        # Optionen: --pal  --no-sdram  --scale N  --fullscreen  --roms DIR  --module DATEI  --version

`make` baut außerdem `dienst/bildwandler` (JPG/PNG/GIF für PICTURES, PAINT, WRITE und den Druck; Quelle in `tools/bildwandler/`, braucht nur einen C-Compiler)
sowie `dienst/sidkern` und `dienst/sidton` für SID JAM (Quellen in `tools/sidkern/` und `tools/sidton/`, auch nur ein C-/C++-Compiler) und den kleinen MP3-Dekoder
`dienst/bin/mpg123` für RADIO.

Wird derselbe Ordner auf mehreren Systemen gebaut (Mac und Linux, geteilter Ordner), baut `make` beim Wechsel alles neu (Datei `.plattform`).
Schlägt etwas fehl: `make clean && make`.

Ohne SDL gibt es einen Testlauf: `make headless && ./meridian-headless roms 300 bild.ppm`

Hotkeys (Cmd-Taste auf dem Mac, Windows-Taste am PC): **Cmd+C** Mauszeiger des Betriebssystems zeigen/verbergen (er ist über dem Fenster verborgen, weil der MERIDIAN seinen eigenen zeichnet; `--show-cursor` lässt ihn sichtbar), **Cmd+M** Ton aus/an, **Cmd+R** Reset, **Cmd+P** PAL/NTSC,
**Cmd+F** Vollbild, **Cmd+S** Screenshot (`meridian-screenshot.bmp`), **Cmd+D** Ordner von Laufwerk 3 im Dateimanager zeigen (dort liegen auch die gedruckten PDFs), **Cmd+Q** Beenden.
Die Tasten werden als PS/2-Scancodes (Satz 2) nach Tastenposition weitergereicht, das Kern-ROM
macht daraus das deutsche Layout. Beide Alt/Option-Tasten wirken als AltGr (@, {, [ …).

## Disketten

    ./meridian --disk1 MERIDIAN.DSK --disk2 BLANK1.DSK

Oder ein Image per Drag & Drop auf das Fenster ziehen (Laufwerk 1, mit gedrückter Shift-Taste Laufwerk 2).
**Cmd+1** / **Cmd+2** werfen Laufwerk 1 / 2 aus. Die Namen stehen im Fenstertitel.
Fertige Images liegen im Core-Repo in `extras/MERIDIAN_disks.zip`, eigene legt `dienst/disk.py` an (dieselbe Datei wie `tools/disk.py` im Core-Repo;
`disk.py neu SPIELE.DSK --mb 8`, `disk.py rein SPIELE.DSK programm.mer`). In BASIC: `DIR`, `LOAD "NAME"`,
`SAVE "NAME",2`. Änderungen gehen sofort ins Image – am besten mit Kopien arbeiten.
Laufwerk 0 (Speicher eines Moduls) lässt sich mit `--disk0` einhängen. Laufwerk 3 ist ein Ordner (`sdcard/files`, siehe „Laufwerk 3 und Netz“).

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
Cmd+Shift+R (Kaltstart) und die Dateien gemeinsam neu laden. Programme, die Daten brauchen, nennen die Reihenfolge selbst
(z. B. KOBOLDE: `ebene_a.mer`, `ebene_b.mer` und `kobolde.mer` aus `programme/` und `programme/daten/` im Core-Repo; die Testdaten für LADETEST
erzeugt `programme/ladetest.py`).
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
(`NET "HELP"`, `GET`, `CONNECT`, `LISTEN`, `SEND` … siehe BASIC.md im Core-Repo); sie brauchen Internet. Voraussetzung ist Python 3
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
Der Dienst baut von selbst keine Verbindung ins Netz auf; er geht nur ins Netz, wenn BASIC einen `NET`-Befehl schickt oder eine App geöffnet wird
(Wetter, Nachrichten und Sender holen die Apps selbst, siehe `dienst/LIESMICH.txt` und die Hinweise zum Datenschutz im Netzdienst-Paket). Alle `NET`-Befehle (`GET`, `CONNECT`, `LISTEN`, `SEND`, `FILES`, `DISK` …) und Laufwerk 3 gehen ebenfalls.

**RADIO** braucht nichts zu installieren: `make` baut `dienst/bin/mpg123`, einen winzigen MP3-Dekoder (`tools/mpg123mini.c` mit
[minimp3](https://github.com/lieff/minimp3), CC0), den der Dienst wie `mpg123` aufruft. Ist `ffmpeg` installiert, nimmt das Radiomodul auf dem Mac lieber das
(es kann mehr als MP3). Der Dekoder versteht MP3 (MPEG 1/2/2.5, Layer 3) in jeder üblichen Abtastrate, Mono und Stereo, kommt mit ID3-Kopf und
mitten im Strom einsteigenden Sendern zurecht und rechnet auf 22 050 Hz um; AAC-Sender spielt er nicht.

**SID JAM:** `make` baut `dienst/sidkern` und `dienst/sidton` aus `tools/sidkern/` und `tools/sidton/` (im Netzdienst-Paket liegen sie nur als ARM-Programme
für den MiSTer bei). `sidkern` lässt die Abspielroutine einer SID-Datei auf einem 6510 laufen und liefert den Strom der SID-Schreibzugriffe; `sidton` macht daraus
mit reSID den Klang „ORIGINAL“, der Klang „ORGEL“ läuft über den Chip des MERIDIAN. Es fehlt nur noch die HVSC (siehe `sdcard/HVSC` unten).

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

## Quellen und Lizenzen

Der Emulator selbst ist GPL-3.0-or-later. Er enthält und benutzt folgende fremde Programme und Dateien. Was nicht im Paket liegt, muss auf dem Rechner vorhanden sein
oder gehört dem Benutzer.

**Im Paket (Quelltext oder Daten):**

| Was | Wofür | Quelle | Lizenz |
|---|---|---|---|
| MERIDIAN-Core (`MERIDIAN_MiSTer`) | ROMs `roms/*.rom` (aus `rom/*.hex`, Core 20261030); Hardware-Vorlage für den Emulator (`rtl/*.sv`); `dienst/draht.py` und `dienst/disk.py` (unverändert aus `tools/`) | <https://github.com/Retro-Modern/MERIDIAN_MiSTer> | GPL-3.0-or-later, © 2026 retro-modern.net |
| Microsoft BASIC M6502 1.1 | BASIC im ROM `roms/basic.rom` (über den Core) | Core-Repo, `basic/quelle/` | MIT, © Microsoft Corporation |
| Netzdienst 2.0 (31.10.2026) | `dienst/*.py` (Laufwerk 3, `NET`, Wetter, Nachrichten, Radio, ATLAS, TAKTSTOCK, SID JAM, Bilder, Drucken), `dienst/zeichensatz.bin`, `dienst/hvsc_index.tsv.gz`, Beispiele `dienst/beispiele/` (`C64GAMES.FIL`, `MERIDIAN.DRW`) | <https://retro-modern.net/en/downloads.html> (Paket „MERIDIAN network“) | © 2026 retro-modern.net, kostenlos nutzbar und unverändert weitergebbar (siehe `dienst/LIESMICH.txt`) |
| `bildwandler` | Quelle für JPG/PNG/GIF → BMP/PDF (`tools/bildwandler/`), vom Netzdienst gerufen | im Netzdienst-Paket (`bildwandler-quelle/`) | © 2026 retro-modern.net |
| stb_image, stb_image_write | Bilder lesen und schreiben in `bildwandler` (`tools/bildwandler/stb/`) | <https://github.com/nothings/stb> (Stand `003501f6d7a8668ce8b6d875e34dd97ad0d5d5b5`), Sean Barrett | Public Domain oder MIT, nach Wahl |
| `sidkern` | 6510-/C64-Umgebung, liefert den SID-Registerstrom für SID JAM (`tools/sidkern/`) | von retro-modern.net bereitgestellter Quelltext | GPL-3.0-or-later, © 2026 retro-modern.net |
| `sidton` | macht aus dem Registerstrom Klang (`tools/sidton/sidton.cpp`) | von retro-modern.net bereitgestellter Quelltext | GPL-3.0-or-later, © 2026 retro-modern.net |
| reSID | SID-Nachbildung in `sidton` (`tools/sidton/resid/`, unverändert bis auf `siddefs.h` aus `siddefs.h.in`) | <https://github.com/libsidplayfp/resid> (Stand `3bf8eff22bc81743d2d5fa089eaf2414bf3ed6e4`), Dag Lem | GPL-2.0-or-later |
| minimp3 | MP3-Dekodierung im kleinen Dekoder `tools/mpg123mini.c` → `dienst/bin/mpg123` | <https://github.com/lieff/minimp3> | CC0 (Public Domain) |
| HVSC-Index (`hvsc_index.tsv.gz`) | Verzeichnis der HVSC #85 für SID JAM allein; die HVSC selbst liegt **nicht** bei | im Netzdienst-Paket; HVSC: <https://hvsc.c64.org> | siehe Netzdienst-Paket; HVSC: Urheber der Stücke |

**Voraussetzungen auf dem Rechner (nicht im Paket):**

| Was | Wofür | Quelle | Lizenz |
|---|---|---|---|
| SDL2 | Fenster, Ton, Eingabe | <https://www.libsdl.org> | zlib |
| Python 3 (ab 3.8) | Netzdienst (nur Standardbibliothek, keine Zusatzpakete) | <https://www.python.org> | PSF |
| C-/C++-Compiler und `make` (clang oder gcc) | Bau des Emulators und der Hilfsprogramme | Xcode-Kommandozeilenwerkzeuge / GCC | – |
| ffmpeg (optional) | RADIO auf dem Mac, wenn installiert (sonst der eingebaute Dekoder) | <https://ffmpeg.org> | LGPL/GPL je nach Bau |
| HORIZONT, TIEFGANG und weitere Module | Programme für den MERIDIAN, nicht im Paket | Core-Repo (`extras/`) und <https://retro-modern.net/en/downloads.html> | © 2026 retro-modern.net |

**Nur zum Prüfen benutzt (nicht im Paket):** Klaus Dormanns 6502-Funktionstest (<https://github.com/Klaus2m5/6502_65C02_functional_tests>, GPL-3.0) und die
SingleStepTests (<https://github.com/SingleStepTests/65x02>) für die 6502-CPU von `sidkern`; libsidplayfp/sidplayfp (<https://github.com/libsidplayfp/libsidplayfp>) als Vergleich für
den SID-Klang; `qpdf` und `poppler-utils` (`pdftotext`, `pdfinfo`, `pdftoppm`) zum Prüfen der PDF-Dateien; Xvfb, Openbox und xdotool (X11) zum Prüfen des Fensterwechsels.

---

# English

The ROMs in `roms/` are converted from `rom/*.hex` of the core repository (as of 30 Oct 2026, core 20261030, 16 KB each).

## Build and run

    macOS:  brew install sdl2         Linux: sudo apt install libsdl2-dev
    make
    ./meridian                        # options: --pal  --no-sdram  --scale N  --fullscreen  --roms DIR  --module FILE  --version

`make` also builds `dienst/bildwandler` (JPG/PNG/GIF for PICTURES, PAINT, WRITE and printing; source in `tools/bildwandler/`, needs only a C compiler),
`dienst/sidkern` and `dienst/sidton` for SID JAM (sources in `tools/sidkern/` and `tools/sidton/`, again only a C/C++ compiler) and the small MP3 decoder
`dienst/bin/mpg123` for RADIO.

If the same folder is built on several systems (Mac and Linux, shared folder), `make` rebuilds everything when the system changes (file `.plattform`).
If something fails: `make clean && make`.

Without SDL there is a test run: `make headless && ./meridian-headless roms 300 picture.ppm`

Hotkeys (Cmd key on the Mac, Windows key on a PC): **Cmd+C** show/hide the operating system's mouse pointer (it is hidden over the window because the MERIDIAN draws its own; `--show-cursor` keeps it visible), **Cmd+M** sound off/on, **Cmd+R** reset, **Cmd+P** PAL/NTSC,
**Cmd+F** full screen, **Cmd+S** screenshot (`meridian-screenshot.bmp`), **Cmd+D** show the folder of drive 3 in the file manager (printed PDFs end up there, too), **Cmd+Q** quit.
Keys are passed on as PS/2 scancodes (set 2) by key position; the core ROM turns them into the German layout. Both Alt/Option keys work as AltGr (@, {, [ …).

## Disks

    ./meridian --disk1 MERIDIAN.DSK --disk2 BLANK1.DSK

Or drag an image onto the window (drive 1; hold Shift for drive 2).
**Cmd+1** / **Cmd+2** eject drive 1 / 2. The names are shown in the window title.
Ready-made images are in the core repository in `extras/MERIDIAN_disks.zip`; you can create your own with `dienst/disk.py` (the same file as `tools/disk.py` in the core repository;
`disk.py neu GAMES.DSK --mb 8`, `disk.py rein GAMES.DSK program.mer`). In BASIC: `DIR`, `LOAD "NAME"`,
`SAVE "NAME",2`. Changes go into the image immediately – it is best to work on copies.
Drive 0 (a module's memory) can be attached with `--disk0`. Drive 3 is a folder (`sdcard/files`, see "Drive 3 and network").

## Programs and modules

    ./meridian --run DATA.MER --run PROGRAM.MER        # like "Load program" in the MiSTer menu, in this order
    ./meridian --module counter.mod                    # insert a module, restart; --no-module-start = "Cartridge start: Off"

Or drag files onto the window: **`.mer`** loads a program (with autostart it starts right away, without it
the console says "LOADED …"), **`.mod`** inserts a module, anything else goes into drive 1 (with Shift: drive 2).
**Select several files at once and drag them onto the window together.** The emulator then loads them like `senden.py` does,
in one go and in the right order: first modules and images, then data, finally the program with autostart.
One after the other does not work well: `ebene_a.mer` overwrites the kernel's key table with its picture data
(`$03:3E00`); the kernel then believes F1–F6 are pressed and opens the Werkstatt, which clears `$03:4000–$03:7FFF`. This
is the same on the real machine; there `senden.py` simply sends the files quickly enough one after the other. If it has happened anyway:
Cmd+Shift+R (cold start) and load the files together again. Programs that need data name the order themselves
(e.g. KOBOLDE: `ebene_a.mer`, `ebene_b.mer` and `kobolde.mer` from `programme/` and `programme/daten/` in the core repository; the test data for LADETEST
is created by `programme/ladetest.py`).
A module's saved state is kept as `NAME.sav` next to the `.mod` file (drive 0, grows when written).
**Returning from a module** (e.g. Esc in the TIEFGANG title): the core ROM of 25 Oct 2026 resets the display
(`anzeige_zuruecksetzen`); interrupt hooks and sound stay. With older core ROMs this was missing: the module's picture stayed on screen
and BASIC kept running invisibly. For such ROMs the emulator inserts the call itself (only if the kernel bytes at `$DDBB`
exactly match the old ROM; with the bundled new ROM it stays off; `--no-module-fix` turns it off completely).
**Cmd+E** ejects the module, **Cmd+R** is a reset (RAM is kept, as on the real machine), **Cmd+Shift+R** a cold start (RAM cleared, module stays inserted).

## Status

| Component | Status |
|---|---|
| 65C816 (emulation and native mode, BCD, block moves, WAI/STP, IRQ/NMI) | done, boots the kernel and BASIC |
| Memory map: chip RAM, SPIEGEL, expansion memory 15.7 MB, DRAHT bank, ROMs, module ROM | done |
| PINSEL: 2 layers, text 40/80, tiles, bitmap 16/256, scrolling, palette, raster/frame IRQ | done; a line is drawn ahead of time like in the hardware, palette changes take effect to the pixel |
| PFORTE: keyboard, timers A/B, µs clock, calendar clock, mouse | done; joysticks are wired up but there is no input source yet |
| KRAN (blitter) | done, but takes no time (the job runs at once); bit 2 "backwards" interpreted as line order – to be checked against the Verilog code |
| TRUHE (drives 1 and 2, `.DSK`/`.IMG`/`.HDF`, FAT16, read and write) | done, tested with DIR, LOAD, SAVE; write-protected files are attached as "read only" |
| KOBOLD (32 sprites, mirroring, double size, behind layer B, collisions, outline) | done, after `kobold.sv` |
| LOTSE (copper: WAIT, SET, JUMP, SIGNAL, COLOUR) | done; commands take no time, wait points accurate to about 4 dots |
| PINSEL look: shine (SHINE), glow (GLOW), ink (INK: outline and drop shadow) | done, after `pinsel.sv` |
| ORGEL (sound): 4 synthesis voices (SID waveforms, sync, ring, envelope), filter, 8 sample channels with crusher/distortion, echo, stereo | done, runs at the 1 MHz clock after `orgel.sv`; output 48 kHz via SDL |
| BOTE: load `.MER` programs (chip RAM and expansion memory, autostart via NMI), modules (`.MOD`, write-protected from `$40:0000`, survive resets), saved state `.sav` as drive 0 | done, tested with MONTEST, KRANTEST, DOSTEST, LADETEST, KOBOLDE, GRAFIK, BALLETT, COUNTER |
| DRAHT: window bank `$FD` as a shared file, with the real network service 2.0 `dienst/draht.py` (Python 3) attached: **drive 3 = folder**, `NET` commands | done for macOS and Linux; not yet for Windows (bank `$FD` stays plain memory there) |

Open: starting the network service on Windows, `FETCH` (needs the BOTE mailbox; in simulation mode it reports "only on the MiSTer" itself).

## Drive 3 and network (DRAHT)

At startup the emulator runs `python3 dienst/draht.py` (the unchanged network service from the core) and gives it the DRAHT window
(bank `$FD`) as a shared file. As on the MiSTer this means:

    ./meridian                      # folder sdcard next to the program, drive 3 = sdcard/files (is created)
    ./meridian --ordner ~/Games/sdcard

What is in `sdcard/files` is visible to the MERIDIAN at once (`DIR 3`, `LOAD "NAME",3`, `SAVE "NAME",3`); what it saves is immediately
available on the Mac. Long names appear as 8.3 short names (`Space Debris.mod` → `SPACED~1.MOD`). Plus the `NET` commands
(`NET "HELP"`, `GET`, `CONNECT`, `LISTEN`, `SEND` … see BASIC.md in the core repository); they need internet access. Python 3 is required
(`python3` is looked up in the PATH, then `/usr/bin`, Homebrew locations and `python`; `MERIDIAN_PYTHON=/path/python3` picks one; at least 3.8). The window title shows "D3: Ordner files" when the service is running, otherwise the reason (no Python, no `draht.py`, service ended,
not answering), with details and an excerpt from the log in the console. The service's log: `$TMPDIR/meridian-draht.log`. `--no-dienst`
turns all this off (drive 3 then reports error 4 after 10 s). The service ends with the emulator, even after a crash.

**Checking that the service is reachable:** `./meridian --dienst-test` sends a `HELP` through the DRAHT window without a window and reports
"Selbsttest OK" or the reason. The same happens one second after every normal start (result in the title: "D3: Ordner files"). At the BASIC prompt:

    NET "HELP":PRINT NET(0):PRINT NET$(0)
    NET "GET wttr.in/Berlin?format=3":PRINT NET(0):PRINT NET$(0):PRINT NET$(0)

`NET(0)` is 0 or the HTTP code (200), negative on errors (−3 no connection). Every `NET` command is written to the service's log together with its result
(`tail -f` on the file named at startup); if nothing is there, the request never reached the service.
**HTTPS certificates (macOS):** Pythons from python.org and Homebrew do not know the system keychain and fail for
weather, news and radio with `CERTIFICATE_VERIFY_FAILED`. The emulator therefore passes the system's certificate bundle to the service
(`/etc/ssl/cert.pem`, otherwise Homebrew locations; `SSL_CERT_FILE=/path/cert.pem` sets your own; `certifi` is the fallback). Which bundle is used
is shown in the console at startup ("DRAHT: HTTPS-Zertifikate: …"); if Python has none at all, a hint with the remedy appears there
(`Install Certificates.command` in the Python folder, or `pip3 install certifi`).

**HORIZONT (module) and its apps:** the emulator ships network service 2.0 (HORIZONT 2.4). With it these work:
WEATHER (and the weather in the menu bar), NEWS, ATLAS (browser page and downloads), TAKTSTOCK (network jam), RADIO (including recording "REC", 32 kHz/12 bit), SID JAM (see below), PICTURES and PAINT with JPG/PNG/GIF, and **printing** (see below). The browser page is reachable only on
this computer: `http://localhost:8816/` (`--ablage-port N`, 0 = off; `MERIDIAN_ABLAGE_LAN=1` opens it to the home network as on the MiSTer).
The service never opens a network connection on its own; it goes online only when BASIC sends a `NET` command or an app is opened
(the apps fetch weather, news and stations themselves, see `dienst/LIESMICH.txt` and the privacy notes in the network service package). All `NET` commands (`GET`, `CONNECT`, `LISTEN`, `SEND`, `FILES`, `DISK` …) and drive 3 work as well.

**RADIO** needs nothing to be installed: `make` builds `dienst/bin/mpg123`, a tiny MP3 decoder (`tools/mpg123mini.c` with
[minimp3](https://github.com/lieff/minimp3), CC0), which the service calls like `mpg123`. If `ffmpeg` is installed, the radio module on the Mac prefers it
(it can do more than MP3). The decoder understands MP3 (MPEG 1/2/2.5, layer 3) at every usual sample rate, mono and stereo, copes with an ID3 header and
stations that are joined in the middle of the stream, and converts to 22,050 Hz; it does not play AAC stations.

**SID JAM:** `make` builds `dienst/sidkern` and `dienst/sidton` from `tools/sidkern/` and `tools/sidton/` (in the network service package they come only as ARM programs
for the MiSTer). `sidkern` runs a SID file's play routine on a 6510 and delivers the stream of SID register writes; `sidton` turns it into the "ORIGINAL" sound with
reSID, while the "ORGEL" sound runs through the MERIDIAN's own chip. Only the HVSC is still missing (see `sdcard/HVSC` below).

**Printing (HORIZONT 2.4):** NOTES, PAINT and the KONTOR apps (WRITE, CALC, FILE, DRAW) offer "Print to PDF …" and "Print".
"Print to PDF …" puts an A4 file `NAME.PDF` on drive 3 (`sdcard/files`; Cmd+D shows the folder). "Print" sends the PDF by IPP to a
network printer that accepts PDF (IPP Everywhere/AirPrint): first line of `sdcard/files/PRINTER.TXT` (`192.168.1.20`, a name or `ipp://…/ipp/print`);
without that file the service searches via Bonjour. The printing itself is done by the network service's unchanged `dienst/druck_dienst.py` (pure Python).
The emulator puts examples for FILE and DRAW (`C64GAMES.FIL`, `MERIDIAN.DRW`) into `sdcard/files` at startup if they are missing there.

## The `sdcard` folder (corresponds to `games/MERIDIAN` on the MiSTer)

    sdcard/files/                     drive 3 (DIR 3, LOAD "X",3, SAVE "X",3); RADIO.TXT lives here too (own stations: name|http://address)
    sdcard/wetter.json                location and settings of WEATHER (created when the app is first opened)
    sdcard/HVSC/hvsc_index.tsv.gz     index for SID JAM stand-alone (HVSC #85); the emulator puts it there itself if it is missing, and never overwrites it
    sdcard/HVSC/hvsc.zip              the HVSC itself: zip the folder `C64Music` (`zip -r hvsc.zip C64Music`, entries must start with `C64Music/`,
                                      only method "stored" or "deflate"); `hvsc.zip.liste` is created next to it at the first start

To build the index yourself for another HVSC version: `python3 dienst/hvsc_index.py bauen C64Music sdcard/HVSC/hvsc_index.tsv.gz`.

## Sound

`--no-audio` turns it off. The ORGEL's 1 MHz mixer is averaged down to 48 kHz for output (simple box filter);
samples and echo read directly from memory, the hardware's DMA timing is not reproduced (the sound does not change because of that).
Echo and samples from bank `$04` need the expansion memory (not with `--no-sdram`). Tested by recording and spectrum:
`SOUND 1,440` gives 440 Hz; BALLETT, the ORGEL demo, FILTERTEST and the Zusammenspiel jingle deliver plausible levels in stereo.
To measure without a window: `make headless`, run the machine with `audio_on = true` and fetch with `m_audio_read()`. The time base is laid out for an 8 MHz / 24 MHz raster (509 CPU cycles per line).

## Sources and licenses

The emulator itself is GPL-3.0-or-later. It contains and uses the following third-party programs and files. Whatever is not in the package must be present on the computer
or belongs to the user.

**In the package (source code or data):**

| What | Used for | Source | License |
|---|---|---|---|
| MERIDIAN core (`MERIDIAN_MiSTer`) | ROMs `roms/*.rom` (from `rom/*.hex`, core 20261030); hardware reference for the emulator (`rtl/*.sv`); `dienst/draht.py` and `dienst/disk.py` (unchanged from `tools/`) | <https://github.com/Retro-Modern/MERIDIAN_MiSTer> | GPL-3.0-or-later, © 2026 retro-modern.net |
| Microsoft BASIC M6502 1.1 | BASIC in the ROM `roms/basic.rom` (via the core) | core repository, `basic/quelle/` | MIT, © Microsoft Corporation |
| Network service 2.0 (31 Oct 2026) | `dienst/*.py` (drive 3, `NET`, weather, news, radio, ATLAS, TAKTSTOCK, SID JAM, pictures, printing), `dienst/zeichensatz.bin`, `dienst/hvsc_index.tsv.gz`, examples `dienst/beispiele/` (`C64GAMES.FIL`, `MERIDIAN.DRW`) | <https://retro-modern.net/en/downloads.html> (package "MERIDIAN network") | © 2026 retro-modern.net, free to use and to pass on unchanged (see `dienst/LIESMICH.txt`) |
| `bildwandler` | source for JPG/PNG/GIF → BMP/PDF (`tools/bildwandler/`), called by the network service | in the network service package (`bildwandler-quelle/`) | © 2026 retro-modern.net |
| stb_image, stb_image_write | reading and writing images in `bildwandler` (`tools/bildwandler/stb/`) | <https://github.com/nothings/stb> (revision `003501f6d7a8668ce8b6d875e34dd97ad0d5d5b5`), Sean Barrett | public domain or MIT, your choice |
| `sidkern` | 6510/C64 environment, delivers the SID register stream for SID JAM (`tools/sidkern/`) | source code provided by retro-modern.net | GPL-3.0-or-later, © 2026 retro-modern.net |
| `sidton` | turns the register stream into sound (`tools/sidton/sidton.cpp`) | source code provided by retro-modern.net | GPL-3.0-or-later, © 2026 retro-modern.net |
| reSID | SID emulation inside `sidton` (`tools/sidton/resid/`, unchanged except `siddefs.h` generated from `siddefs.h.in`) | <https://github.com/libsidplayfp/resid> (revision `3bf8eff22bc81743d2d5fa089eaf2414bf3ed6e4`), Dag Lem | GPL-2.0-or-later |
| minimp3 | MP3 decoding in the small decoder `tools/mpg123mini.c` → `dienst/bin/mpg123` | <https://github.com/lieff/minimp3> | CC0 (public domain) |
| HVSC index (`hvsc_index.tsv.gz`) | directory of HVSC #85 for SID JAM stand-alone; the HVSC itself is **not** included | in the network service package; HVSC: <https://hvsc.c64.org> | see network service package; HVSC: the tunes' authors |

**Required on the computer (not in the package):**

| What | Used for | Source | License |
|---|---|---|---|
| SDL2 | window, sound, input | <https://www.libsdl.org> | zlib |
| Python 3 (3.8 or later) | network service (standard library only, no extra packages) | <https://www.python.org> | PSF |
| C/C++ compiler and `make` (clang or gcc) | building the emulator and the helper programs | Xcode command line tools / GCC | – |
| ffmpeg (optional) | RADIO on the Mac if installed (otherwise the built-in decoder) | <https://ffmpeg.org> | LGPL/GPL depending on the build |
| HORIZONT, TIEFGANG and other modules | programs for the MERIDIAN, not in the package | core repository (`extras/`) and <https://retro-modern.net/en/downloads.html> | © 2026 retro-modern.net |

**Used for testing only (not in the package):** Klaus Dormann's 6502 functional test (<https://github.com/Klaus2m5/6502_65C02_functional_tests>, GPL-3.0) and the
SingleStepTests (<https://github.com/SingleStepTests/65x02>) for `sidkern`'s 6502 CPU; libsidplayfp/sidplayfp (<https://github.com/libsidplayfp/libsidplayfp>) as a comparison for
the SID sound; `qpdf` and `poppler-utils` (`pdftotext`, `pdfinfo`, `pdftoppm`) to check the PDF files; Xvfb, Openbox and xdotool (X11) to check the window switching.
