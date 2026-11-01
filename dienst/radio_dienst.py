#!/usr/bin/env python3
"""Internetradio fuer den MERIDIAN - Teil des Netzdiensts (draht.py)

Die App RADIO (horizont/apps/radio.asm) schickt eine Anfrage Art 12 mit der
Adresse eines Senders (MP3, http oder https) und Art 13 zum Anhalten. Der
Dienst holt den Strom selbst (mit Icy-MetaData, so kommt der laufende Titel
mit), gibt ihn an mpg123 (22 050 Hz, Stereo, 16 Bit; am Mac ffmpeg) und legt die oberen
Bytes - 8 Bit mit Vorzeichen, wie ORGEL sie spielt - in zwei Ringpuffer im
DRAHT-Fenster: links ab $8000,
rechts ab $C000, je 16 KB.

Art 23 (ab Netzdienst 1.9) spielt denselben Sender in besserer Qualitaet:
32 000 Hz und 12 Bit. ORGEL spielt nur 8 Bit, darum je Seite zwei Kanaele -
die oberen 8 Bit (grob) bei voller Lautstaerke und die naechsten 4 Bit (fein,
0-15) bei einem Sechzehntel davon. Dafuer vier Ringe zu 8 KB: links grob
$8000, links fein $A000, rechts grob $C000, rechts fein $E000. Aeltere
Dienste kennen Art 23 nicht (-6), die App nimmt dann Art 12.

Die App kopiert von dort in eine Sample-
Schleife, die ORGEL spielt, und meldet, wie weit sie gelesen hat; der Dienst
schreibt nie ueber diese Stelle hinaus (ist der Ring voll, wartet er - und
mit ihm mpg123 und der Strom).

Stand im Fenster ab $80:
   +0    Zustand: 0 aus, 1 verbindet, 2 spielt, 3 Fehler, 4 Datei zu Ende
   +2/+3 Schreibzeiger (Dienst), +4/+5 Lesezeiger (App), je 0-$3FFF
   +6/+7 kbit/s des Senders
   +8    laufender Titel (StreamTitle, mit 0, bis 56 Bytes)
   +$40  Name des Senders oder Fehlertext (mit 0, bis 64 Bytes)
   +1    zaehlt bei jeder Aenderung von Titel, Name oder Zustand
Spektrum und Cover: radio_bild.py (ab $0400, $0800 und $1000 im Fenster).

Mitschnitt (Art 16, Netzdienst 1.8): Byte 0 des Texts = 1 startet ihn fuer
den laufenden Sender (dahinter, mit 0, sein Name aus der Liste der App),
0 haelt ihn an, 2 fragt nur (aeltere Dienste antworten -6). Der Dienst schreibt den MP3-Strom, wie er kommt, nach
Laufwerk 3: SLAY0001.MP3 - bis zu vier Zeichen des Senders und eine
laufende Nummer, 8.3 in Grossbuchstaben, damit der MERIDIAN den Namen sieht,
wie er ist. Vorn steht ein ID3-Tag (v2.3, fest 32 KB) mit Sender, Datum und
der Liste der Titel mit ihrer Zeit; der Dienst schreibt ihn bei jedem neuen
Titel neu. Der Mitschnitt endet mit Stopp, anderem Sender oder Datei, Lesen
oder Schreiben auf Laufwerk 3 (Art 5/6), anderem Core, abgerissenem Strom -
und wenn auf Laufwerk 3 weniger als 20 MB frei sind.

Abspielen (Art 12 mit "FILE:NAME.MP3"): eine MP3-Datei von Laufwerk 3 auf
demselben Weg wie ein Sender. Name und Titel kommen aus dem ID3-Tag; bei
einem Mitschnitt der Titel, der an der Stelle lief. Ist die Datei zu Ende und
der Ring leer: Zustand 4.

Stand des Mitschnitts und der Datei im Fenster ab $0880:
   +0    0 nichts, 1 nimmt auf, 2 Fehler, 3 spielt eine Datei, 4 Datei zu Ende
   +1    zaehlt bei jeder neuen Zeile
   +$20  Zeile fuer die App (mit 0, bis 34 Zeichen)
"""
import os
import re
import shutil
import ssl
import struct
import subprocess
import sys
import threading
import time
import unicodedata
import urllib.request

import radio_bild

STAND = 0x80
AUFNAHME = 0x0880               # Stand des Mitschnitts (hinter dem Kopf des Covers)
A_ZEILE = AUFNAHME + 0x20
A_BREITE = 34
# Formate: Art 12 wie bisher, Art 23 mit 32 kHz und 12 Bit (je Seite grob + fein)
ALT = dict(ring=0x4000, rate=22050, ringe=(0x8000, None, 0xC000, None))
FEIN = dict(ring=0x2000, rate=32000, ringe=(0x8000, 0xA000, 0xC000, 0xE000))
FEIN_4 = bytes(b >> 4 for b in range(256))    # unteres Byte -> Bits 4-7 des 16-Bit-Werts
VOR = 0x4000                    # so weit liegt die Schleife der App vor dem Ohr (radio.asm)
TAG = 32768                     # ID3-Tag der Mitschnitte, fest gross: wird neu geschrieben
LOG = "MERIDIAN RADIO"          # Beschreibung des COMM-Rahmens mit der Titelliste
FREI_START = 100 * 1024 * 1024  # so viel muss frei sein, damit ein Mitschnitt beginnt
FREI_MIN = 20 * 1024 * 1024     # darunter endet er
DATEI_MAX = 4_000_000_000       # FAT32 kann nicht mehr

# Dekoder: mpg123 (auf dem MiSTer, 1.25); am Mac stuerzt mpg123 1.33 bei
# manchen Stroemen in der Pipe ab, dort nimmt der Dienst ffmpeg. Beide liefern
# 16 Bit mit Vorzeichen, Stereo, in der Rate des Formats.
def dekoder(rate):
    if sys.platform == "darwin" and shutil.which("ffmpeg"):
        return ["ffmpeg", "-loglevel", "quiet", "-i", "-", "-f", "s16le", "-ac", "2", "-ar", str(rate), "-"]
    return ["mpg123", "-q", "-s", "-r", str(rate), "--stereo", "-"]


# ---------------------------------------------------------------- Hilfen
def zeit(s):
    s = int(s)
    return f"{s // 3600}:{s // 60 % 60:02d}:{s % 60:02d}" if s >= 3600 else f"{s // 60}:{s % 60:02d}"


def sekunden(t):
    w = 0
    for teil in t.split(":"):
        w = w * 60 + int(teil)
    return w


def groesse(n):
    return f"{n / 1048576:.1f} MB" if n >= 1048576 else f"{n // 1024} KB"


def zeile(teile, wort=""):
    """Teile mit zwei Leerzeichen, wenn es passt, sonst mit einem; das Wort
    vorn ("REC") mit einem"""
    vorn = wort + " " if wort else ""
    z = vorn + "  ".join(teile)
    return z if len(z) <= A_BREITE else vorn + " ".join(teile)


# MP3-Rahmen (Layer III): Bitraten und Abtastraten
BITRATEN = {3: (0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320),
            2: (0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160)}
ABTASTRATEN = {3: (44100, 48000, 32000), 2: (22050, 24000, 16000), 0: (11025, 12000, 8000)}


def rahmen(b, i):
    """Rahmenkopf bei i -> (kbit/s, Laenge) oder None"""
    if i + 4 > len(b) or b[i] != 0xFF or b[i + 1] & 0xE0 != 0xE0:
        return None
    ver, schicht = (b[i + 1] >> 3) & 3, (b[i + 1] >> 1) & 3
    bi, si, pad = b[i + 2] >> 4, (b[i + 2] >> 2) & 3, (b[i + 2] >> 1) & 1
    if ver == 1 or schicht != 1 or bi in (0, 15) or si == 3:
        return None
    kbps = BITRATEN[3 if ver == 3 else 2][bi]
    return kbps, (144 if ver == 3 else 72) * kbps * 1000 // ABTASTRATEN[ver][si] + pad


def erster_rahmen(b):
    """erster Rahmen, auf den gleich der naechste folgt -> (Stelle, kbit/s)
    oder (None, 0)"""
    for i in range(max(0, len(b) - 3)):
        r = rahmen(b, i)
        if r and rahmen(b, i + r[1]):
            return i, r[0]
    return None, 0


# ID3v2.3 schreiben (UTF-16 mit BOM, versteht jeder Spieler) und lesen (2.3, 2.4)
def _sicher(n):
    return bytes(((n >> 21) & 0x7F, (n >> 14) & 0x7F, (n >> 7) & 0x7F, n & 0x7F))


def _rahmen23(kennung, inhalt):
    return kennung.encode("ascii") + struct.pack(">I", len(inhalt)) + b"\0\0" + inhalt


def _text(s):
    return b"\x01" + s.encode("utf-16") + b"\0\0"


def _comm(zeilen):
    return _rahmen23("COMM", b"\x01eng" + LOG.encode("utf-16") + b"\0\0" + "\n".join(zeilen).encode("utf-16"))


def id3_bauen(sender, datum, titel):
    """Tag von genau TAG Bytes; titel = [(Sekunden, Text)]"""
    kopf = (_rahmen23("TIT2", _text(f"{sender} {datum}")) + _rahmen23("TPE1", _text(sender))
            + _rahmen23("TALB", _text("MERIDIAN 816 RADIO")) + _rahmen23("TYER", _text(datum[:4])))
    zeilen = [f"{zeit(s)} {t}" for s, t in titel]
    while zeilen and 10 + len(kopf) + len(_comm(zeilen)) > TAG:
        zeilen.pop()                            # passt nicht mehr: die ersten bleiben
    if zeilen:
        kopf += _comm(zeilen)
    return b"ID3\x03\x00\x00" + _sicher(TAG - 10) + kopf + bytes(TAG - 10 - len(kopf))


def _lesbar(art, b):
    kodierung = {0: "latin-1", 1: "utf-16", 2: "utf-16-be"}.get(art, "utf-8")
    return b.decode(kodierung, "replace").strip("\0").strip()


def _trennen(art, b):
    """erstes Feld (bis zur 0) abtrennen -> (Feld, Rest)"""
    if art in (1, 2):
        for i in range(0, len(b) - 1, 2):
            if b[i:i + 2] == b"\0\0":
                return b[:i], b[i + 2:]
        return b, b""
    i = b.find(b"\0")
    return (b, b"") if i < 0 else (b[:i], b[i + 1:])


def id3_lesen(f):
    """ID3v2 am Anfang der Datei -> (Laenge, {Kennung: Text}, [(Sekunden,
    Titel)]); f steht danach hinter dem Tag"""
    kopf = f.read(10)
    if len(kopf) < 10 or kopf[:3] != b"ID3":
        f.seek(0)
        return 0, {}, []
    ver, flags = kopf[3], kopf[5]
    n = (kopf[6] << 21) | (kopf[7] << 14) | (kopf[8] << 7) | kopf[9]
    laenge = 10 + n + (10 if flags & 0x10 else 0)
    daten = f.read(n)
    f.seek(laenge)
    felder, liste = {}, []
    if ver not in (3, 4) or flags & 0x80:       # alt oder unsynchronisiert: nur ueberspringen
        return laenge, felder, liste
    i = 0
    if flags & 0x40 and len(daten) >= 4:        # erweiterter Kopf
        i = 4 + struct.unpack(">I", daten[:4])[0] if ver == 3 else \
            (daten[0] << 21) | (daten[1] << 14) | (daten[2] << 7) | daten[3]
    while i + 10 <= len(daten) and daten[i] != 0:
        kennung = daten[i:i + 4].decode("latin-1")
        g = daten[i + 4:i + 8]
        n = struct.unpack(">I", g)[0] if ver == 3 else (g[0] << 21) | (g[1] << 14) | (g[2] << 7) | g[3]
        inhalt = daten[i + 10:i + 10 + n]
        i += 10 + n
        if not inhalt:
            continue
        if kennung in ("TIT2", "TPE1", "TALB"):
            felder[kennung] = _lesbar(inhalt[0], inhalt[1:])
        elif kennung == "COMM" and len(inhalt) > 4:
            besch, text = _trennen(inhalt[0], inhalt[4:])
            if _lesbar(inhalt[0], besch) == LOG:
                for z in _lesbar(inhalt[0], text).splitlines():
                    m = re.match(r"(\d+(?::\d\d){1,2}) (.*)", z)
                    if m:
                        liste.append((sekunden(m.group(1)), m.group(2)))
    return laenge, felder, liste


def kuerzel(sender):
    z = unicodedata.normalize("NFKD", sender).upper()
    return ("".join(c for c in z if c.isascii() and c.isalnum()) or "REC")[:4]


def neuer_name(ordner, sender):
    """SLAY0001.MP3: Kuerzel und die naechste Nummer hinter der hoechsten"""
    k = kuerzel(sender)
    stellen = 8 - len(k)
    muster = re.compile(re.escape(k) + r"(\d{%d})\.MP3" % stellen)
    hoechste = 0
    for n in os.listdir(ordner):
        m = muster.fullmatch(n.upper())
        if m:
            hoechste = max(hoechste, int(m.group(1)))
    if hoechste + 1 >= 10 ** stellen:
        return None
    return f"{k}{hoechste + 1:0{stellen}d}.MP3"


class Mitschnitt:
    """eine MP3-Datei auf Laufwerk 3, die mit dem Strom waechst"""
    def __init__(self, pfad, sender, kbps, lauf):
        self.pfad = pfad
        self.name = os.path.basename(pfad)
        self.sender = sender
        self.kbps = kbps                # icy-br, bis der erste Rahmen da ist
        self.lauf = lauf
        self.datum = time.strftime("%Y-%m-%d %H:%M")
        self.titel = []
        self.bytes = 0
        self.beginn = time.time()
        self.vorlauf = b""              # bis zum ersten ganzen Rahmen
        self.gefunden = False
        self.f = open(pfad, "wb")
        self.f.write(id3_bauen(sender, self.datum, []))

    def sekunden(self):
        if self.kbps:
            return self.bytes / (self.kbps * 125)
        return time.time() - self.beginn

    def schreiben(self, daten):
        if not self.gefunden:           # mitten im Strom: ab dem ersten ganzen Rahmen
            self.vorlauf += daten
            i, kbps = erster_rahmen(self.vorlauf)
            if i is None and len(self.vorlauf) < 65536:
                return
            self.gefunden = True
            daten, self.vorlauf = self.vorlauf[i or 0:], b""
            if kbps:
                self.kbps = kbps
        self.f.write(daten)
        self.bytes += len(daten)

    def titel_dazu(self, t):
        if t and not (self.titel and self.titel[-1][1] == t):
            self.titel.append((self.sekunden(), t))
            self.tag_neu()

    def tag_neu(self):
        self.f.seek(0)
        self.f.write(id3_bauen(self.sender, self.datum, self.titel))
        self.f.seek(0, 2)
        self.f.flush()

    def schliessen(self):
        try:
            self.tag_neu()
        finally:
            self.f.close()


class Radio:
    def __init__(self, fenster, zu_meridian, ordner=None, finden=None):
        self.m = fenster
        self.zu_meridian = zu_meridian
        self.ordner = ordner                # () -> Ordner von Laufwerk 3
        self.finden = finden                # "NAME.MP3" -> Pfad oder None
        self.lauf = 0                       # jeder Start bekommt eine neue Nummer
        self.proc = None
        self.wp = 0
        self.fmt = ALT                      # Format des laufenden Starts (Art 12 oder 23)
        self.zaehler = 0
        self.quelle = None                  # "strom" oder "datei"
        self.sender_name = ""
        self.titel_jetzt = ""
        self.kbps = 0
        self.geschrieben = 0                # Samples seit dem Start
        self.datei = None                   # spielt eine Datei: Name, Titel, Laenge
        self.mitschnitt = None
        self.m_lock = threading.Lock()
        self.a_zaehler = 0
        self.a_zeit = 0.0
        self.a_halten = 0.0                 # bis dahin bleibt eine Fehlerzeile stehen
        self.platz_zeit = 0.0
        self.m[STAND:STAND + 0x80] = bytes(0x80)
        self.m[AUFNAHME:AUFNAHME + 0x80] = bytes(0x80)
        self.spektrum = radio_bild.Spektrum(fenster)
        self.cover = radio_bild.Cover(fenster)

    # -------------------------------------------------- Stand im Fenster
    def zustand(self, z, name=None):
        self.m[STAND] = z
        if name is not None:
            self.text(0x40, name, 64)
        self.geaendert()

    def text(self, versatz, s, laenge):
        b = self.zu_meridian(s)[:laenge - 1] + b"\0"
        self.m[STAND + versatz:STAND + versatz + len(b)] = b

    def geaendert(self):
        self.zaehler = (self.zaehler + 1) & 0xFF
        self.m[STAND + 1] = self.zaehler

    def titel_setzen(self, t, suche=None):
        self.text(8, t, 56)
        self.geaendert()
        self.cover.titel_neu(suche or t)

    def a_zeile(self, z, text):
        """Zeile des Mitschnitts (oder der Datei) fuer die App"""
        if z == 2:                          # Fehler: eine Weile lesbar lassen
            self.a_halten = time.time() + 3.0
        b = self.zu_meridian(text)[:A_BREITE] + b"\0"
        self.m[A_ZEILE:A_ZEILE + len(b)] = b
        self.m[AUFNAHME] = z
        self.a_zaehler = (self.a_zaehler + 1) & 0xFF
        self.m[AUFNAHME + 1] = self.a_zaehler          # zuletzt: die App zeigt sie

    def lesezeiger(self):
        return struct.unpack("<H", self.m[STAND + 4:STAND + 6])[0] & (self.fmt["ring"] - 1)

    def spektrum_zeigen(self):
        """Spektrum der Stelle, die gerade klingt (radio_bild.Spektrum)"""
        self.spektrum.zeigen(self.geschrieben - ((self.wp - self.lesezeiger()) & (self.fmt["ring"] - 1)))

    # -------------------------------------------------- Anfragen
    def anfrage(self, art, text):
        if art == 16:
            return self.aufnahme(text)
        if art in (12, 23):
            self.fmt_neu = FEIN if art == 23 else ALT
            t = text.decode("latin-1").strip()
            if t[:5].upper() == "FILE:":
                self.datei_start(t[5:].strip())
                return 0, b""
            if not re.match(r"https?://", t):
                return -1, b"ONLY HTTP AND HTTPS"
            self.start(t)
            return 0, b""
        self.stop()                         # Art 13
        return 0, b""

    def neu(self, quelle):
        """vor jedem Start: alles auf Anfang"""
        self.stop()
        self.lauf += 1
        self.quelle = quelle
        self.fmt = getattr(self, "fmt_neu", ALT)
        self.spektrum.ring = self.fmt["ring"]
        # Stimmt, solange das Ohr versatz bis versatz + ring hinter der Lesestelle
        # liegt; die Schleife der App (32 KB) ist fast immer zu 24-32 KB voll
        self.spektrum.versatz = 0x4000 if self.fmt is ALT else 0x6000
        self.wp = 0
        self.geschrieben = 0
        self.sender_name = ""
        self.titel_jetzt = ""
        self.kbps = 0
        self.m[STAND + 2:STAND + 6] = bytes(4)     # Schreib- und Lesezeiger auf 0
        self.spektrum.leeren()
        self.cover.leeren()
        self.m[STAND + 6:STAND + 8] = bytes(2)
        self.text(8, "", 56)
        self.zustand(1, "")

    def start(self, url):
        self.neu("strom")
        print("Radio:", url, flush=True)
        threading.Thread(target=self.holen, args=(url, self.lauf), daemon=True).start()

    def datei_start(self, name):
        self.neu("datei")
        print("Radio: Datei", name, flush=True)
        threading.Thread(target=self.datei_holen, args=(name, self.lauf), daemon=True).start()

    def stop(self, still=False):
        """anhalten; still: nichts mehr ins Fenster schreiben (anderer Core)"""
        self.lauf += 1
        self.mitschnitt_ende(still=still)
        if self.datei and not still:        # die Zeile der Datei gilt nicht mehr
            self.a_zeile(0, "")
        self.quelle = None
        self.datei = None
        p, self.proc = self.proc, None
        if p:
            try:
                p.kill()
            except OSError:
                pass
        if not still:
            self.cover.leeren()
            if self.m[STAND] != 0:
                self.zustand(0)

    # -------------------------------------------------- Mitschnitt
    def aufnahme(self, text):
        if text[:1] == b"\x02":             # nur fragen: kennt der Dienst den Mitschnitt?
            return 0, b""
        if not text or text[0] == 0:
            self.mitschnitt_ende()
            return 0, b""
        if self.mitschnitt:
            return 0, self.zu_meridian(self.mitschnitt.name)
        if self.quelle != "strom" or self.m[STAND] not in (1, 2):
            self.a_zeile(2, "Only stations can be recorded" if self.quelle == "datei"
                         else "Play a station first")
            return -1, b"NOT PLAYING"
        sender = text[1:].split(b"\0")[0].decode("latin-1").strip() or self.sender_name or "RADIO"
        try:
            ordner = self.ordner() if self.ordner else None
            if not ordner or not os.path.isdir(ordner):
                raise OSError("no drive 3")
            st = os.statvfs(ordner)
            if st.f_bavail * st.f_frsize < FREI_START:
                self.a_zeile(2, "Drive 3 is full")
                return -2, b"DRIVE 3 FULL"
            name = neuer_name(ordner, sender)
            if not name:
                raise OSError("no free file name")
            with self.m_lock:
                self.mitschnitt = Mitschnitt(os.path.join(ordner, name), sender, self.kbps, self.lauf)
                self.mitschnitt.titel_dazu(self.titel_jetzt)
        except OSError as e:
            self.a_zeile(2, "Cannot record: " + str(e.strerror or e))
            return -3, b"CANNOT RECORD"
        print("Radio: Mitschnitt", name, "-", sender, flush=True)
        self.a_zeit = 0.0
        self.a_anzeigen()
        return 0, self.zu_meridian(name)

    def a_anzeigen(self):
        m = self.mitschnitt
        jetzt = time.time()
        if m and jetzt - self.a_zeit >= 1.0:
            self.a_zeit = jetzt
            self.a_zeile(1, zeile([m.name, zeit(m.sekunden()), groesse(m.bytes)], "REC"))

    def mit_schreiben(self, daten, lauf):
        fehler = None
        with self.m_lock:
            m = self.mitschnitt
            if not m or m.lauf != lauf:
                return
            try:
                m.schreiben(daten)
                jetzt = time.time()
                if m.bytes > DATEI_MAX:
                    fehler = "File is full"
                elif jetzt - self.platz_zeit > 10:
                    self.platz_zeit = jetzt
                    st = os.statvfs(os.path.dirname(m.pfad))
                    if st.f_bavail * st.f_frsize < FREI_MIN:
                        fehler = "Drive 3 is full"
            except OSError as e:
                fehler = "Write error"
                print("Radio: Mitschnitt", e, flush=True)
        if fehler:
            self.mitschnitt_ende(fehler)
        else:
            self.a_anzeigen()

    def mit_titel(self, titel, lauf):
        with self.m_lock:
            m = self.mitschnitt
            if m and m.lauf == lauf:
                try:
                    m.titel_dazu(titel)
                except OSError as e:
                    print("Radio: Mitschnitt", e, flush=True)

    def mitschnitt_ende(self, fehler=None, still=False):
        with self.m_lock:
            m, self.mitschnitt = self.mitschnitt, None
        if not m:
            return
        try:
            m.schliessen()
            if m.bytes == 0:                # nichts angekommen: keine leere Datei liegen lassen
                os.remove(m.pfad)
        except OSError as e:
            print("Radio: Mitschnitt", e, flush=True)
        print("Radio: Mitschnitt zu", m.name, zeit(m.sekunden()), groesse(m.bytes), fehler or "", flush=True)
        if still:
            return
        if m.bytes == 0:
            self.a_zeile(2, fehler or "Nothing recorded")
        elif fehler:
            self.a_zeile(2, zeile([m.name, "saved"], fehler + ":"))
        else:
            self.a_zeile(0, zeile([m.name, zeit(m.sekunden()), groesse(m.bytes)], "Saved"))

    # -------------------------------------------------- Strom und mpg123
    def holen(self, url, lauf):
        antwort = None
        try:
            kopf = {"Icy-MetaData": "1", "User-Agent": "MERIDIAN-816 RADIO"}
            # ohne Proxy-Abfrage: am Mac ruft urllib dafuer das System
            # (_scproxy), und danach stuerzt jeder neu gestartete Prozess ab
            oeffner = urllib.request.build_opener(urllib.request.ProxyHandler({}),
                                                  urllib.request.HTTPSHandler(context=ssl.create_default_context()))
            antwort = oeffner.open(urllib.request.Request(url, headers=kopf), timeout=10)
            metaint = int(antwort.headers.get("icy-metaint") or 0)
            name = antwort.headers.get("icy-name") or ""
            try:
                self.kbps = int(antwort.headers.get("icy-br") or 0)
                self.m[STAND + 6:STAND + 8] = struct.pack("<H", self.kbps)
            except ValueError:
                pass
            if lauf != self.lauf:
                return
            self.sender_name = name
            self.zustand(1, name)
            self.cover.sender(name)
            # 16 Bit; die oberen Bytes sind das 8-Bit-Sample fuer ORGEL
            proc = subprocess.Popen(dekoder(self.fmt["rate"]),
                                    stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                    stderr=subprocess.DEVNULL)
            self.proc = proc
            threading.Thread(target=self.schreiben, args=(proc, lauf), daemon=True).start()
            while lauf == self.lauf:
                daten = antwort.read(metaint if metaint else 4096)
                if not daten:
                    raise ConnectionError("stream ended")
                self.mit_schreiben(daten, lauf)
                proc.stdin.write(daten)
                if metaint:
                    n = antwort.read(1)
                    if n and n[0]:
                        meta = antwort.read(n[0] * 16)
                        t = re.search(rb"StreamTitle='(.*?)';", meta, re.S)
                        if t:
                            titel = t.group(1).decode("utf-8", "replace")
                            self.titel_jetzt = titel
                            self.titel_setzen(titel)
                            self.mit_titel(titel, lauf)
                            print("Radio:", titel, flush=True)
        except Exception as e:              # nichts darf den Dienst anhalten
            if lauf == self.lauf:
                print("Radio:", e, flush=True)
                self.zustand(3, "ERROR: " + str(e))
                self.mitschnitt_ende("Stream lost")
        finally:
            if antwort:
                antwort.close()
            if lauf == self.lauf and self.proc:
                try:
                    self.proc.stdin.close()
                except OSError:
                    pass

    def datei_holen(self, name, lauf):
        """eine MP3-Datei von Laufwerk 3 statt eines Senders"""
        try:
            pfad = self.finden(name) if self.finden else None
            if not pfad:
                raise OSError("FILE NOT FOUND")
            with open(pfad, "rb") as f:
                laenge, felder, liste = id3_lesen(f)
                kopf = f.read(65536)
                i, kbps = erster_rahmen(kopf)
                if i is None:
                    raise ValueError("NOT AN MP3 FILE")
                gesamt = (os.path.getsize(pfad) - laenge - i) / (kbps * 125)
                kuenstler, titel = felder.get("TPE1", ""), felder.get("TIT2", "")
                if lauf != self.lauf:
                    return
                self.kbps = kbps
                self.m[STAND + 6:STAND + 8] = struct.pack("<H", kbps)
                anzeige = kuenstler or name     # Mitschnitt: der Sender, sonst der Kuenstler
                self.zustand(1, anzeige)
                self.cover.sender(anzeige)
                if not liste and (titel or kuenstler):
                    self.titel_setzen(titel or name, " ".join(x for x in (kuenstler, titel) if x))
                self.datei = {"name": name, "liste": liste, "gesamt": gesamt, "i": -1}
                self.a_zeit = 0.0
                proc = subprocess.Popen(dekoder(self.fmt["rate"]),
                                        stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                        stderr=subprocess.DEVNULL)
                self.proc = proc
                threading.Thread(target=self.schreiben, args=(proc, lauf), daemon=True).start()
                daten = kopf[i:]
                while daten and lauf == self.lauf:
                    proc.stdin.write(daten)
                    daten = f.read(16384)
        except Exception as e:
            if lauf == self.lauf:
                print("Radio:", e, flush=True)
                self.zustand(3, "ERROR: " + str(e))
        finally:
            if lauf == self.lauf and self.proc:
                try:
                    self.proc.stdin.close()
                except OSError:
                    pass

    def datei_takt(self, lauf):
        """spielt eine Datei: Titel an der Stelle, die man hoert, und die Zeit"""
        d = self.datei
        if not d or lauf != self.lauf:
            return
        offen = (self.wp - self.lesezeiger()) & (self.fmt["ring"] - 1)
        sek = max(0.0, (self.geschrieben - offen - VOR) / self.fmt["rate"])
        liste, j = d["liste"], d["i"]
        while j + 1 < len(liste) and liste[j + 1][0] <= sek:
            j += 1
        if j != d["i"]:
            d["i"] = j
            self.titel_setzen(liste[j][1])
        jetzt = time.time()
        if jetzt - self.a_zeit >= 1.0 and jetzt >= self.a_halten:
            self.a_zeit = jetzt
            self.a_zeile(3, zeile([d["name"], zeit(sek) + " / " + zeit(d["gesamt"])]))

    def datei_ende(self, lauf):
        """Datei zu Ende: warten, bis die App den Ring geleert und ihre
        Schleife gespielt hat, dann Zustand 4"""
        t0 = time.time()
        while lauf == self.lauf and self.lesezeiger() != self.wp and time.time() - t0 < 5:
            self.datei_takt(lauf)
            self.spektrum_zeigen()
            time.sleep(0.02)
        time.sleep(VOR / self.fmt["rate"])
        d = self.datei
        if lauf != self.lauf or not d:
            return
        self.spektrum.leeren()              # die Balken fallen, statt stehen zu bleiben
        self.zustand(4)
        self.a_zeile(4, zeile([d["name"], zeit(d["gesamt"])], "Finished"))

    def schreiben(self, proc, lauf):
        """PCM von mpg123 in die Ringe, nie ueber den Lesezeiger"""
        m = self.m
        ring = self.fmt["ring"]
        lg, lf, rg, rf = self.fmt["ringe"]
        rest = b""
        while lauf == self.lauf:
            daten = proc.stdout.read(4096)
            if not daten:
                if self.datei and lauf == self.lauf:
                    self.datei_ende(lauf)
                return
            daten = rest + daten            # je Bild 4 Bytes: L tief, L hoch, R tief, R hoch
            n = len(daten) // 4
            rest = daten[4 * n:]
            links, rechts = daten[1:4 * n:4], daten[3:4 * n:4]
            if lf is not None:              # 12 Bit: die naechsten 4 Bit (0-15) fuer die Feinkanaele
                fein_l, fein_r = daten[0:4 * n:4].translate(FEIN_4), daten[2:4 * n:4].translate(FEIN_4)
            i = 0
            while i < n and lauf == self.lauf:
                frei = (self.lesezeiger() - self.wp - 1) & (ring - 1)
                self.spektrum_zeigen()
                if frei == 0:
                    time.sleep(0.01)
                    continue
                k = min(frei, n - i, ring - self.wp)
                w = self.wp
                if lf is not None:          # fein zuerst: die App kopiert alle vier bis zum Schreibzeiger
                    m[lf + w:lf + w + k] = fein_l[i:i + k]
                    m[rf + w:rf + w + k] = fein_r[i:i + k]
                m[lg + w:lg + w + k] = links[i:i + k]
                m[rg + w:rg + w + k] = rechts[i:i + k]
                self.spektrum.dazu(self.geschrieben, links[i:i + k], rechts[i:i + k])
                self.wp = (self.wp + k) & (ring - 1)
                m[STAND + 2:STAND + 4] = struct.pack("<H", self.wp)
                self.geschrieben += k
                i += k
            if m[STAND] != 2 and lauf == self.lauf:
                self.zustand(2)
            if self.datei:
                self.datei_takt(lauf)


if __name__ == "__main__":                 # Probe ohne MERIDIAN: Strom holen, App spielen
    import sys
    fenster = bytearray(0x10000)
    r = Radio(fenster, lambda s: s.encode("latin-1", "replace"))
    r.start(sys.argv[1] if len(sys.argv) > 1 else "http://ice1.somafm.com/groovesalad-128-mp3")
    gelesen, t0 = 0, time.time()
    while time.time() - t0 < 8:             # wie die App: in Echtzeit lesen
        time.sleep(0.1)
        wp = struct.unpack("<H", fenster[STAND + 2:STAND + 4])[0]
        rp = struct.unpack("<H", fenster[STAND + 4:STAND + 6])[0]
        n = min((wp - rp) & (r.fmt["ring"] - 1), r.fmt["rate"] // 10)
        fenster[STAND + 4:STAND + 6] = struct.pack("<H", (rp + n) & (r.fmt["ring"] - 1))
        gelesen += n
        print("Zustand", fenster[STAND], "gelesen", gelesen, "Titel",
              bytes(fenster[STAND + 8:STAND + 64]).split(b"\0")[0], flush=True)
    r.stop()
