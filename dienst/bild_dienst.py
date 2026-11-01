#!/usr/bin/env python3
"""Bilder fuer PICTURES und PAINT (Netzdienst, Art 22)

Der MERIDIAN liest nur unkomprimierte BMP. JPG, PNG und alles andere, was
stb_image kennt, wandelt das Programm bildwandler (meridian/bildwandler/,
statisch fuer den MiSTer gebaut) in eine 8-Bit-BMP mit den Farben des
MERIDIAN (4 Bit je Anteil), und es schreibt Bilder aus PAINT als PNG oder JPG.

Befehle (Text der Anfrage):
    LADE NAME.EXT E N B H   Datei von Laufwerk 3 wandeln: in B x H eingepasst,
                            hoechstens N Farben auf den Plaetzen E .. E+N-1.
                            Das Ergebnis liest die App danach als ~BILD.BMP
                            von Laufwerk 3 (ein Name nur fuer sie, er steht
                            in keinem Verzeichnis).
    LADE NAME.EXT E 16 B H FEST
                            wie oben, aber mit den festen 16 Farben von WRITE
                            (bildwandler --fest 1) auf E .. E+15.
    SICHERE NAME.EXT        was die App als ~MALEN.BMP auf Laufwerk 3
                            geschrieben hat, als NAME.EXT ablegen (PNG und
                            JPG gewandelt, BMP kopiert).
    STAND                   0 nichts, 1 BUSY, 2 fertig ("W H" bzw. "OK"),
                            3 Fehler (Text).
Beides laeuft in einem eigenen Faden - ein 12-MP-Foto braucht auf dem MiSTer
ein paar Sekunden; die App fragt solange STAND.
"""
import os
import shutil
import subprocess
import threading

HIER = os.path.dirname(os.path.abspath(__file__))
ZEIT = 30                               # Sekunden je Lauf
BILD, MALEN = "bild.bmp", "malen.bmp"   # im Zwischenspeicher: ~BILD.BMP, ~MALEN.BMP


def wandler_finden():
    for p in (os.environ.get("MERIDIAN_BILDWANDLER", ""), os.path.join(HIER, "bildwandler"),
              os.path.join(HIER, "..", "bildwandler", "bau", "bildwandler-macos"),
              os.path.join(HIER, "..", "bildwandler", "bau", "bildwandler-linux-armv7")):
        if p and os.path.isfile(p) and os.access(p, os.X_OK):
            return p
    return None


class Bild:
    def __init__(self, datei_pfad, dateien, cache="/tmp/meridian_bild"):
        self.datei_pfad = datei_pfad    # "NAME.EXT" (8.3) -> Pfad auf Laufwerk 3 oder None
        self.dateien = dateien          # () -> Ordner von Laufwerk 3
        self.cache = cache
        self.lock = threading.Lock()
        self.zustand, self.ergebnis = 0, b""
        self.faden = None

    def pfad(self, name):
        """Pfad im Zwischenspeicher (legt den Ordner an)"""
        os.makedirs(self.cache, exist_ok=True)
        return os.path.join(self.cache, name)

    # -------------------------------------------------- Anfragen
    def anfrage(self, text):
        teile = text.split(b"\0")[0].decode("latin-1").upper().split()
        if not teile:
            return -1, b"BAD COMMAND"
        befehl = teile[0]
        if befehl == "STAND":
            with self.lock:
                return self.zustand, self.ergebnis
        if befehl not in ("LADE", "SICHERE"):
            return -1, b"BAD COMMAND"
        with self.lock:
            if self.zustand == 1:
                return -1, b"BUSY"
        wandler = wandler_finden()
        if befehl == "LADE":
            fest = len(teile) == 7 and teile[6] == "FEST"
            if len(teile) != 6 and not fest:
                return -1, b"BAD COMMAND"
            try:
                e, n, b, h = (int(t) for t in teile[2:6])
            except ValueError:
                return -1, b"BAD COMMAND"
            quelle = self.datei_pfad(teile[1])
            if not quelle:
                return -1, b"FILE NOT FOUND"
            if not wandler:
                return -1, b"NO CONVERTER"
            ziel = self.pfad(BILD)
            cmd = [wandler, "bmp", quelle, ziel, "--erste", str(e), "--farben", str(n),
                   "--breite", str(b), "--hoehe", str(h)]
            if fest:
                cmd += ["--fest", "1"]
            return self.starten(cmd, ziel, None, "lade " + os.path.basename(quelle))
        # SICHERE
        if len(teile) != 2 or "." not in teile[1]:
            return -1, b"BAD COMMAND"
        name = teile[1]
        endung = name.rsplit(".", 1)[1]
        if endung not in ("PNG", "JPG", "BMP"):
            return -1, b"ONLY BMP, PNG AND JPG"
        quelle = self.pfad(MALEN)
        if not os.path.isfile(quelle):
            return -1, b"NOTHING TO SAVE"
        ziel = self.datei_pfad(name) or os.path.join(self.dateien(), name)
        if endung == "BMP":
            cmd = None
        else:
            if not wandler:
                return -1, b"NO CONVERTER"
            cmd = [wandler, endung.lower(), quelle, ziel + ".neu"]
            if endung == "JPG":
                cmd += ["--qualitaet", "92"]
        return self.starten(cmd, ziel, quelle, "sichere " + os.path.basename(ziel))

    def starten(self, cmd, ziel, kopie_von, was):
        with self.lock:
            self.zustand, self.ergebnis = 1, b"BUSY"
        self.faden = threading.Thread(target=self.lauf, args=(cmd, ziel, kopie_von, was), daemon=True)
        self.faden.start()
        return 0, b""

    # -------------------------------------------------- im Faden
    def lauf(self, cmd, ziel, kopie_von, was):
        try:
            if cmd is None:                         # BMP: nur kopieren
                shutil.copyfile(kopie_von, ziel + ".neu")
                os.replace(ziel + ".neu", ziel)
                ergebnis = b"OK"
            else:
                r = subprocess.run(cmd, capture_output=True, timeout=ZEIT)
                if r.returncode != 0:
                    text = r.stderr.decode("latin-1", "replace").strip().splitlines()
                    raise RuntimeError(text[-1] if text else "CONVERTER FAILED (%d)" % r.returncode)
                if kopie_von:                       # SICHERE: fertige Datei an ihren Platz
                    os.replace(ziel + ".neu", ziel)
                    ergebnis = b"OK"
                else:
                    ergebnis = r.stdout.decode("latin-1").strip().encode("latin-1")[:20]
            print("BILD:", was, "->", ergebnis.decode("latin-1"), flush=True)
            with self.lock:
                self.zustand, self.ergebnis = 2, ergebnis
        except subprocess.TimeoutExpired:
            self.fehler(was, "TOO SLOW")
        except Exception as e:
            self.fehler(was, str(e))
        finally:
            if os.path.exists(ziel + ".neu"):
                try:
                    os.remove(ziel + ".neu")
                except OSError:
                    pass

    def fehler(self, was, text):
        print("BILD:", was, "- Fehler:", text, flush=True)
        with self.lock:
            self.zustand, self.ergebnis = 3, text.upper().encode("latin-1", "replace")[:40]
