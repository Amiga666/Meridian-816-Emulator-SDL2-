# MERIDIAN 816 – Emulator (Stufe 1, Version 0.2.0)

Emulator für den MiSTer-Core [MERIDIAN_MiSTer](https://github.com/Retro-Modern/MERIDIAN_MiSTer),
geschrieben in C mit SDL2. Läuft auf macOS, Linux und Windows (MSYS2).
Lizenz: GPL-3.0-or-later (wie der Core). Die ROMs in `roms/` sind aus `rom/*.hex` des Core-Repos
umgewandelt (je 16 KB); BASIC stammt von Microsoft (MIT-Lizenz, siehe Core-Repo).

## Bauen und starten

    macOS:  brew install sdl2         Linux: sudo apt install libsdl2-dev
    make
    ./meridian                        # Optionen: --pal  --no-sdram  --scale N  --fullscreen  --roms DIR  --module DATEI  --version

Ohne SDL gibt es einen Testlauf: `make headless && ./meridian-headless roms 300 bild.ppm`

Hotkeys (Cmd-Taste auf dem Mac, Windows-Taste am PC): **Cmd+R** Reset, **Cmd+P** PAL/NTSC,
**Cmd+F** Vollbild, **Cmd+S** Screenshot (`meridian-screenshot.bmp`), **Cmd+Q** Beenden.
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

## Stand

| Baustein | Stand |
|---|---|
| 65C816 (emulierend und nativ, BCD, Block-Moves, WAI/STP, IRQ/NMI) | fertig, bootet Kern und BASIC |
| Speicherkarte: Chip-RAM, SPIEGEL, Zusatzspeicher 15,7 MB, DRAHT-Bank, ROMs, Modul-ROM | fertig |
| PINSEL: 2 Ebenen, Text 40/80, Kacheln, Bitmap 16/256, Scrolling, Palette, Raster-/Bild-IRQ | fertig (zeilenweise, Palettenwechsel mitten im Bild wirken) |
| PFORTE: Tastatur, Timer A/B, µs-Uhr, Kalenderuhr, Maus | fertig; Joysticks sind verdrahtet, aber noch keine Eingabequelle |
| KRAN (Blitter) | fertig, aber ohne Zeitverbrauch (Auftrag läuft sofort); Bit 2 „rückwärts“ als Zeilenreihenfolge gedeutet – gegen den Verilog-Code prüfen |
| TRUHE (Laufwerke 1 und 2, `.DSK`/`.IMG`/`.HDF`, FAT16, lesen und schreiben) | fertig, getestet mit DIR, LOAD, SAVE; schreibgeschützte Dateien werden als „nur lesen“ eingehängt |
| KOBOLD (Sprites), LOTSE (Copper), PINSEL-Look (Glanz, Leuchten, Tusche) | **offen** – Register lesen/schreiben nur, ohne Wirkung |
| ORGEL (Klang) | **offen**, kein Ton |
| BOTE (`.MER`-Programme laden, Fernstart, Module), DRAHT (Netz, Laufwerk 3 = Ordner `files`) | **offen** |

Nächste Stufen: KOBOLD + LOTSE (für GLOWMINE & Demos), TRUHE mit FAT16-Images, ORGEL über SDL-Audio,
dann Glanz/Leuchten/Tusche. Die Zeitbasis ist auf 8 MHz / 24 MHz-Raster ausgelegt (509 CPU-Zyklen je Zeile).
