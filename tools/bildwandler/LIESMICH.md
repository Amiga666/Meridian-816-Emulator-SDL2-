# bildwandler

Wandelt Bilder für den MERIDIAN 816. Der Netzdienst ruft ihn für die Apps
PICTURES, PAINT und WRITE (Art 22, `tools/bild_dienst.py`) und zum Drucken
(`tools/druck_dienst.py`).

    bildwandler bmp QUELLE ZIEL.BMP --erste E --farben N --breite B --hoehe H [--fest 1]
    bildwandler ppm QUELLE ZIEL.PPM --breite B --hoehe H
    bildwandler png|jpg QUELLE ZIEL [--qualitaet 90]

- `bmp`: jedes Format von stb_image (JPG, PNG, BMP, GIF, TGA …) wird zu einer
  8-Bit-BMP: in B × H eingepasst (nur verkleinert, Flächenmittel), Alpha über
  Schwarz, Farben mit 4 Bit je Anteil wie die Palette des MERIDIAN (in der
  BMP als Vielfache von 17), höchstens N Farben auf den Plätzen E … E+N−1.
  Hat das Bild höchstens N solche Farben, bleiben sie genau; sonst Median-Cut
  und Floyd-Steinberg. Ausgabe: `W H`.
  `--fest 1` (WRITE): statt eigener Farben die festen 16 (Werte der
  PICO-8-Palette) auf E … E+15; bis 64 Farben die nächste, sonst
  Floyd-Steinberg.
- `ppm`: volle Farben für den Druck (P6), in B × H eingepasst (nur
  verkleinert).
- `png`/`jpg`: QUELLE (meist eine BMP aus PAINT) als PNG (RGB) oder JPG.
- Fehler: eine englische Zeile auf stderr, Rückgabe 1.

Bauen: `./bauen.sh` (macOS mit clang, MiSTer mit zig als statisches
ARMv7-Programm) → `bau/bildwandler-macos`, `bau/bildwandler-linux-armv7`.

## Lizenz

`stb/stb_image.h` und `stb/stb_image_write.h` sind von Sean Barrett
(github.com/nothings/stb, Stand in `stb/HERKUNFT.txt`), Public Domain oder
MIT – nach Wahl. Der Rest: (c) 2026 retro-modern.net.
