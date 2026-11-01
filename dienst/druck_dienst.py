#!/usr/bin/env python3
"""Drucken fuer HORIZONT (Netzdienst, Art 24)

Die App legt, was sie drucken will, als Datei auf Laufwerk 3 ab - NOTES den
Text als ~DRUCK.TXT, PAINT das Bild als ~MALEN.BMP (beide nur im
Zwischenspeicher, in keinem Verzeichnis) - oder nennt eine Datei, die dort
schon liegt. Der Dienst macht daraus ein PDF (A4, reines Python: Text in
Courier, Bilder scharf vergroessert) und

    DRUCKE QUELLE [TITEL]       schickt es an den Netzwerkdrucker (IPP),
    PDF QUELLE NAME.PDF [TITEL] legt es als NAME.PDF auf Laufwerk 3 ab,
    DRUCKER                     nennt den Drucker (oder Fehlertext),
    STAND                       0 nichts, 1 BUSY, 2 fertig (Text), 3 Fehler.

QUELLE: .TXT (Zeilen mit LF, Umbruch nach 70 Zeichen wie in NOTES), .BMP
(1, 4, 8 oder 24 Bit, unkomprimiert) oder .WRL (Zeilenliste von WRITE: jede
Zeile mit Lage, jedes Wort mit Schrift, Stil, Lage und Breite am Schirm;
Bilder als Name einer Datei auf Laufwerk 3, die der Dienst in Druckaufloesung
einsetzt: BMP bis 8 Bit scharf, alles andere ueber bildwandler ppm). CALC
schreibt die .WRL in Fassung 2: Zelltexte (Satz 7) und Gitterlinien (Satz 6).
FILE setzt fuer Etiketten Satz 8 (Seite einrichten): Masse in 1/10 mm ab
dem Blattrand statt in Schirmpunkten, ohne Kopfzeile, eigene Schriftgroesse.
DRAW schickt seine Formen als Vektoren: Satz 9 (die 32 Bytes einer Form,
dazu die Punkte eines Vielecks), Satz 11 (eine gesetzte Textzeile), Satz 8
mit Bit 0 im vierten Byte fuer A4 quer. Ellipsen und runde Ecken werden zu
Bezier-Kurven, Muster zu PDF-Kachelmustern.
TITEL steht oben auf jeder Seite (bei .WRL nur in Fassung 2: bei WRITE ist
die Seite das Dokument).

Der Drucker: die erste Zeile in PRINTER.TXT auf Laufwerk 3 (eine Adresse wie
192.168.1.20, ein Name oder ipp://.../ipp/print); fehlt sie, sucht der Dienst
per Bonjour (mDNS, _ipp._tcp) im Heimnetz und nimmt den ersten. Der Drucker
muss PDF annehmen (IPP Everywhere / AirPrint koennen das meist). Alles laeuft
in einem eigenen Faden; die App fragt solange STAND.
"""
import http.client
import os
import random
import subprocess
import socket
import struct
import threading
import time
import zlib

ZEIT = 60                               # Sekunden fuer den Drucker
DRUCK = "druck.txt"                     # im Zwischenspeicher: ~DRUCK.TXT
DRUCK_WRL = "druck.wrl"                 # ~DRUCK.WRL (WRITE, CALC)
DRUCK_BMP = "druck.bmp"                 # ~DRUCK.BMP (Diagramm von CALC)
SPALTEN = 70                            # wie NOTES

A4_B, A4_H = 595, 842                   # Punkte (1/72 Zoll)


# ====================================================================== PDF

class Pdf:
    """Kleiner PDF-Schreiber: Seiten mit Inhaltsstrom, Courier/Helvetica, Bilder"""

    def __init__(self):
        self.objekte = []               # bytes je Objekt (Nummer = Index + 1)
        self.seiten = []                # Objektnummern der Seiten
        self.bilder = {}                # Name -> Objektnummer
        self.katalog = self.neu(b"")    # 1, wird am Ende gefuellt
        self.baum = self.neu(b"")       # 2
        self.f_courier = self.neu(b"<< /Type /Font /Subtype /Type1 /BaseFont /Courier /Encoding /WinAnsiEncoding >>")
        self.f_helv = self.neu(b"<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica /Encoding /WinAnsiEncoding >>")
        self.schriften = {}             # Grundschrift -> (Kuerzel, Objekt), fuer WRITE
        self.muster = {}                # (Muster, Farbe) -> (Name, Objekt), fuer DRAW

    def schrift(self, name):
        """Grundschrift (z. B. Times-Bold) -> Kuerzel fuer Tf"""
        if name not in self.schriften:
            o = self.neu(b"<< /Type /Font /Subtype /Type1 /BaseFont /%s /Encoding /WinAnsiEncoding >>" % name.encode())
            self.schriften[name] = ("F%d" % (len(self.schriften) + 1), o)
        return self.schriften[name][0]

    def neu(self, inhalt):
        self.objekte.append(inhalt)
        return len(self.objekte)

    def strom(self, kopf, daten, packen=True):
        if packen:
            daten = zlib.compress(daten, 6)
            kopf += b" /Filter /FlateDecode"
        return self.neu(b"<< " + kopf + b" /Length %d >>\nstream\n" % len(daten) + daten + b"\nendstream")

    def bild(self, name, breite, hoehe, palette, punkte):
        """palette: 256 x RGB (bytes), punkte: ein Byte je Punkt, Zeilen von oben"""
        pal = self.strom(b"", palette, packen=False)
        kopf = (b"/Type /XObject /Subtype /Image /Width %d /Height %d /BitsPerComponent 8 "
                b"/ColorSpace [/Indexed /DeviceRGB %d %d 0 R] /Interpolate false"
                % (breite, hoehe, len(palette) // 3 - 1, pal))
        self.bilder[name] = self.strom(kopf, punkte)

    def bild_rgb(self, name, breite, hoehe, rgb):
        """Foto: 3 Bytes je Punkt, Zeilen von oben, weich gerechnet"""
        kopf = (b"/Type /XObject /Subtype /Image /Width %d /Height %d /BitsPerComponent 8 "
                b"/ColorSpace /DeviceRGB /Interpolate true" % (breite, hoehe))
        self.bilder[name] = self.strom(kopf, rgb)

    def kachel(self, muster, rgb):
        """Kachelmuster von DRAW (8 x 8, Bit 7 links): nur die gesetzten Punkte in rgb"""
        if (muster, rgb) not in self.muster:
            d = b"%.3f %.3f %.3f rg\n" % rgb
            for z, bits in enumerate(D_MUSTER[muster]):
                for sp in range(8):
                    if bits & (0x80 >> sp):
                        d += b"%d %d 1 1 re\n" % (sp, 7 - z)
            d += b"f\n"
            s = D_PUNKT_MM * 10 * PT_MM         # ein Punkt des Musters in pt
            kopf = (b"/Type /Pattern /PatternType 1 /PaintType 1 /TilingType 1 /BBox [0 0 8 8] "
                    b"/XStep 8 /YStep 8 /Resources << >> /Matrix [%.4f 0 0 %.4f 0 0]" % (s, s))
            self.muster[(muster, rgb)] = ("P%d" % (len(self.muster) + 1), self.strom(kopf, d))
        return self.muster[(muster, rgb)][0]

    def seite(self, inhalt, breite=A4_B, hoehe=A4_H):
        s = self.strom(b"", inhalt)
        res = b"/Font << /C %d 0 R /H %d 0 R" % (self.f_courier, self.f_helv)
        res += b"".join(b" /%s %d 0 R" % (k.encode(), o) for k, o in self.schriften.values()) + b" >>"
        if self.bilder:
            res += b" /XObject << " + b" ".join(b"/%s %d 0 R" % (n.encode(), o) for n, o in self.bilder.items()) + b" >>"
        if self.muster:
            res += b" /Pattern << " + b" ".join(b"/%s %d 0 R" % (n.encode(), o) for n, o in self.muster.values()) + b" >>"
        self.seiten.append(self.neu(b"<< /Type /Page /Parent 2 0 R /MediaBox [0 0 %d %d] /Resources << %s >> /Contents %d 0 R >>"
                                    % (breite, hoehe, res, s)))

    def fertig(self, titel=""):
        self.objekte[0] = b"<< /Type /Catalog /Pages 2 0 R >>"
        self.objekte[1] = (b"<< /Type /Pages /Count %d /Kids [%s] >>"
                           % (len(self.seiten), b" ".join(b"%d 0 R" % s for s in self.seiten)))
        info = self.neu(b"<< /Title " + text_pdf(titel or "MERIDIAN") + b" /Producer (MERIDIAN 816 - HORIZONT) >>")
        aus = bytearray(b"%PDF-1.4\n%\xe2\xe3\xcf\xd3\n")
        lagen = []
        for i, o in enumerate(self.objekte, 1):
            lagen.append(len(aus))
            aus += b"%d 0 obj\n" % i + o + b"\nendobj\n"
        xref = len(aus)
        aus += b"xref\n0 %d\n0000000000 65535 f \n" % (len(self.objekte) + 1)
        for l in lagen:
            aus += b"%010d 00000 n \n" % l
        aus += (b"trailer\n<< /Size %d /Root 1 0 R /Info %d 0 R >>\nstartxref\n%d\n%%%%EOF\n"
                % (len(self.objekte) + 1, info, xref))
        return bytes(aus)


def text_pdf(t):
    """Text -> PDF-Zeichenkette in WinAnsi (Latin-1 passt fuer A0-FF)"""
    b = t.encode("latin-1", "replace") if isinstance(t, str) else t
    b = bytes(c if c >= 0x20 else 0x20 for c in b)
    return b"(" + b.replace(b"\\", b"\\\\").replace(b"(", b"\\(").replace(b")", b"\\)") + b")"


def kopfzeile(titel, nr, gesamt):
    t = b"BT /H 8 Tf 0.3 g 56 806 Td " + text_pdf(titel) + b" Tj ET\n" if titel else b""
    seite = "%d / %d" % (nr, gesamt)
    return t + b"BT /H 8 Tf 0.3 g %d 806 Td " % (A4_B - 56 - 4 * len(seite)) + text_pdf(seite) + b" Tj ET\n" \
        + b"0.5 G 0.6 w 56 800 m %d 800 l S\n" % (A4_B - 56)


def text_zu_pdf(roh, titel=""):
    """Textdatei (LF, CR faellt weg) -> PDF, Zeilen nach 70 Zeichen umgebrochen"""
    if any(c >= 0x80 for c in roh):     # UTF-8 (vom Mac) -> Latin-1 wie der MERIDIAN
        try:
            roh = roh.decode("utf-8").encode("latin-1", "replace")
        except UnicodeError:
            pass
    zeilen = []
    for z in roh.replace(b"\r", b"").split(b"\n"):
        z = z.replace(b"\t", b"    ")
        while len(z) > SPALTEN:
            zeilen.append(z[:SPALTEN])
            z = z[SPALTEN:]
        zeilen.append(z)
    while zeilen and not zeilen[-1].strip():
        zeilen.pop()
    if not zeilen:
        zeilen = [b""]
    je_seite = 58                       # Courier 10 auf 12 Punkt Zeilenabstand
    links = (A4_B - SPALTEN * 6) // 2   # 70 Zeichen zu 6 Punkt, mittig
    pdf = Pdf()
    seiten = [zeilen[i:i + je_seite] for i in range(0, len(zeilen), je_seite)]
    for nr, teil in enumerate(seiten, 1):
        s = kopfzeile(titel, nr, len(seiten))
        s += b"BT /C 10 Tf 0 g 12 TL %d 776 Td\n" % links
        for z in teil:
            s += text_pdf(z) + b" '\n"
        s += b"ET\n"
        pdf.seite(s)
    return pdf.fertig(titel), len(seiten)


# ====================================================================== WRITE

def _b(t):
    return [int(v) for v in t.split()]


# Breiten der Grundschriften (1/1000 der Groesse, Zeichen 32-255 in WinAnsi),
# aus den Adobe-Metriken (AFM); Courier hat ueberall 600.
BREITEN = {
    "Helvetica": _b("""
        278 278 355 556 556 889 667 191 333 333 389 584 278 333 278 278 556 556 556 556
        556 556 556 556 556 556 278 278 584 584 584 556 1015 667 667 722 722 667 611 778
        722 278 500 667 556 833 722 778 667 778 722 667 611 722 667 944 667 667 611 278
        278 278 469 556 333 556 556 500 556 556 278 556 556 222 222 500 222 833 556 556
        556 556 333 500 278 556 500 722 500 500 500 334 260 334 584 350 556 350 222 556
        333 1000 556 556 333 1000 667 333 1000 350 611 350 350 222 222 333 333 350 556 1000
        333 1000 500 333 944 350 500 667 278 333 556 556 556 556 260 556 333 737 370 556
        584 333 737 333 400 584 333 333 333 556 537 278 333 333 365 556 834 834 834 611
        667 667 667 667 667 667 1000 722 667 667 667 667 278 278 278 278 722 722 778 778
        778 778 778 584 778 722 722 722 722 667 667 611 556 556 556 556 556 556 889 500
        556 556 556 556 278 278 278 278 556 556 556 556 556 556 556 584 611 556 556 556
        556 500 556 500
        """),
    "Helvetica-Bold": _b("""
        278 333 474 556 556 889 722 238 333 333 389 584 278 333 278 278 556 556 556 556
        556 556 556 556 556 556 333 333 584 584 584 611 975 722 722 722 722 667 611 778
        722 278 556 722 611 833 722 778 667 778 722 667 611 722 667 944 667 667 611 333
        278 333 584 556 333 556 611 556 611 556 333 611 611 278 278 556 278 889 611 611
        611 611 389 556 333 611 556 778 556 556 500 389 280 389 584 350 556 350 278 556
        500 1000 556 556 333 1000 667 333 1000 350 611 350 350 278 278 500 500 350 556 1000
        333 1000 556 333 944 350 500 667 278 333 556 556 556 556 280 556 333 737 370 556
        584 333 737 333 400 584 333 333 333 611 556 278 333 333 365 556 834 834 834 611
        722 722 722 722 722 722 1000 722 667 667 667 667 278 278 278 278 722 722 778 778
        778 778 778 584 778 722 722 722 722 667 667 611 556 556 556 556 556 556 889 556
        556 556 556 556 278 278 278 278 611 611 611 611 611 611 611 584 611 611 611 611
        611 556 611 556
        """),
    "Helvetica-Oblique": _b("""
        278 278 355 556 556 889 667 191 333 333 389 584 278 333 278 278 556 556 556 556
        556 556 556 556 556 556 278 278 584 584 584 556 1015 667 667 722 722 667 611 778
        722 278 500 667 556 833 722 778 667 778 722 667 611 722 667 944 667 667 611 278
        278 278 469 556 333 556 556 500 556 556 278 556 556 222 222 500 222 833 556 556
        556 556 333 500 278 556 500 722 500 500 500 334 260 334 584 350 556 350 222 556
        333 1000 556 556 333 1000 667 333 1000 350 611 350 350 222 222 333 333 350 556 1000
        333 1000 500 333 944 350 500 667 278 333 556 556 556 556 260 556 333 737 370 556
        584 333 737 333 400 584 333 333 333 556 537 278 333 333 365 556 834 834 834 611
        667 667 667 667 667 667 1000 722 667 667 667 667 278 278 278 278 722 722 778 778
        778 778 778 584 778 722 722 722 722 667 667 611 556 556 556 556 556 556 889 500
        556 556 556 556 278 278 278 278 556 556 556 556 556 556 556 584 611 556 556 556
        556 500 556 500
        """),
    "Helvetica-BoldOblique": _b("""
        278 333 474 556 556 889 722 238 333 333 389 584 278 333 278 278 556 556 556 556
        556 556 556 556 556 556 333 333 584 584 584 611 975 722 722 722 722 667 611 778
        722 278 556 722 611 833 722 778 667 778 722 667 611 722 667 944 667 667 611 333
        278 333 584 556 333 556 611 556 611 556 333 611 611 278 278 556 278 889 611 611
        611 611 389 556 333 611 556 778 556 556 500 389 280 389 584 350 556 350 278 556
        500 1000 556 556 333 1000 667 333 1000 350 611 350 350 278 278 500 500 350 556 1000
        333 1000 556 333 944 350 500 667 278 333 556 556 556 556 280 556 333 737 370 556
        584 333 737 333 400 584 333 333 333 611 556 278 333 333 365 556 834 834 834 611
        722 722 722 722 722 722 1000 722 667 667 667 667 278 278 278 278 722 722 778 778
        778 778 778 584 778 722 722 722 722 667 667 611 556 556 556 556 556 556 889 556
        556 556 556 556 278 278 278 278 611 611 611 611 611 611 611 584 611 611 611 611
        611 556 611 556
        """),
    "Times-Roman": _b("""
        250 333 408 500 500 833 778 180 333 333 500 564 250 333 250 278 500 500 500 500
        500 500 500 500 500 500 278 278 564 564 564 444 921 722 667 667 722 611 556 722
        722 333 389 722 611 889 722 722 556 722 667 556 611 722 722 944 722 722 611 333
        278 333 469 500 333 444 500 444 500 444 333 500 500 278 278 500 278 778 500 500
        500 500 333 389 278 500 500 722 500 500 444 480 200 480 541 350 500 350 333 500
        444 1000 500 500 333 1000 556 333 889 350 611 350 350 333 333 444 444 350 500 1000
        333 980 389 333 722 350 444 722 250 333 500 500 500 500 200 500 333 760 276 500
        564 333 760 333 400 564 300 300 333 500 453 250 333 300 310 500 750 750 750 444
        722 722 722 722 722 722 889 667 611 611 611 611 333 333 333 333 722 722 722 722
        722 722 722 564 722 722 722 722 722 722 556 500 444 444 444 444 444 444 667 444
        444 444 444 444 278 278 278 278 500 500 500 500 500 500 500 564 500 500 500 500
        500 500 500 500
        """),
    "Times-Bold": _b("""
        250 333 555 500 500 1000 833 278 333 333 500 570 250 333 250 278 500 500 500 500
        500 500 500 500 500 500 333 333 570 570 570 500 930 722 667 722 722 667 611 778
        778 389 500 778 667 944 722 778 611 778 722 556 667 722 722 1000 722 722 667 333
        278 333 581 500 333 500 556 444 556 444 333 500 556 278 333 556 278 833 556 500
        556 556 444 389 333 556 500 722 500 500 444 394 220 394 520 350 500 350 333 500
        500 1000 500 500 333 1000 556 333 1000 350 667 350 350 333 333 500 500 350 500 1000
        333 1000 389 333 722 350 444 722 250 333 500 500 500 500 220 500 333 747 300 500
        570 333 747 333 400 570 300 300 333 556 540 250 333 300 330 500 750 750 750 500
        722 722 722 722 722 722 1000 722 667 667 667 667 389 389 389 389 722 722 778 778
        778 778 778 570 778 722 722 722 722 722 611 556 500 500 500 500 500 500 722 444
        444 444 444 444 278 278 278 278 500 556 500 500 500 500 500 570 500 556 556 556
        556 500 556 500
        """),
    "Times-Italic": _b("""
        250 333 420 500 500 833 778 214 333 333 500 675 250 333 250 278 500 500 500 500
        500 500 500 500 500 500 333 333 675 675 675 500 920 611 611 667 722 611 611 722
        722 333 444 667 556 833 667 722 611 722 611 500 556 722 611 833 611 556 556 389
        278 389 422 500 333 500 500 444 500 444 278 500 500 278 278 444 278 722 500 500
        500 500 389 389 278 500 444 667 444 444 389 400 275 400 541 350 500 350 333 500
        556 889 500 500 333 1000 500 333 944 350 556 350 350 333 333 556 556 350 500 889
        333 980 389 333 667 350 389 556 250 389 500 500 500 500 275 500 333 760 276 500
        675 333 760 333 400 675 300 300 333 500 523 250 333 300 310 500 750 750 750 500
        611 611 611 611 611 611 889 667 611 611 611 611 333 333 333 333 722 667 722 722
        722 722 722 675 722 722 722 722 722 556 611 500 500 500 500 500 500 500 667 444
        444 444 444 444 278 278 278 278 500 500 500 500 500 500 500 675 500 500 500 500
        500 444 500 444
        """),
    "Times-BoldItalic": _b("""
        250 389 555 500 500 833 778 278 333 333 500 570 250 333 250 278 500 500 500 500
        500 500 500 500 500 500 333 333 570 570 570 500 832 667 667 667 722 667 667 722
        778 389 500 667 611 889 722 722 611 722 667 556 611 722 667 889 667 611 611 333
        278 333 570 500 333 500 500 444 500 444 333 500 556 278 278 500 278 778 556 500
        500 500 389 389 278 556 444 667 500 444 389 348 220 348 570 350 500 350 333 500
        500 1000 500 500 333 1000 556 333 944 350 611 350 350 333 333 500 500 350 500 1000
        333 1000 389 333 722 350 389 611 250 389 500 500 500 500 220 500 333 747 266 500
        606 333 747 333 400 570 300 300 333 576 500 250 333 300 300 500 750 750 750 500
        667 667 667 667 667 667 944 667 667 667 667 667 389 389 389 389 722 722 722 722
        722 722 722 570 722 722 722 722 722 611 611 500 500 500 500 500 500 500 722 444
        444 444 444 444 278 278 278 278 500 556 500 500 500 500 500 570 500 556 556 556
        556 444 500 444
        """),
}


FAMILIEN = (("Helvetica", "Helvetica-Bold", "Helvetica-Oblique", "Helvetica-BoldOblique"),
            ("Times-Roman", "Times-Bold", "Times-Italic", "Times-BoldItalic"),
            ("Courier", "Courier-Bold", "Courier-Oblique", "Courier-BoldOblique"))
GROESSEN = (10, 12, 18, 24)             # pt, wie am Schirm
MASS = 1.5                              # pt je Schirmpunkt
RAND_L = (A4_B - 302 * MASS) / 2        # Textrahmen 302 Punkte, mittig


def wrl_schrift(fmt):
    """Format-Byte von WRITE -> (Grundschrift, Groesse in pt)"""
    fam = min((fmt >> 2) & 3, 2)
    stil = (1 if fmt & 0x10 else 0) + (2 if fmt & 0x20 else 0)
    return FAMILIEN[fam][stil], GROESSEN[fmt & 3]


def wrl_breite(name, text):
    """natuerliche Breite in 1/1000 der Groesse"""
    if name.startswith("Courier"):
        return 600 * len(text)
    b = BREITEN[name]
    return sum(b[c - 32] if c >= 32 else b[0] for c in text)


def scharf(b, h, punkte, k):
    """Punkte ganzzahlig k-fach vergroessern (Drucker rechnen sonst weich)"""
    gross = bytearray()
    for y in range(h):
        z = bytes(c for c in punkte[y * b:(y + 1) * b] for _ in range(k))
        gross += z * k
    return bytes(gross)


def ppm_lesen(roh):
    """P6 von bildwandler -> (breite, hoehe, RGB)"""
    teile = roh.split(b"\n", 3)
    if teile[0] != b"P6" or len(teile) < 4:
        raise ValueError("NOT A PPM FILE")
    b, h = (int(t) for t in teile[1].split())
    return b, h, teile[3][:3 * b * h]


# CALC rechnet in Latin-9 (Euro auf $A4), PDF mit WinAnsi
LATIN9 = {0xA4: 0x80, 0xA6: 0x8A, 0xA8: 0x9A, 0xB4: 0x8E, 0xB8: 0x9E, 0xBC: 0x8C, 0xBD: 0x9C, 0xBE: 0x9F}
ZELL_GR = 9                             # Zelltext von CALC: Helvetica 9 pt
ZELL_RAND = 2.2                         # pt Abstand zum Gitter
ZELL_FARBE = (b"0 g", b"0 0 0.7 rg", b"0.8 0 0 rg", b"0 g")


def zelle_pdf(x, y, w, art, text, mm=None):
    """Satz 7 (CALC): Text in einer Zelle, x/y/w in Schirmpunkten, y = Oberkante.
    mm = Schriftgroesse: x/y/w in 1/10 mm ab dem Blattrand (Satz 8, FILE)"""
    text = bytes(LATIN9.get(c, c) for c in text)
    if mm:
        gr, rand, links, oben, breite = mm, 0.0, x * PT_MM, A4_H - y * PT_MM, w * PT_MM
        hoehe, grund_y = mm * 1.3, mm * 1.0
    else:
        gr, rand, links, oben, breite = ZELL_GR, ZELL_RAND, RAND_L + x * MASS, A4_H - y * MASS, w * MASS
        hoehe, grund_y = P_ZH * MASS, 10.6
    natur = wrl_breite("Helvetica", text) * gr / 1000
    platz = max(1.0, breite - 2 * rand)
    tz = 1.0 if natur <= platz else max(platz / natur, 0.75)
    breit = natur * tz
    ausr = art & 3
    if ausr == 3:
        tx = links + breite - rand - breit
    elif ausr == 2:
        tx = links + (breite - breit) / 2
    else:
        tx = links + rand
    tx = max(tx, links + rand)          # zu breit: von links, rechts abgeschnitten
    grund = b""
    if art & 0x10:                      # laeuft in die Nachbarn: das Gitter darunter weg
        grund = b"q 1 g %.2f %.2f %.2f %.2f re f Q\n" % (links + 0.4, oben - hoehe + 0.4,
                                                         breite - 0.8, hoehe - 0.8)
    return grund + (b"q %.2f %.2f %.2f %.2f re W n BT %s /H %d Tf %.1f Tz %.2f %.2f Td " % (
        links, oben - hoehe, breite, hoehe, ZELL_FARBE[art >> 2 & 3], gr, tz * 100,
        tx, oben - grund_y) + text_pdf(text) + b" Tj ET Q\n")


P_ZH = 10                               # Zeilenhoehe von CALC (Schirmpunkte)
PT_MM = 72 / 254                        # pt je 1/10 mm (Satz 8)

# ---------------------------------------------------------------- DRAW

D_FARBEN = (0x000, 0x125, 0x725, 0x085, 0xa53, 0x554, 0xccc, 0xffe,     # Plaetze 48-63 ($0RGB)
            0xf05, 0xfa0, 0xfe2, 0x0e3, 0x2af, 0x879, 0xf7a, 0xfca)
D_MUSTER = ((0xff,) * 8,
            (0xaa, 0x55) * 4,
            (0x88, 0x00, 0x22, 0x00) * 2,
            (0xff, 0x00, 0x00, 0x00) * 2,
            (0x88,) * 8,
            (0x03, 0x06, 0x0c, 0x18, 0x30, 0x60, 0xc0, 0x81),
            (0xc0, 0x60, 0x30, 0x18, 0x0c, 0x06, 0x03, 0x81),
            (0xff, 0x80, 0x80, 0x80, 0xff, 0x08, 0x08, 0x08))
D_PUNKT_MM = 0.4                        # ein Punkt eines Musters auf Papier (mm)
D_STAERKE = (0.3, 1.0, 2.0, 4.0)        # pt: fein, 1, 2, 4
D_PFEIL = (40, 45, 60, 80)              # Laenge der Spitze (1/10 mm) nach der Staerke
D_BEZ = 0.5522847                       # Viertelkreis als Bezier-Kurve


def d_rgb(platz):
    """Farbe 0-15 (Platz 48-63 am Schirm) -> (r, g, b) 0-1; $FF (keine) -> None"""
    if platz > 15:
        return None
    w = D_FARBEN[platz]
    return ((w >> 8 & 15) / 15, (w >> 4 & 15) / 15, (w & 15) / 15)


def d_ellipse(x0, y0, x1, y1):
    """Ellipse im Kasten (pt) als vier Bezier-Kurven"""
    cx, cy, rx, ry = (x0 + x1) / 2, (y0 + y1) / 2, abs(x1 - x0) / 2, abs(y1 - y0) / 2
    kx, ky = rx * D_BEZ, ry * D_BEZ
    return (b"%.2f %.2f m " % (cx + rx, cy)
            + b"%.2f %.2f %.2f %.2f %.2f %.2f c " % (cx + rx, cy + ky, cx + kx, cy + ry, cx, cy + ry)
            + b"%.2f %.2f %.2f %.2f %.2f %.2f c " % (cx - kx, cy + ry, cx - rx, cy + ky, cx - rx, cy)
            + b"%.2f %.2f %.2f %.2f %.2f %.2f c " % (cx - rx, cy - ky, cx - kx, cy - ry, cx, cy - ry)
            + b"%.2f %.2f %.2f %.2f %.2f %.2f c h\n" % (cx + kx, cy - ry, cx + rx, cy - ky, cx + rx, cy))


def d_rund(x0, y0, x1, y1, r):
    """Rechteck mit runden Ecken (pt, x0 < x1, y0 < y1)"""
    r = max(0.0, min(r, (x1 - x0) / 2, (y1 - y0) / 2))
    k = r * (1 - D_BEZ)
    return (b"%.2f %.2f m %.2f %.2f l " % (x0 + r, y0, x1 - r, y0)
            + b"%.2f %.2f %.2f %.2f %.2f %.2f c " % (x1 - k, y0, x1, y0 + k, x1, y0 + r)
            + b"%.2f %.2f l %.2f %.2f %.2f %.2f %.2f %.2f c " % (x1, y1 - r, x1, y1 - k, x1 - k, y1, x1 - r, y1)
            + b"%.2f %.2f l %.2f %.2f %.2f %.2f %.2f %.2f c " % (x0 + r, y1, x0 + k, y1, x0, y1 - k, x0, y1 - r)
            + b"%.2f %.2f l %.2f %.2f %.2f %.2f %.2f %.2f c h\n" % (x0, y0 + r, x0, y0 + k, x0 + k, y0, x0 + r, y0))


def d_spitze(tip, von, laenge):
    """Pfeilspitze bei tip (1/10 mm) aus Richtung von: (Dreieck, Fuss) oder None"""
    dx, dy = tip[0] - von[0], tip[1] - von[1]
    d = (dx * dx + dy * dy) ** 0.5
    if d < 1:
        return None
    l = min(laenge, d)
    ux, uy = dx * l / d, dy * l / d
    fuss = (tip[0] - ux, tip[1] - uy)
    hx, hy = -uy / 2, ux / 2
    return (tip, (fuss[0] + hx, fuss[1] + hy), (fuss[0] - hx, fuss[1] - hy)), fuss


def d_form_pdf(pdf, form, punkte, seite_h):
    """Satz 9: eine Form von DRAW (32 Bytes wie im Speicher, Masse in 1/10 mm)"""
    art, schalter, lf, ff, st, la, mu = form[:7]
    x1, y1, x2, y2 = struct.unpack_from("<4H", form, 8)

    def p(x, y):
        return x * PT_MM, seite_h - y * PT_MM
    linie, grund = d_rgb(lf), d_rgb(ff)
    pfad, zu, ecken = b"", True, []
    if art == 1:
        ecken, zu = [(x1, y1), (x2, y2)], False
    elif art == 2 or art == 6:
        pfad = b"%.2f %.2f %.2f %.2f re\n" % (x1 * PT_MM, seite_h - y2 * PT_MM, (x2 - x1) * PT_MM, (y2 - y1) * PT_MM)
    elif art == 3:
        pfad = d_ellipse(*p(x1, y1), *p(x2, y2))
    elif art == 4:
        r, = struct.unpack_from("<H", form, 16)
        a, b = p(x1, y2)
        c, d = p(x2, y1)
        pfad = d_rund(a, b, c, d, r * PT_MM)
    elif art == 5:
        w, h = x2 - x1, y2 - y1
        ecken = [(x1 + (punkte[2 * k] * w + 0x8000 >> 16), y1 + (punkte[2 * k + 1] * h + 0x8000 >> 16))
                 for k in range(len(punkte) // 2)]
        zu = bool(schalter & 4)
    else:
        return b""
    spitzen = []
    if ecken:
        if not zu and linie and schalter & 3 and len(ecken) >= 2:
            l = D_PFEIL[st & 3]
            if schalter & 1:
                s = d_spitze(ecken[0], ecken[1], l)
                if s:
                    spitzen.append(s[0])
                    ecken[0] = s[1]
            if schalter & 2:
                s = d_spitze(ecken[-1], ecken[-2], l)
                if s:
                    spitzen.append(s[0])
                    ecken[-1] = s[1]
        pfad = b"%.2f %.2f m " % p(*ecken[0]) + b"".join(b"%.2f %.2f l " % p(*e) for e in ecken[1:])
        pfad += b"h\n" if zu else b"\n"
    aus = b"q\n"
    if grund and zu:                    # Fuellung (bei Text der Grund)
        if mu & 7:
            aus += b"/Pattern cs /%s scn\n" % pdf.kachel(mu & 7, grund).encode()
        else:
            aus += b"%.3f %.3f %.3f rg\n" % grund
        aus += pfad + b"f\n"
    if linie and art != 6:
        w = D_STAERKE[st & 3]
        aus += b"%.3f %.3f %.3f RG %.2f w 0 j\n" % (linie + (w,))
        if la == 1:
            aus += b"[%.2f %.2f] 0 d 0 J\n" % (max(3.0, 3 * w), max(2.0, 2 * w))
        elif la == 2:
            aus += b"[0 %.2f] 0 d 1 J\n" % max(2.0, 2 * w)
        aus += pfad + b"S\n"
        if spitzen:
            aus += b"[] 0 d %.3f %.3f %.3f rg\n" % linie
            for t in spitzen:
                aus += b"%.2f %.2f m %.2f %.2f l %.2f %.2f l h f\n" % (p(*t[0]) + p(*t[1]) + p(*t[2]))
    return aus + b"Q\n"


def d_text_pdf(pdf, x, y, w, fmt, farbe, text, seite_h):
    """Satz 11: Textzeile von DRAW; x links, y Grundlinie, w Breite (1/10 mm), Format
    wie bei WRITE (Bit 6: unterstrichen), Farbe 0-15"""
    name, gr = wrl_schrift(fmt & 0x3f)
    rgb = d_rgb(farbe) or (0, 0, 0)
    natur = wrl_breite(name, text) * gr / 1000
    ziel = w * PT_MM
    tz = min(max(ziel / natur, 0.85), 1.18) if natur else 1.0
    tc = (ziel - natur * tz) / (max(1, len(text)) * tz)
    xx, yy = x * PT_MM, seite_h - y * PT_MM
    aus = (b"%.3f %.3f %.3f rg BT /%s %d Tf %.1f Tz %.3f Tc %.2f %.2f Td " % (rgb + (pdf.schrift(name).encode(), gr, tz * 100, tc, xx, yy))
           + text_pdf(text) + b" Tj ET\n")
    if fmt & 0x40 and w:
        aus += b"%.3f %.3f %.3f RG [] 0 d %.2f w %.2f %.2f m %.2f %.2f l S\n" % (
            rgb + (gr * 0.06, xx, yy - gr * 0.12, xx + ziel, yy - gr * 0.12))
    return aus


def wrl_zu_pdf(roh, titel="", bild=None):
    """Zeilenliste von WRITE (Fassung 1) oder CALC (Fassung 2) -> PDF. Jedes
    Wort steht, wo es am Schirm steht, und wird ueber den Zeichenabstand genau
    auf seine Breite am Schirm gebracht; die Schrift ist die passende
    PDF-Grundschrift. bild(name, b, h) liefert ein Bild fuer Satzart 5
    (("idx", b, h, palette, punkte) oder ("rgb", b, h, daten)) oder None -
    dann steht dort ein grauer Kasten. Fassung 2 hat oben auf jeder Seite
    TITEL und die Seitenzahl, dazu Zelltexte (7) und Linien (6)."""
    if roh[:4] != b"MWRL" or roh[4] not in (1, 2):
        raise ValueError("NOT A WRITE PRINT LIST")
    tabelle = roh[4] == 2
    mm, kopf_an = None, True            # Satz 8 (FILE): 1/10 mm, ohne Kopfzeile
    seite_b, seite_h = A4_B, A4_H       # Satz 8, Bit 0 im vierten Byte: quer (DRAW)
    seiten_n, = struct.unpack_from("<H", roh, 6)
    pdf = Pdf()
    seiten = [bytearray() for _ in range(max(1, seiten_n))]
    s, y, i = seiten[0], 0, 8
    while i < len(roh):
        art = roh[i]
        i += 1
        if art == 0:
            break
        if art == 1:
            nr, = struct.unpack_from("<H", roh, i)
            i += 2
            while nr >= len(seiten):
                seiten.append(bytearray())
            s = seiten[nr]
        elif art == 2:
            y, = struct.unpack_from("<H", roh, i)
            i += 2
        elif art == 3:
            x, w, fmt, n = struct.unpack_from("<HHBB", roh, i)
            i += 6
            text = roh[i:i + n]
            i += n
            name, gr = wrl_schrift(fmt)
            natur = wrl_breite(name, text) * gr / 1000
            ziel = w * MASS
            # erst die Zeichen leicht breiter oder schmaler (bis 18 %), den Rest
            # ueber den Zeichenabstand (der wird mit gestreckt)
            tz = min(max(ziel / natur, 0.85), 1.18) if natur else 1.0
            tc = (ziel - natur * tz) / (max(1, n) * tz)
            s += (b"BT /%s %d Tf %.1f Tz %.3f Tc %.2f %.2f Td " % (pdf.schrift(name).encode(), gr, tz * 100, tc,
                                                                  RAND_L + x * MASS, A4_H - y * MASS)
                  + text_pdf(text) + b" Tj ET\n")
        elif art == 4:
            x, w, fmt = struct.unpack_from("<HHB", roh, i)
            i += 5
            gr = GROESSEN[fmt & 3]
            yy = A4_H - y * MASS - gr * 0.12
            s += b"%.2f w %.2f %.2f m %.2f %.2f l S\n" % (gr * 0.06, RAND_L + x * MASS, yy,
                                                          RAND_L + (x + w) * MASS, yy)
        elif art == 8 and tabelle:      # Seite einrichten (FILE)
            einheit, ohne_kopf, gr, lage = roh[i], roh[i + 1], roh[i + 2], roh[i + 3]
            i += 4
            mm = (gr or ZELL_GR) if einheit == 1 else None
            kopf_an = not ohne_kopf
            if lage & 1:
                seite_b, seite_h = A4_H, A4_B
        elif art == 9 and tabelle:      # Form von DRAW: 32 Bytes, Zahl der Punkte, Punkte
            form = roh[i:i + 32]
            n, = struct.unpack_from("<H", roh, i + 32)
            punkte = struct.unpack_from("<%dH" % (2 * n), roh, i + 34)
            i += 34 + 4 * n
            s += d_form_pdf(pdf, form, punkte, seite_h)
        elif art == 11 and tabelle:     # Textzeile von DRAW
            x, yy, w, fmt, farbe, n = struct.unpack_from("<HHHBBB", roh, i)
            i += 9
            s += d_text_pdf(pdf, x, yy, w, fmt, farbe, roh[i:i + n], seite_h)
            i += n
        elif art == 7 and tabelle:      # Zelltext (CALC)
            x, w, f, n = struct.unpack_from("<HHBB", roh, i)
            i += 6
            s += zelle_pdf(x, y, w, f, roh[i:i + n], mm)
            i += n
        elif art == 6 and tabelle:      # Linie (CALC): Bit 0 senkrecht, Bit 1 dunkel
            x, yy, l, f = struct.unpack_from("<HHHB", roh, i)
            i += 7
            if mm:
                x0, y0, ll = x * PT_MM, A4_H - yy * PT_MM, l * PT_MM
            else:
                x0, y0, ll = RAND_L + x * MASS, A4_H - yy * MASS, l * MASS
            x1, y1 = (x0, y0 - ll) if f & 1 else (x0 + ll, y0)
            s += b"%s G %s w %.2f %.2f m %.2f %.2f l S\n" % (b"0.15" if f & 2 else b"0.5",
                                                            b"1" if f & 2 else b"0.6", x0, y0, x1, y1)
        elif art == 5:                  # Bild: unten auf der Grundlinie
            x, w, h, n = struct.unpack_from("<HHHB", roh, i)
            i += 7
            name = roh[i:i + n].decode("latin-1")
            i += n
            bx, by, bw, bh = RAND_L + x * MASS, A4_H - y * MASS, w * MASS, h * MASS
            kurz = "B%d" % (len(pdf.bilder) + 1)
            daten = None
            try:
                daten = bild(name, w, h) if bild else None
            except Exception as e:      # ein fehlendes Bild verdirbt nicht den Druck
                print("DRUCK: Bild", name, e, flush=True)
            if daten and daten[0] == "idx":
                _, b, hh, pal, punkte = daten
                k = max(1, min(8, round(bw / b * 300 / 72)))
                pdf.bild(kurz, b * k, hh * k, pal, scharf(b, hh, punkte, k))
            elif daten:
                _, b, hh, rgb = daten
                pdf.bild_rgb(kurz, b, hh, rgb)
            if daten:
                s += b"q %.2f 0 0 %.2f %.2f %.2f cm /%s Do Q\n" % (bw, bh, bx, by, kurz.encode())
            else:
                s += b"q 0.8 g %.2f %.2f %.2f %.2f re f Q\n" % (bx, by, bw, bh)
        else:
            raise ValueError("BAD PRINT LIST")
    for nr, inhalt in enumerate(seiten, 1):
        kopf = kopfzeile(titel, nr, len(seiten)) if tabelle and kopf_an else b""
        pdf.seite(kopf + b"0 g 0 G 0 J\n" + bytes(inhalt), seite_b, seite_h)
    return pdf.fertig(titel), len(seiten)


def bmp_lesen(roh):
    """unkomprimiertes BMP -> (breite, hoehe, palette 256xRGB, Punkte von oben)"""
    if roh[:2] != b"BM":
        raise ValueError("NOT A BMP FILE")
    daten, = struct.unpack_from("<I", roh, 10)
    kopf, b, h, _, bits, art = struct.unpack_from("<IiiHHI", roh, 14)
    if art not in (0, 3) or bits not in (1, 4, 8, 24):
        raise ValueError("ONLY UNCOMPRESSED BMP")
    oben = h < 0
    h = abs(h)
    zeile = (b * bits + 31) // 32 * 4
    if bits == 24:                      # auf 256 Farben bringen wie der MERIDIAN (4 Bit je Anteil)
        farben, punkte = {}, bytearray()
        for y in range(h):
            q = daten + zeile * (y if oben else h - 1 - y)
            for x in range(b):
                bl, g, r = roh[q + 3 * x:q + 3 * x + 3]
                f = (r & 0xf0 | r >> 4, g & 0xf0 | g >> 4, bl & 0xf0 | bl >> 4)
                if f not in farben:
                    if len(farben) == 256:
                        raise ValueError("TOO MANY COLOURS")
                    farben[f] = len(farben)
                punkte.append(farben[f])
        pal = bytearray(768)
        for f, i in farben.items():
            pal[3 * i:3 * i + 3] = bytes(f)
        return b, h, bytes(pal), bytes(punkte)
    n, = struct.unpack_from("<I", roh, 46)
    n = n or 1 << bits
    pal = bytearray(768)
    for i in range(min(n, 256)):
        bl, g, r = roh[14 + kopf + 4 * i:14 + kopf + 4 * i + 3]
        pal[3 * i:3 * i + 3] = bytes((r, g, bl))
    punkte = bytearray()
    for y in range(h):
        q = daten + zeile * (y if oben else h - 1 - y)
        z = roh[q:q + zeile]
        if bits == 8:
            punkte += z[:b]
        elif bits == 4:
            punkte += bytes(z[x >> 1] >> 4 if x % 2 == 0 else z[x >> 1] & 15 for x in range(b))
        else:
            punkte += bytes(z[x >> 3] >> (7 - x % 8) & 1 for x in range(b))
    return b, h, bytes(pal), bytes(punkte)


def bild_zu_pdf(roh, titel=""):
    """BMP -> PDF: Bild oben mittig, so gross es auf A4 passt, jeder Punkt
    als scharfes Quadrat (ganzzahlig vergroessert, Interpolate aus)"""
    b, h, pal, punkte = bmp_lesen(roh)
    breit_max, hoch_max = A4_B - 2 * 56, 760 - 56
    mass = min(breit_max / b, hoch_max / h)    # Punkte (1/72") je Bildpunkt
    k = max(1, min(16, round(mass * 300 / 72 / 1.0)))  # etwa 300 dpi
    k = max(1, min(k, 8000 // max(b, h)))
    pdf = Pdf()
    pdf.bild("B", b * k, h * k, pal, scharf(b, h, punkte, k))
    w, hh = b * mass, h * mass
    x, y = (A4_B - w) / 2, 780 - hh
    s = kopfzeile(titel, 1, 1) + b"q %.2f 0 0 %.2f %.2f %.2f cm /B Do Q\n" % (w, hh, x, y)
    pdf.seite(s)
    return pdf.fertig(titel), 1


# ====================================================================== IPP

def ipp_attr(tag, name, wert):
    n = name.encode()
    w = wert if isinstance(wert, bytes) else wert.encode("utf-8")
    return struct.pack(">BH", tag, len(n)) + n + struct.pack(">H", len(w)) + w


def ipp_anfrage(uri, op, extra=b"", daten=b"", zeit=ZEIT):
    """IPP-Anfrage an uri (ipp://host[:port]/pfad) -> (Status, Attribute als dict)"""
    rest = uri.split("://", 1)[1]
    hostport, _, pfad = rest.partition("/")
    host, _, port = hostport.partition(":")
    kopf = struct.pack(">HHI", 0x0101, op, random.randint(1, 0x7fffffff))
    kopf += b"\x01" + ipp_attr(0x47, "attributes-charset", "utf-8") \
        + ipp_attr(0x48, "attributes-natural-language", "en") \
        + ipp_attr(0x45, "printer-uri", uri) + extra + b"\x03"
    socket.create_connection((host, int(port or 631)), timeout=5).close()   # schnell sagen, wenn keiner da ist
    v = http.client.HTTPConnection(host, int(port or 631), timeout=zeit)
    try:
        v.request("POST", "/" + pfad, body=kopf + daten,
                  headers={"Content-Type": "application/ipp"})
        antwort = v.getresponse()
        roh = antwort.read()
        if antwort.status != 200:
            raise RuntimeError("PRINTER SAYS HTTP %d" % antwort.status)
    finally:
        v.close()
    status, = struct.unpack_from(">H", roh, 2)
    attr, i, name = {}, 8, ""
    while i < len(roh):
        tag = roh[i]
        i += 1
        if tag == 3:
            break
        if tag < 0x10:                  # neue Gruppe
            continue
        nl, = struct.unpack_from(">H", roh, i)
        n = roh[i + 2:i + 2 + nl].decode("latin-1")
        i += 2 + nl
        wl, = struct.unpack_from(">H", roh, i)
        w = roh[i + 2:i + 2 + wl]
        i += 2 + wl
        name = n or name
        attr.setdefault(name, []).append(w)
    return status, attr


IPP_FEHLER = {0x040A: "PRINTER CANNOT PRINT PDF", 0x0400: "PRINTER REFUSED THE JOB",
              0x0401: "PRINTER WANTS A LOGIN", 0x0403: "PRINTER REFUSED THE JOB",
              0x0405: "WRONG PRINTER ADDRESS", 0x0507: "PRINTER IS BUSY", 0x0506: "PRINTER IS BUSY",
              0x0500: "PRINTER ERROR"}


def drucken(uri, pdf, titel, nur_pruefen=False):
    """PDF an den Drucker (Print-Job; nur_pruefen: Validate-Job, ohne Papier)"""
    extra = ipp_attr(0x42, "requesting-user-name", "MERIDIAN") \
        + ipp_attr(0x42, "job-name", titel or "MERIDIAN") \
        + ipp_attr(0x49, "document-format", "application/pdf")
    status, attr = ipp_anfrage(uri, 0x0004 if nur_pruefen else 0x0002, extra,
                               b"" if nur_pruefen else pdf)
    if status >= 0x0100 and status != 0x0001:
        raise RuntimeError(IPP_FEHLER.get(status, "PRINTER ERROR %04X" % status))
    return attr


def drucker_name(uri):
    try:
        status, attr = ipp_anfrage(uri, 0x000B, ipp_attr(0x44, "requested-attributes", "printer-make-and-model")
                                   + ipp_attr(0x44, "", "document-format-supported"), zeit=8)
    except Exception:
        return None, None
    name = attr.get("printer-make-and-model", [b""])[0].decode("utf-8", "replace")
    formate = [f.decode() for f in attr.get("document-format-supported", [])]
    return name, formate


# ====================================================================== Bonjour

def mdns_suchen(zeit=2.0):
    """_ipp._tcp im Heimnetz suchen -> Liste von ipp://adresse:port/pfad"""
    def name_lesen(roh, i):
        teile, sprung = [], None
        while True:
            l = roh[i]
            if l == 0:
                i += 1
                break
            if l & 0xc0 == 0xc0:
                ziel = struct.unpack_from(">H", roh, i)[0] & 0x3fff
                if sprung is None:
                    sprung = i + 2
                i = ziel
                continue
            teile.append(roh[i + 1:i + 1 + l].decode("utf-8", "replace"))
            i += 1 + l
        return ".".join(teile).lower(), (sprung if sprung is not None else i)

    frage = struct.pack(">HHHHHH", 0, 0, 1, 0, 0, 0)
    for t in ("_ipp", "_tcp", "local"):
        frage += bytes([len(t)]) + t.encode()
    frage += b"\0" + struct.pack(">HH", 12, 0x8001)     # PTR, Antwort direkt an uns
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_TTL, 255)
    s.settimeout(0.3)
    ptr, srv, adr, txt = [], {}, {}, {}
    try:
        s.sendto(frage, ("224.0.0.251", 5353))
        ende = time.time() + zeit
        while time.time() < ende:
            try:
                roh, absender = s.recvfrom(9000)
            except socket.timeout:
                continue
            try:
                _, _, qd, an, ns, ar = struct.unpack_from(">HHHHHH", roh, 0)
                i = 12
                for _ in range(qd):
                    _, i = name_lesen(roh, i)
                    i += 4
                for _ in range(an + ns + ar):
                    n, i = name_lesen(roh, i)
                    typ, _, _, l = struct.unpack_from(">HHIH", roh, i)
                    i += 10
                    d = roh[i:i + l]
                    if typ == 12:
                        ptr.append(name_lesen(roh, i)[0])
                    elif typ == 33:
                        srv[n] = (struct.unpack_from(">H", d, 4)[0], name_lesen(roh, i + 6)[0])
                    elif typ == 1 and l == 4:
                        adr[n] = socket.inet_ntoa(d)
                    elif typ == 16:
                        j, werte = 0, {}
                        while j < len(d):
                            e = d[j + 1:j + 1 + d[j]].decode("utf-8", "replace")
                            j += 1 + d[j]
                            k, _, v = e.partition("=")
                            werte[k.lower()] = v
                        txt[n] = werte
                    i += l
            except (struct.error, IndexError):
                continue
    finally:
        s.close()
    gefunden = []
    for p in dict.fromkeys(ptr):
        if p in srv:
            port, ziel = srv[p]
            ip = adr.get(ziel)
            if ip:
                pfad = txt.get(p, {}).get("rp", "ipp/print")
                gefunden.append("ipp://%s:%d/%s" % (ip, port, pfad))
    return gefunden


def adresse_zu_uri(a):
    a = a.strip()
    if "://" in a:
        return a.replace("ipps://", "ipp://", 1)
    if "/" in a:
        return "ipp://" + a
    return "ipp://%s/ipp/print" % a


# ====================================================================== Dienst

def bildwandler():
    try:
        import bild_dienst
        return bild_dienst.wandler_finden()
    except ImportError:
        return None


class Druck:
    def __init__(self, datei_pfad, dateien, cache="/tmp/meridian_bild"):
        self.datei_pfad = datei_pfad    # "NAME.EXT" (8.3) -> Pfad auf Laufwerk 3, auch ~DRUCK.TXT / ~MALEN.BMP
        self.dateien = dateien          # () -> Ordner von Laufwerk 3
        self.cache = cache
        self.lock = threading.Lock()
        self.zustand, self.ergebnis = 0, b""
        self.drucker = None             # (Quelle der Adresse, uri)

    def pfad(self, name):
        os.makedirs(self.cache, exist_ok=True)
        return os.path.join(self.cache, name)

    def drucker_finden(self):
        """-> uri; PRINTER.TXT auf Laufwerk 3, sonst Bonjour (gemerkt)"""
        datei = self.datei_pfad("PRINTER.TXT")
        if datei:
            with open(datei, "rb") as f:
                for z in f.read().decode("latin-1").splitlines():
                    z = z.strip()
                    if z and not z.startswith("#"):
                        return adresse_zu_uri(z)
        if self.drucker:
            return self.drucker
        for uri in mdns_suchen():
            name, formate = drucker_name(uri)
            if formate and "application/pdf" in formate:
                self.drucker = uri
                print("DRUCK: gefunden", uri, name, flush=True)
                return uri
        raise RuntimeError("NO PRINTER FOUND - SEE PRINTER.TXT")

    def anfrage(self, text):
        teile = text.split(b"\0")[0].decode("latin-1").split()
        if not teile:
            return -1, b"BAD COMMAND"
        befehl = teile[0].upper()
        if befehl == "STAND":
            with self.lock:
                return self.zustand, self.ergebnis
        if befehl not in ("DRUCKE", "PDF", "DRUCKER"):
            return -1, b"BAD COMMAND"
        with self.lock:
            if self.zustand == 1:
                return -1, b"BUSY"
            self.zustand, self.ergebnis = 1, b"BUSY"
        if befehl == "DRUCKER":
            auftrag = (self.nennen,)
        else:
            if len(teile) < 2 or (befehl == "PDF" and (len(teile) < 3 or not teile[2].upper().endswith(".PDF"))):
                with self.lock:
                    self.zustand, self.ergebnis = 0, b""
                return -1, b"BAD COMMAND"
            quelle = teile[1].upper()
            ziel = teile[2].upper() if befehl == "PDF" else None
            titel = " ".join(teile[3 if befehl == "PDF" else 2:])
            auftrag = (self.lauf, quelle, ziel, titel)
        threading.Thread(target=self.sicher, args=auftrag, daemon=True).start()
        return 0, b""

    def sicher(self, f, *args):
        try:
            ergebnis = f(*args)
            print("DRUCK:", ergebnis, flush=True)
            with self.lock:
                self.zustand, self.ergebnis = 2, ergebnis.upper().encode("latin-1", "replace")[:40]
        except Exception as e:
            text = str(e) if isinstance(e, (RuntimeError, ValueError)) else "%s: %s" % (type(e).__name__, e)
            print("DRUCK: Fehler:", e, flush=True)
            with self.lock:
                self.zustand, self.ergebnis = 3, text.upper().encode("latin-1", "replace")[:40]

    def nennen(self):
        uri = self.drucker_finden()
        name, formate = drucker_name(uri)
        if formate is None:
            raise RuntimeError("PRINTER DOES NOT ANSWER")
        return name or uri.split("://")[1].split("/")[0]

    def wrl_bild(self, name, w, h):
        """Bild fuer WRITE: das Original von Laufwerk 3 in Druckaufloesung"""
        pfad = self.datei_pfad(name.upper())
        if not pfad or not os.path.isfile(pfad):
            return None
        with open(pfad, "rb") as f:
            roh = f.read()
        if roh[:2] == b"BM" and struct.unpack_from("<H", roh, 28)[0] <= 8:
            b, hh, pal, punkte = bmp_lesen(roh)     # Pixelbild (PAINT): scharf
            return ("idx", b, hh, pal, punkte)
        wandler = bildwandler()
        if not wandler:
            return None
        ziel = self.pfad("druck_bild.ppm")
        r = subprocess.run([wandler, "ppm", pfad, ziel, "--breite", str(w * 6), "--hoehe", str(h * 6)],
                           capture_output=True, timeout=ZEIT)
        if r.returncode:
            return None
        with open(ziel, "rb") as f:
            b, hh, rgb = ppm_lesen(f.read())
        return ("rgb", b, hh, rgb)

    def lauf(self, quelle, ziel, titel):
        pfad = self.datei_pfad(quelle)
        if not pfad or not os.path.isfile(pfad):
            raise RuntimeError("FILE NOT FOUND")
        with open(pfad, "rb") as f:
            roh = f.read()
        end = quelle.rsplit(".", 1)[-1]
        if end == "BMP":
            pdf, seiten = bild_zu_pdf(roh, titel)
        elif end == "WRL":
            pdf, seiten = wrl_zu_pdf(roh, titel, self.wrl_bild)
        elif end in ("TXT", "BAS", "ASM", "MD", "CSV", ""):
            pdf, seiten = text_zu_pdf(roh, titel)
        else:
            raise RuntimeError("ONLY TEXT, BMP AND WRITE")
        if ziel:
            datei = self.datei_pfad(ziel) or os.path.join(self.dateien(), ziel)
            with open(datei + ".neu", "wb") as f:
                f.write(pdf)
            os.replace(datei + ".neu", datei)
            return "SAVED %s (%d PAGE%s)" % (ziel, seiten, "" if seiten == 1 else "S")
        uri = self.drucker_finden()
        try:
            drucken(uri, pdf, titel)
        except (OSError, http.client.HTTPException):
            raise RuntimeError("PRINTER DOES NOT ANSWER")
        return "PRINTED %d PAGE%s" % (seiten, "" if seiten == 1 else "S")


if __name__ == "__main__":             # zum Ausprobieren am Mac
    import sys
    if sys.argv[1:2] == ["suchen"]:
        for u in mdns_suchen():
            print(u, drucker_name(u))
    elif sys.argv[1:2] == ["pdf"]:     # pdf QUELLE ZIEL.pdf [titel]
        roh = open(sys.argv[2], "rb").read()
        f = {"bmp": bild_zu_pdf, "wrl": wrl_zu_pdf}.get(sys.argv[2].lower().rsplit(".", 1)[-1], text_zu_pdf)
        if f is wrl_zu_pdf:             # Bilder (~DRUCK.BMP) neben der Liste suchen
            ordner = os.path.dirname(os.path.abspath(sys.argv[2]))

            def f(roh, titel, f=f):
                def bild(name, w, h):
                    p = os.path.join(ordner, name.replace("~DRUCK", "druck").lower())
                    if not os.path.isfile(p):
                        return None
                    b, hh, pal, punkte = bmp_lesen(open(p, "rb").read())
                    return ("idx", b, hh, pal, punkte)
                return f(roh, titel, bild)
        pdf, n = f(roh, " ".join(sys.argv[4:]))
        open(sys.argv[3], "wb").write(pdf)
        print(n, "Seite(n)")
    elif sys.argv[1:2] == ["pruefen"]:  # pruefen URI QUELLE: Validate-Job, ohne Papier
        roh = open(sys.argv[3], "rb").read()
        f = bild_zu_pdf if sys.argv[3].lower().endswith(".bmp") else text_zu_pdf
        pdf, n = f(roh, "TEST")
        print(drucken(sys.argv[2], pdf, "TEST", nur_pruefen=True))
