#!/usr/bin/env python3
"""Nachrichten fuer den MERIDIAN - Teil des Netzdiensts (draht.py)

Die App NEWS (horizont/apps/news.asm) zeigt Schlagzeilen und Artikel aus
drei Quellen: Tagesschau (ARD, api2u: die bundesweiten Meldungen mit
Ressort, beim Oeffnen der ganze Artikel), heise online (Atom: Anrisstext)
und das Retro-Modern-Forum (Discourse-RSS: der erste Beitrag eines Themas).
Der Dienst holt nur, wenn die App fragt (eine Quelle hoechstens alle
5 Minuten neu), saeubert den Text, setzt ihn in den Zeichensatz des
MERIDIAN und bricht ihn fuer die App um - sie rechnet nichts.

Anfragen der App: Art 15, Befehl als Text.
   QUELLE n        Quelle 0-2 waehlen, holt, wenn noetig (Status 1)
   NEU             die gewaehlte Quelle jetzt neu holen (Status 1)
   STAND           Status 0: fertig, Antwort = Anzahl Meldungen (Byte),
                   Zeit des Abrufs "HH:MM" (6), Anzahl Artikelzeilen (Wort);
                   1: holt noch; 2: Fehler (Text in der Antwort)
   LISTE i         ab Meldung i drei Eintraege zu 80 Bytes: Zeit (6, "18:42"
                   oder "19.10."), Schlagzeile (74, gekuerzt auf B_LISTE)
   VORSCHAU i      Dachzeile und erster Satz, drei Zeilen zu 68 Bytes
   ARTIKEL i       Meldung i als Zeilen umbrechen (holt den Text, wenn
                   noetig - Status 1, dann STAND)
   ZEILEN z        ab Artikelzeile z so viele, wie in 255 Bytes passen: je
                   Zeile Art (1 Dachzeile, 2 Titel, 3 Datum, 4 Text,
                   5 Zwischentitel), Text, 0
"""
import datetime
import email.utils
import html
import json
import re
import threading
import time
import urllib.request
import xml.etree.ElementTree as ET

KOPF = {"User-Agent": "MERIDIAN-816-News/1.0 (retro-modern.net)"}
NEU_NACH = 5 * 60
B_TEXT = 66                     # Artikelzeilen so breit
B_LISTE = 60                    # Schlagzeilen so breit
HOECHSTENS = 40                 # Meldungen je Quelle
QUELLEN = [
    ("tagesschau.de", "https://www.tagesschau.de/api2u/news"),
    ("heise online", "https://www.heise.de/rss/heise-atom.xml"),
    ("Retro-Modern Forum", "https://forum.retro-modern.net/latest.rss"),
]
ATOM = "{http://www.w3.org/2005/Atom}"
DC = "{http://purl.org/dc/elements/1.1/}"
Z_DACH, Z_TITEL, Z_DATUM, Z_TEXT, Z_ZWISCHEN = 1, 2, 3, 4, 5


class Umleitung(urllib.request.HTTPRedirectHandler):
    """folgt auch 308 (Python 3.9 auf dem MiSTer kennt nur 301-303 und 307)"""
    def redirect_request(self, req, fp, code, msg, headers, neu):
        return super().redirect_request(req, fp, 307 if code == 308 else code, msg, headers, neu)
    http_error_308 = urllib.request.HTTPRedirectHandler.http_error_302


# -------------------------------------------------- Zeit (deutsche Zeit, ohne tzdata)
def sommerzeit(utc):
    """MESZ vom letzten Sonntag im Maerz bis zum letzten im Oktober, je 1 Uhr UTC"""
    def letzter_sonntag(monat):
        t = datetime.datetime(utc.year, monat, 31)
        return t - datetime.timedelta(days=(t.weekday() + 1) % 7)
    beginn = letzter_sonntag(3).replace(hour=1)
    ende = letzter_sonntag(10).replace(hour=1)
    return beginn <= utc.replace(tzinfo=None) < ende


def deutsche_zeit(t):
    """datetime mit Zeitzone -> deutsche Ortszeit (ohne Zeitzone)"""
    utc = t.astimezone(datetime.timezone.utc).replace(tzinfo=None)
    return utc + datetime.timedelta(hours=2 if sommerzeit(utc) else 1)


def jetzt():
    return deutsche_zeit(datetime.datetime.now(datetime.timezone.utc))


def kurzzeit(t):
    """"18:42" von heute, sonst "19.10." """
    return t.strftime("%H:%M") if t.date() == jetzt().date() else t.strftime("%d.%m.")


# -------------------------------------------------- Text
def absaetze(roh):
    """HTML -> Absaetze (Liste von (Art, Text)); Listenpunkte mit "- " """
    s = roh or ""
    s = re.sub(r"(?is)<(script|style|figure|aside)[^>]*>.*?</\1>", " ", s)
    s = re.sub(r"(?i)<li[^>]*>", "\n- ", s)
    s = re.sub(r"(?i)<h[1-6][^>]*>", "\n\x05", s)
    s = re.sub(r"(?i)</h[1-6]>", "\n", s)
    s = re.sub(r"(?i)<br\s*/?>|</p>|</li>|</div>|</blockquote>|<p[^>]*>", "\n", s)
    s = html.unescape(re.sub(r"<[^>]+>", "", s))
    teile = []
    for z in s.split("\n"):
        z = re.sub(r"\s+", " ", z).strip()
        if not z:
            continue
        if z.startswith("\x05"):
            teile.append((Z_ZWISCHEN, z[1:].strip()))
        else:
            teile.append((Z_TEXT, z))
    return teile


class Nachrichten:
    def __init__(self, zu_meridian):
        self.zu = zu_meridian
        self.quelle = 0
        self.listen = [None, None, None]        # je Quelle: Meldungen
        self.zeiten = [0.0, 0.0, 0.0]          # wann geholt
        self.holt = False
        self.fehler = ""
        self.zeilen = []                        # umbrochener Artikel: (Art, Bytes)
        self.lock = threading.Lock()

    # -------------------------------------------------- Netz
    def holen_roh(self, url):
        o = urllib.request.build_opener(urllib.request.ProxyHandler({}), Umleitung())
        with o.open(urllib.request.Request(url, headers=KOPF), timeout=15) as r:
            return r.read()

    def m(self, s):
        """Text -> Zeichensatz des MERIDIAN, als str (fuer Umbruch und Laenge)"""
        return self.zu(s or "").decode("latin-1")

    def liste_holen(self, q):
        name, url = QUELLEN[q]
        d = self.holen_roh(url)
        meldungen = []
        if q == 0:
            for n in json.loads(d).get("news", []):
                if n.get("type") != "story" or not n.get("ressort"):
                    continue                        # nur bundesweite Artikel
                t = deutsche_zeit(datetime.datetime.fromisoformat(n["date"]))
                meldungen.append({"zeit": t, "titel": n.get("title", ""),
                                  "dach": n.get("topline") or n["ressort"].capitalize(),
                                  "satz": n.get("firstSentence", ""), "details": n.get("details"),
                                  "absaetze": None})
        elif q == 1:
            for e in ET.fromstring(d).findall(ATOM + "entry"):
                t = deutsche_zeit(datetime.datetime.fromisoformat(e.findtext(ATOM + "updated")))
                satz = e.findtext(ATOM + "summary") or ""
                meldungen.append({"zeit": t, "titel": e.findtext(ATOM + "title") or "",
                                  "dach": "heise online", "satz": satz, "details": None,
                                  "absaetze": absaetze(e.findtext(ATOM + "content") or satz)})
        else:
            for it in ET.fromstring(d).findall("./channel/item"):
                t = deutsche_zeit(email.utils.parsedate_to_datetime(it.findtext("pubDate")))
                text = absaetze(it.findtext("description"))
                von = it.findtext(DC + "creator") or ""
                kat = it.findtext("category") or ""
                meldungen.append({"zeit": t, "titel": it.findtext("title") or "",
                                  "dach": kat, "satz": text[0][1] if text else "",
                                  "von": von, "details": None, "absaetze": text})
            meldungen.sort(key=lambda x: x["zeit"], reverse=True)
        return meldungen[:HOECHSTENS]

    def faden_liste(self, q):
        try:
            liste = self.liste_holen(q)
            with self.lock:
                self.listen[q] = liste
                self.zeiten[q] = time.time()
                self.fehler = ""
            print("Nachrichten:", QUELLEN[q][0], len(liste), flush=True)
        except Exception as e:
            with self.lock:
                self.fehler = "No connection to " + QUELLEN[q][0] if isinstance(e, OSError) else str(e)[:80]
            print("Nachrichten: Fehler", QUELLEN[q][0], e, flush=True)
        finally:
            self.holt = False

    def faden_artikel(self, q, i):
        try:
            n = self.listen[q][i]
            if n["absaetze"] is None and n.get("details"):
                d = json.loads(self.holen_roh(n["details"]))
                teile = []
                for c in d.get("content", []):
                    if c.get("type") == "text":
                        teile += absaetze(c.get("value"))
                    elif c.get("type") == "headline":
                        teile += [(Z_ZWISCHEN, t) for _, t in absaetze(c.get("value"))]
                n["absaetze"] = teile or [(Z_TEXT, n["satz"])]
            self.umbrechen(q, i)
        except Exception as e:
            with self.lock:
                self.fehler = "Article not available" if isinstance(e, OSError) else str(e)[:80]
            print("Nachrichten: Fehler Artikel", e, flush=True)
        finally:
            self.holt = False

    def umbrechen(self, q, i):
        import textwrap
        n = self.listen[q][i]
        zeilen = []

        def dazu(art, text, einzug=""):
            for z in textwrap.wrap(self.m(text), B_TEXT, subsequent_indent=einzug) or [""]:
                zeilen.append((art, z.encode("latin-1")))

        dazu(Z_DACH, n["dach"])
        dazu(Z_TITEL, n["titel"])
        datum = n["zeit"].strftime("%d.%m.%Y, %H:%M") + "  -  " + QUELLEN[q][0]
        if n.get("von"):
            datum += "  -  " + n["von"]
        dazu(Z_DATUM, datum)
        punkt = False
        for art, text in n["absaetze"] or [(Z_TEXT, n["satz"])]:
            neu_punkt = text.startswith("- ")
            if not (punkt and neu_punkt):           # Aufzaehlungen ohne Leerzeile dazwischen
                zeilen.append((Z_TEXT, b""))
            dazu(art, text, "  " if neu_punkt else "")
            punkt = neu_punkt
        with self.lock:
            self.zeilen = zeilen

    def starte(self, ziel, *args):
        if not self.holt:
            self.holt = True
            threading.Thread(target=ziel, args=args, daemon=True).start()

    # -------------------------------------------------- Anfragen der App
    def eintrag(self, i):
        with self.lock:
            liste = self.listen[self.quelle] or []
        return liste[i] if 0 <= i < len(liste) else None

    def anfrage(self, text):
        teile = text.decode("latin-1").strip().split(None, 1)
        wort = teile[0].upper() if teile else "STAND"
        try:
            zahl = int(teile[1]) if len(teile) > 1 else 0
        except ValueError:
            zahl = 0
        if wort == "QUELLE":
            self.quelle = max(0, min(2, zahl))
            self.zeilen = []
            if self.listen[self.quelle] is None or time.time() - self.zeiten[self.quelle] > NEU_NACH:
                self.fehler = ""
                self.starte(self.faden_liste, self.quelle)
                return 1, b""
            return 0, b""
        if wort == "NEU":
            self.fehler = ""
            self.starte(self.faden_liste, self.quelle)
            return 1, b""
        if wort == "STAND":
            if self.holt:
                return 1, b""
            with self.lock:
                if self.fehler:
                    return 2, self.zu(self.fehler)
                liste = self.listen[self.quelle]
                if liste is None:
                    return 1, b""
                um = jetzt() - datetime.timedelta(seconds=time.time() - self.zeiten[self.quelle])
                b = bytearray([len(liste)]) + um.strftime("%H:%M").encode() + b"\0"
                b += len(self.zeilen).to_bytes(2, "little")
            return 0, bytes(b)
        if wort == "LISTE":
            b = bytearray()
            for k in range(3):
                n = self.eintrag(zahl + k)
                feld = bytearray(80)
                if n:
                    zeit = kurzzeit(n["zeit"]).encode()
                    feld[0:len(zeit)] = zeit
                    titel = self.m(n["titel"]).strip()
                    if len(titel) > B_LISTE:
                        titel = titel[:B_LISTE - 3].rstrip() + "..."
                    t = titel.encode("latin-1")
                    feld[6:6 + len(t)] = t
                b += feld
            return 0, bytes(b)
        if wort == "VORSCHAU":
            n = self.eintrag(zahl)
            b = bytearray(3 * 68)
            if n:
                import textwrap
                zeilen = [self.m(n["dach"])[:67]] + textwrap.wrap(self.m(n["satz"]), 67)[:2]
                for k, z in enumerate(zeilen):
                    t = z.encode("latin-1")
                    b[k * 68:k * 68 + len(t)] = t
            return 0, bytes(b)
        if wort == "ARTIKEL":
            if not self.eintrag(zahl):
                return 2, b"No such article"
            self.zeilen = []
            self.fehler = ""
            self.starte(self.faden_artikel, self.quelle, zahl)
            return 1, b""
        if wort == "ZEILEN":
            b = bytearray()
            with self.lock:
                zeilen = self.zeilen
            for art, t in zeilen[zahl:]:
                if len(b) + len(t) + 2 > 255:
                    break
                b += bytes([art]) + t + b"\0"
            return 0, bytes(b)
        return 2, b"Unknown command"
