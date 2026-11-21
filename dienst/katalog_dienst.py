#!/usr/bin/env python3
"""KATALOG fuer den MERIDIAN - Teil des Netzdiensts (draht.py)

Die App KATALOG (horizont/apps/katalog.asm) zeigt die Software aus dem
KATALOG auf retro-modern.net (https://retro-modern.net/katalog.html) und
laedt ein Programm auf Laufwerk 3. Der Dienst holt die Liste nur, wenn die
App fragt (hoechstens alle 5 Minuten neu), setzt alles in den Zeichensatz
des MERIDIAN und bricht es fuer die App um - sie rechnet nichts.

Laden: einzelne Dateien (MOD, MER, BAS, APP ...) kommen nach Laufwerk 3
(games/MERIDIAN/files). ZIP-Pakete packt der Dienst aus: Dateien unter
games/MERIDIAN/ an ihren Platz auf der SD-Karte (so liegen unsere Pakete,
etwa SENDESCHLUSS), sonst alles, was der MERIDIAN lesen kann, nach
Laufwerk 3. Was fuer Linux oder andere Rechner ist, bleibt draussen.

Anfragen der App: Art 17, Befehl als Text.
   KAT n           Kategorie 0-7 waehlen (0 alles), holt, wenn noetig (Status 1)
   NEU             die Liste jetzt neu holen (Status 1)
   SPRACHE s       DE oder EN (die Beschreibungen; gemerkt in katalog.json)
   STAND           Status 0: fertig, Antwort = Anzahl Eintraege (Byte), Zeit
                   des Abrufs "HH:MM" (6), Sprache (1: EN); 1: holt noch;
                   2: Fehler (Text in der Antwort)
   LISTE i         ab Eintrag i drei Eintraege zu 80 Bytes: Art (6, "GAME"),
                   Zeile (74: Titel, Version, Autor)
   VORSCHAU i      drei Zeilen zu 68 Bytes: von, Version, Datum, Groesse;
                   Anfang der Beschreibung
   INFO i          Eintrag i als Zeilen umbrechen; Antwort: Anzahl (Wort)
   ZEILEN z        ab Zeile z so viele, wie in 255 Bytes passen: je Zeile Art
                   (1 Art/Version, 2 Titel, 3 von/Datum, 4 Text,
                   5 Zwischentitel), Text, 0
   LADE i [!]      Eintrag i holen und ablegen (Status 1); mit "!" ersetzen
   LADESTAND       1: laedt (Text "Loading 45 %"); 0: fertig, zwei Zeilen
                   mit 0 (was wo liegt, wie man es startet); 2: Fehler;
                   3: gibt es schon (Text: der Name) - die App fragt, dann
                   LADE i !
   MEINE i         Status 0, Antwort ein Byte: die Sterne, die dieses Geraet
                   Eintrag i gegeben hat (0: noch keine)
   BEWERTE i s     Eintrag i mit s Sternen (1-5) bewerten (Status 1); eine
                   Bewertung je Geraet und Eintrag, danach nicht aenderbar
   BEWSTAND        1: sendet noch; 0: fertig (Text mit 0); 2: Fehler (Text)

Bewerten: Der Dienst legt beim ersten Mal eine zufaellige Geraetekennung an
(katalog.json neben der Sprache) und schickt sie mit; der Server speichert
nur einen Hash davon. Was das Geraet schon bewertet hat, merkt sich
katalog.json auch, damit die Beschreibung "Your rating" zeigen kann.
"""
import datetime
import hashlib
import io
import json
import os
import secrets
import ssl
import threading
import time
import urllib.error
import urllib.request
import zipfile

LISTE_URL = "https://retro-modern.net/api/katalog/liste.json"
BEWERTEN_URL = "https://retro-modern.net/api/katalog/bewerten"
WURZEL = "https://retro-modern.net"
KOPF = {"User-Agent": "MERIDIAN-816-KATALOG/1.0 (retro-modern.net)"}
NEU_NACH = 5 * 60
GRENZE = 16 * 1024 * 1024        # so gross darf ein Download sein
B_TEXT = 66
B_LISTE = 60
HOECHSTENS = 250
# Kategorien der App (Chips 0-7) -> Kategorie im Katalog
KATEGORIEN = [None, "spiel", "programm", "app", "modul", "musik", "werkzeug", "demo"]
ART = {"spiel": "GAME", "programm": "PROG", "app": "APP", "modul": "CART", "musik": "MUSIC",
       "werkzeug": "TOOL", "demo": "DEMO"}
ART_LANG = {"spiel": "Game", "programm": "Program", "app": "HORIZONT app", "modul": "Cartridge",
            "musik": "Music", "werkzeug": "Tool", "demo": "Demo"}
LIZENZ = {"frei": "free to share", "gpl3": "GPL-3", "ccby": "CC BY 4.0", "nurladen": "download only"}
BENOETIGT = {"sdram": "SDRAM module", "netzdienst": "network service", "horizont": "HORIZONT",
             "schub": "SCHUB core"}
# was der MERIDIAN selbst liest (Laufwerk 3); ZIP packt der Dienst aus
MERIDIAN = {"BAS", "MER", "APP", "MOD", "XM", "TAK", "SPR", "BMP", "DSK", "TXT", "WRT", "CAL", "FIL",
            "DRW", "MP3", "DAT", "MWS", "SID"}
NUR_EINZELN = {"TXT"}            # Texte nur, wenn sie selbst der Eintrag sind (nicht LIESMICH aus ZIPs)
Z_ART, Z_TITEL, Z_VON, Z_TEXT, Z_ZWISCHEN = 1, 2, 3, 4, 5


def oeffnen(url, daten=None):
    kopf = dict(KOPF)
    if daten is not None:
        kopf["Content-Type"] = "application/json"
    anfrage = urllib.request.Request(url, data=daten, headers=kopf)
    try:
        return urllib.request.urlopen(anfrage, timeout=20)
    except urllib.error.URLError as e:
        if isinstance(e.reason, ssl.SSLCertVerificationError):
            # der MiSTer hat nicht immer aktuelle Zertifikate
            return urllib.request.urlopen(anfrage, timeout=20, context=ssl._create_unverified_context())
        raise


def sterne_text(n):
    """1-5 -> "***--" (der MERIDIAN hat keinen Stern im Zeichensatz)"""
    n = max(0, min(5, int(float(n) + 0.5)))
    return "*" * n + "-" * (5 - n)


def schnitt_text(b):
    """{"schnitt", "anzahl"} -> "****- 4.3 (12)" oder "" """
    if not b or not b.get("anzahl"):
        return ""
    return "%s %.1f (%d)" % (sterne_text(b["schnitt"]), float(b["schnitt"]), int(b["anzahl"]))


def jetzt_hhmm():
    return time.strftime("%H:%M")


def groesse_text(b):
    if b >= 1000 * 1000:
        return "%.1f MB" % (b / 1e6)
    return "%d KB" % max(1, round(b / 1000))


def sicherer_name(n):
    """Dateiname fuer den Ordner: ohne Pfad, nur harmlose Zeichen"""
    n = os.path.basename(n.replace("\\", "/"))
    n = "".join(c for c in n if c.isalnum() or c in "._-~ ")
    return n.strip(". ")[:64]


class Katalog:
    def __init__(self, zu_meridian, dateien, ordner):
        self.zu = zu_meridian
        self.dateien = dateien            # -> Ordner von Laufwerk 3 (games/MERIDIAN/files)
        self.ordner = ordner              # -> games/MERIDIAN (fuer Pakete im Aufbau der SD-Karte)
        self.alle = None
        self.zeit = 0.0
        self.um = "--:--"
        self.kat = 0
        self.holt = False
        self.fehler = ""
        self.zeilen = []
        self.lock = threading.Lock()
        self.en = self.einst().get("sprache") == "en"
        # Bewerten
        self.b_laeuft = False
        self.b_status = 0                 # 0 fertig, 1 sendet, 2 Fehler
        self.b_text = b""
        # Laden
        self.laedt = False
        self.l_status = 0                 # 0 fertig, 1 laedt, 2 Fehler, 3 gibt es schon
        self.l_text = b""
        self.l_prozent = 0
        self.l_daten = None               # (id, version, Bytes): schon geholt, wartet auf "ersetzen"

    def einst_pfad(self):
        return os.path.join(self.ordner(), "katalog.json")

    def einst(self):
        try:
            d = json.load(open(self.einst_pfad()))
            return d if isinstance(d, dict) else {}
        except (OSError, ValueError, AttributeError, TypeError):
            return {}

    def einst_merken(self, **neu):
        d = self.einst()
        d.update(neu)
        try:
            tmp = self.einst_pfad() + ".neu"
            with open(tmp, "w") as f:
                json.dump(d, f)
            os.replace(tmp, self.einst_pfad())
        except (OSError, TypeError):
            pass

    def geraet(self):
        g = self.einst().get("geraet")
        if not isinstance(g, str) or len(g) < 16:
            g = secrets.token_urlsafe(18)
            self.einst_merken(geraet=g)
        return g

    def meine(self, e):
        """-> Sterne, die dieses Geraet Eintrag e gegeben hat (0: keine)"""
        b = self.einst().get("bewertet")
        try:
            return int((b or {}).get(str(e.get("id")), 0))
        except (TypeError, ValueError):
            return 0

    def m(self, s):
        return self.zu(s or "").decode("latin-1")

    # -------------------------------------------------- Liste
    def faden_liste(self):
        try:
            with oeffnen(LISTE_URL) as r:
                d = json.loads(r.read(4 * 1024 * 1024))
            alle = []
            for e in d.get("eintraege", [])[:HOECHSTENS]:
                name = str((e.get("datei") or {}).get("name") or "")
                endung = name.rsplit(".", 1)[-1].upper() if "." in name else ""
                if endung != "ZIP" and endung not in MERIDIAN:
                    continue                    # z. B. der Emulator fuer Windows
                alle.append(e)
            with self.lock:
                self.alle = alle
                self.zeit = time.time()
                self.um = jetzt_hhmm()
                self.fehler = ""
            print("Katalog:", len(alle), "Eintraege", flush=True)
        except Exception as e:
            with self.lock:
                self.fehler = "No connection to retro-modern.net" if isinstance(e, OSError) else str(e)[:80]
            print("Katalog: Fehler", e, flush=True)
        finally:
            self.holt = False

    def starte(self, ziel, *args):
        if not self.holt:
            self.holt = True
            threading.Thread(target=ziel, args=args, daemon=True).start()

    def gezeigt(self):
        with self.lock:
            alle = self.alle or []
        k = KATEGORIEN[self.kat]
        return [e for e in alle if k is None or e.get("kategorie") == k]

    def eintrag(self, i):
        g = self.gezeigt()
        return g[i] if 0 <= i < len(g) else None

    def text(self, e):
        t = (e.get("text_en") or e.get("text")) if self.en else (e.get("text") or e.get("text_en"))
        return t or ""

    # -------------------------------------------------- Beschreibung
    def startweise(self, name, ort="3"):
        """wie man eine Datei startet (englisch, kurz)"""
        n, _, end = name.upper().rpartition(".")
        if end == "MOD":
            return "Insert it in the OSD: Insert cartridge."
        if end == "MER":
            if ort == "sd":
                return "Start it in the OSD: Load program."
            return "Start it from ATLAS (drive 3) or BLOAD."
        if end == "APP":
            return "HORIZONT: Programs > Open app ..."
        if end == "BAS":
            return 'LOAD "%s",3 and RUN.' % n[:8]
        if end in ("XM", "TAK"):
            return "Open it in TAKTSTOCK (drive 3)."
        if end == "DSK":
            return "Insert it in the OSD as a disk."
        return "It is on drive 3."

    def umbrechen(self, e):
        import textwrap
        zeilen = []

        def dazu(art, text, einzug=""):
            for z in textwrap.wrap(self.m(text), B_TEXT, subsequent_indent=einzug) or [""]:
                zeilen.append((art, z.encode("latin-1")))

        kopf = ART_LANG.get(e.get("kategorie"), "")
        if e.get("stand"):
            kopf += "  -  version " + str(e["stand"])
        dazu(Z_ART, kopf)
        dazu(Z_TITEL, e.get("titel", ""))
        d = e.get("datei") or {}
        von = "by " + str(e.get("autor", ""))
        if e.get("erschienen"):
            von += "  -  " + str(e["erschienen"])
        von += "  -  " + LIZENZ.get(e.get("lizenz"), "")
        dazu(Z_VON, von)
        b = e.get("bewertung")
        if b and b.get("anzahl"):
            n = int(b["anzahl"])
            bew = "Rating %s  -  %.1f of 5 from %d %s" % (sterne_text(b["schnitt"]), float(b["schnitt"]), n,
                                                        "rating" if n == 1 else "ratings")
        else:
            bew = "Not rated yet"
        mein = self.meine(e)
        if mein:
            bew += "  -  yours: " + sterne_text(mein)
        dazu(Z_VON, bew)
        zeilen.append((Z_TEXT, b""))
        for absatz in self.text(e).split("\n"):
            if absatz.strip():
                dazu(Z_TEXT, absatz.strip())
        zeilen.append((Z_TEXT, b""))
        ben = [BENOETIGT[b] for b in e.get("benoetigt") or [] if b in BENOETIGT]
        if ben:
            dazu(Z_ZWISCHEN, "Needs")
            dazu(Z_TEXT, ", ".join(ben))
        name = str(d.get("name", ""))
        dazu(Z_ZWISCHEN, "File")
        if name.upper().endswith(".ZIP"):
            dazu(Z_TEXT, "%s (%s), a package: LOAD unpacks what is for the MERIDIAN onto drive 3"
                 % (name, groesse_text(int(d.get("groesse") or 0))))
        else:
            dazu(Z_TEXT, "%s (%s) - LOAD puts it on drive 3. %s"
                 % (name, groesse_text(int(d.get("groesse") or 0)), self.startweise(name)))
        if not mein:
            dazu(Z_ZWISCHEN, "Rate it")
            dazu(Z_TEXT, "Press 1 to 5 for one to five stars (or use the menu Rate). "
                         "One rating per MERIDIAN - it can't be changed later.")
        dazu(Z_TEXT, "More: retro-modern.net/katalog.html")
        with self.lock:
            self.zeilen = zeilen

    # -------------------------------------------------- Laden
    def faden_laden(self, e, ersetzen):
        try:
            d = e.get("datei") or {}
            url = str(d.get("url") or "")
            if url.startswith("/"):
                url = WURZEL + url
            if not url.startswith("https://"):
                raise ValueError("Bad address")
            alt = self.l_daten
            if alt and alt[0] == e["id"] and alt[1] == e.get("version"):
                roh = alt[2]
            else:
                with oeffnen(url) as r:
                    laenge = r.headers.get("Content-Length")
                    laenge = int(laenge) if laenge and laenge.isdigit() else int(d.get("groesse") or 0)
                    if laenge > GRENZE:
                        raise ValueError("Too big for the MERIDIAN")
                    teile, n = [], 0
                    while True:
                        stueck = r.read(32768)
                        if not stueck:
                            break
                        teile.append(stueck)
                        n += len(stueck)
                        if n > GRENZE:
                            raise ValueError("Too big for the MERIDIAN")
                        if laenge:
                            self.l_prozent = min(99, n * 100 // laenge)
                    roh = b"".join(teile)
                sha = d.get("sha256")
                if sha and hashlib.sha256(roh).hexdigest() != sha:
                    raise ValueError("Download damaged - please try again")
                self.l_daten = (e["id"], e.get("version"), roh)
            ziele = self.ziele(e, roh)
            if not ziele:
                raise ValueError(self.leer_grund(roh))
            if not ersetzen:
                da = [os.path.basename(p) for p, _ in ziele if os.path.exists(p)]
                if da:
                    with self.lock:
                        self.l_status, self.l_text = 3, self.zu(da[0][:40])
                    return
            for p, daten in ziele:
                os.makedirs(os.path.dirname(p), exist_ok=True)
                tmp = p + ".katalog-neu"
                with open(tmp, "wb") as f:
                    f.write(daten)
                os.replace(tmp, p)
            self.l_daten = None
            haupt, ort = self.hauptdatei(ziele)
            name = os.path.basename(haupt)
            if len(ziele) == 1:
                z1 = "%s is on drive 3." % name if ort == "3" else "%s is on the SD card." % name
            else:
                z1 = "%d files saved, with %s." % (len(ziele), name)
            with self.lock:
                self.l_status = 0
                self.l_text = self.zu(z1[:48]) + b"\0" + self.zu(self.startweise(name, ort)[:48]) + b"\0"
            print("Katalog: geladen", e.get("titel"), [p for p, _ in ziele], flush=True)
        except Exception as x:
            with self.lock:
                self.l_status = 2
                self.l_text = self.zu(("No connection" if isinstance(x, OSError) else str(x))[:48])
            print("Katalog: Fehler beim Laden", e.get("titel"), x, flush=True)
        finally:
            self.laedt = False

    def ziele(self, e, roh):
        """-> [(Pfad, Bytes)]: wohin was kommt"""
        name = str((e.get("datei") or {}).get("name") or "DATEI")
        lw3, sd = self.dateien(), self.ordner()
        if not name.upper().endswith(".ZIP"):
            return [(os.path.join(lw3, sicherer_name(name).upper()), roh)]
        aus = []
        with zipfile.ZipFile(io.BytesIO(roh)) as z:
            if any(n.replace("\\", "/").lower().startswith("linux/") for n in z.namelist()):
                return []                       # der Netzdienst selbst: gehoert auf die SD-Karte, nicht hierher
            gesamt = 0
            for info in z.infolist():
                if info.is_dir():
                    continue
                pfad = info.filename.replace("\\", "/").lstrip("/")
                teile = [t for t in pfad.split("/") if t]
                if not teile or any(t in (".", "..") for t in teile):
                    continue
                gesamt += info.file_size
                if gesamt > 4 * GRENZE:
                    raise ValueError("Package too big")
                if [t.lower() for t in teile[:2]] == ["games", "meridian"] and len(teile) > 2:
                    rest = [sicherer_name(t) for t in teile[2:]]
                    if all(rest):
                        aus.append((os.path.join(sd, *rest), z.read(info)))
                    continue
                b = sicherer_name(teile[-1])
                endung = b.rsplit(".", 1)[-1].upper() if "." in b else ""
                if endung in MERIDIAN and endung not in NUR_EINZELN:
                    aus.append((os.path.join(lw3, b), z.read(info)))
        return aus

    def leer_grund(self, roh):
        try:
            with zipfile.ZipFile(io.BytesIO(roh)) as z:
                if any(n.lower().startswith("linux/") for n in z.namelist()):
                    return "This is for the MiSTer's SD card."
        except zipfile.BadZipFile:
            return "Damaged package"
        return "Nothing for the MERIDIAN in it."

    def hauptdatei(self, ziele):
        """die Datei, mit der man anfaengt (Modul, Programm, App, BASIC ...), und wo sie liegt"""
        lw3 = os.path.normpath(self.dateien())
        pfad = ziele[0][0]
        for end in ("MOD", "MER", "APP", "BAS", "TAK", "XM", "DSK"):
            treffer = [p for p, _ in ziele if p.upper().endswith("." + end)]
            if treffer:
                pfad = treffer[0]
                break
        return pfad, "3" if os.path.dirname(os.path.normpath(pfad)) == lw3 else "sd"

    # -------------------------------------------------- Bewerten
    def faden_bewerten(self, e, sterne):
        try:
            daten = json.dumps({"id": e["id"], "sterne": sterne, "geraet": self.geraet()}).encode()
            try:
                with oeffnen(BEWERTEN_URL, daten) as r:
                    j = json.loads(r.read(4096))
                code = 200
            except urllib.error.HTTPError as x:
                code = x.code
                try:
                    j = json.loads(x.read(4096))
                except ValueError:
                    j = {}
            if code == 200 or (code == 409 and isinstance(j.get("sterne"), int)):
                gegeben = sterne if code == 200 else j["sterne"]
                b = dict(self.einst().get("bewertet") or {})
                b[str(e["id"])] = gegeben
                self.einst_merken(bewertet=b)
                if code == 200 and isinstance(j.get("bewertung"), dict):
                    with self.lock:
                        e["bewertung"] = j["bewertung"]
                text = ("Thank you: %s" % sterne_text(gegeben)) if code == 200 else \
                    ("Already rated: %s" % sterne_text(gegeben))
                with self.lock:
                    self.b_status, self.b_text = 0, self.zu(text)
                print("Katalog: bewertet", e.get("titel"), gegeben, code, flush=True)
                return
            if code == 429:
                raise ValueError("Too many ratings - try later")
            raise ValueError(str(j.get("fehler") or "Error %d" % code)[:40])
        except Exception as x:
            with self.lock:
                self.b_status = 2
                self.b_text = self.zu(("No connection" if isinstance(x, OSError) else str(x))[:40])
            print("Katalog: Fehler beim Bewerten", e.get("titel"), x, flush=True)
        finally:
            self.b_laeuft = False

    # -------------------------------------------------- Anfragen der App
    def anfrage(self, text):
        """Befehl -> (Status, Antwort); Texte (Status 1-3) enden mit 0"""
        status, antwort = self.befehl(text)
        if status and not antwort.endswith(b"\0"):
            antwort += b"\0"
        return status, antwort

    def befehl(self, text):
        teile = text.decode("latin-1").strip().split()
        wort = teile[0].upper() if teile else "STAND"
        try:
            zahl = int(teile[1]) if len(teile) > 1 else 0
        except ValueError:
            zahl = 0
        if wort == "KAT":
            self.kat = max(0, min(len(KATEGORIEN) - 1, zahl))
            self.zeilen = []
            if self.alle is None or time.time() - self.zeit > NEU_NACH:
                self.fehler = ""
                self.starte(self.faden_liste)
                return 1, b""
            return 0, b""
        if wort == "NEU":
            self.fehler = ""
            self.starte(self.faden_liste)
            return 1, b""
        if wort == "SPRACHE":
            self.en = len(teile) > 1 and teile[1].upper() == "EN"
            self.einst_merken(sprache="en" if self.en else "de")
            return 0, b""
        if wort == "STAND":
            if self.holt:
                return 1, b""
            with self.lock:
                if self.fehler:
                    return 2, self.zu(self.fehler)
                if self.alle is None:
                    return 1, b""
            n = len(self.gezeigt())
            return 0, bytes([min(n, 255)]) + self.um.encode() + b"\0" + bytes([1 if self.en else 0])
        if wort == "LISTE":
            b = bytearray()
            for k in range(3):
                e = self.eintrag(zahl + k)
                feld = bytearray(80)
                if e:
                    art = ART.get(e.get("kategorie"), "").encode()
                    feld[0:len(art)] = art
                    titel = self.m(e.get("titel"))
                    if len(titel) > 30:
                        titel = titel[:29] + "~"
                    zeile = "%-30s %-9s %s" % (titel, self.m(e.get("stand"))[:9], self.m(e.get("autor"))[:19])
                    t = zeile[:B_LISTE].encode("latin-1")
                    feld[6:6 + len(t)] = t
                b += feld
            return 0, bytes(b)
        if wort == "VORSCHAU":
            e = self.eintrag(zahl)
            b = bytearray(3 * 68)
            if e:
                import textwrap
                d = e.get("datei") or {}
                kopf = "by " + str(e.get("autor", ""))
                if e.get("erschienen"):
                    kopf += "  -  " + str(e["erschienen"])
                kopf += "  -  %s  -  %s" % (d.get("name", ""), groesse_text(int(d.get("groesse") or 0)))
                bw = e.get("bewertung")
                if bw and bw.get("anzahl"):       # kurz, damit die Groesse noch passt
                    kopf = "%s %.1f  -  by %s  -  %s  -  %s" % (
                        sterne_text(bw["schnitt"]), float(bw["schnitt"]), e.get("autor", ""), d.get("name", ""),
                        groesse_text(int(d.get("groesse") or 0)))
                text = " ".join(self.text(e).split())
                z = textwrap.wrap(self.m(text), 67)
                if len(z) > 2:
                    z[1] = z[1][:63].rstrip() + " ..."
                zeilen = [self.m(kopf)[:67]] + z[:2]
                for k, s in enumerate(zeilen):
                    t = s.encode("latin-1")
                    b[k * 68:k * 68 + len(t)] = t
            return 0, bytes(b)
        if wort == "INFO":
            e = self.eintrag(zahl)
            if not e:
                return 2, b"No such entry"
            self.umbrechen(e)
            return 0, len(self.zeilen).to_bytes(2, "little")
        if wort == "ZEILEN":
            b = bytearray()
            with self.lock:
                zeilen = self.zeilen
            for art, t in zeilen[zahl:]:
                if len(b) + len(t) + 2 > 255:
                    break
                b += bytes([art]) + t + b"\0"
            return 0, bytes(b)
        if wort == "LADE":
            e = self.eintrag(zahl)
            if not e:
                return 2, b"No such entry"
            if self.laedt:
                return 2, b"Still loading"
            self.laedt = True
            self.l_status, self.l_text, self.l_prozent = 1, b"", 0
            threading.Thread(target=self.faden_laden, args=(e, "!" in teile[2:]), daemon=True).start()
            return 1, b""
        if wort == "LADESTAND":
            with self.lock:
                if self.laedt:
                    return 1, ("Loading %d %%" % self.l_prozent).encode()
                return self.l_status, self.l_text
        if wort == "MEINE":
            e = self.eintrag(zahl)
            return 0, bytes([self.meine(e) if e else 0])
        if wort == "BEWERTE":
            e = self.eintrag(zahl)
            try:
                sterne = int(teile[2])
            except (IndexError, ValueError):
                sterne = 0
            if not e or not 1 <= sterne <= 5:
                return 2, b"No such entry"
            if self.b_laeuft:
                return 2, b"Still sending"
            self.b_laeuft = True
            self.b_status, self.b_text = 1, b""
            threading.Thread(target=self.faden_bewerten, args=(e, sterne), daemon=True).start()
            return 1, b""
        if wort == "BEWSTAND":
            with self.lock:
                if self.b_laeuft:
                    return 1, b""
                return self.b_status, self.b_text + (b"\0" if self.b_status == 0 else b"")
        return 2, b"Unknown command"
