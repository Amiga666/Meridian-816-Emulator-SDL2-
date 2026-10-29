#!/usr/bin/env python3
"""Wetter fuer den MERIDIAN - Teil des Netzdiensts (draht.py)

Der Dienst holt das Wetter selbst: beim ersten Mal den Ort aus der
IP-Adresse (ip-api.com), danach alle 15 Minuten die Werte von Open-Meteo
(aktuell und fuenf Tage). Ort und Einstellungen merkt er sich in
wetter.json im Ordner des MERIDIAN (games/MERIDIAN). Er fragt niemanden,
bevor die App WEATHER einmal offen war - erst dann entsteht wetter.json. Was er hat, legt er
fertig zum Zeigen bereit - die App WEATHER (horizont/apps/wetter.asm) und
die Menueleiste von HORIZONT rechnen nichts mehr.

Kurzform fuer die Menueleiste im Fenster ab $22 (schreibt nur der Dienst,
laufend, solange der MERIDIAN-Core laeuft):
   +0      "W", wenn etwas zu zeigen ist (sonst 0)
   +1      zaehlt bei jeder Aenderung
   +4      Temperatur, gerundet, mit Gradzeichen ("-12\xb0", mit 0, bis 5 Bytes)
   +$0E    Symbol: 16 Bytes Zeichenbild (8 Zeilen links, 8 Zeilen rechts)
Die Bibliothek zeigt das nur, solange der Herzschlag des Diensts ($10) geht.

Anfragen der App: Art 14, Befehl als Text.
   STAND           Status 0: Werte in der Antwort (Aufbau unten), 1: holt
                   noch, 2: Fehler (Text in der Antwort)
   NEU             jetzt neu holen (Status 1)
   SUCHE name      Ort suchen (Open-Meteo Geocoding), Status 1
   TREFFER         Status 1: sucht noch, 0: Antwort = Anzahl (Byte) und je
                   Treffer 48 Bytes Text, 2: Fehler
   WAEHLE n        Treffer n (0-4) wird der Ort, holt neu (Status 1)
   IP              Ort wieder aus der IP-Adresse, holt neu (Status 1)
   EINHEIT C|F     Grad Celsius (km/h) oder Fahrenheit (mph)
   LEISTE 0|1      Kurzform in der Menueleiste aus oder an

Antwort auf STAND (feste Stellen, Texte mit 0):
   $00  Ort (40)              $28  Zeit der Werte "HH:MM" (6)
   $2E  Symbol jetzt (0-10)   $2F  Bits: 0 Fahrenheit, 1 Leiste an,
                                     2 Ort aus der IP, 3 Werte veraltet
   $30  Temperatur "-12.5\xb0C" (8)  $38  Wetterlage (20)
   $4C  gefuehlt (8)          $54  Feuchte (8)     $5C  Wind (12)
   $68  Regen heute (6)
   $6E  fuenf Tage je 22 Bytes: Name (6), Symbol (1), Hoechst (5),
        Tiefst (5), Regen (5)
Symbole: 0 klar, 1 Nacht, 2 heiter, 3 Nacht mit Wolken, 4 bedeckt,
5 Nebel, 6 Niesel, 7 Regen, 8 Schauer, 9 Schnee, 10 Gewitter.
"""
import datetime
import json
import os
import threading
import time
import urllib.parse
import urllib.request

KURZ = 0x22
NEU_NACH = 15 * 60                  # so alt duerfen die Werte werden
FEHLER_PAUSE = 120                  # nach einem Fehler erst wieder nach 2 min
KOPF = {"User-Agent": "MERIDIAN-816-Weather/1.0 (retro-modern.net)"}

# WMO-Code -> (Text, Symbol bei Tag, Symbol bei Nacht)
WMO = [
    (0, "Clear sky", 0, 1), (1, "Mainly clear", 2, 3), (2, "Partly cloudy", 2, 3),
    (3, "Overcast", 4, 4), (48, "Fog", 5, 5), (55, "Drizzle", 6, 6),
    (57, "Freezing drizzle", 6, 6), (61, "Light rain", 6, 6), (65, "Rain", 7, 7),
    (67, "Freezing rain", 7, 7), (77, "Snow", 9, 9), (82, "Rain showers", 8, 8),
    (86, "Snow showers", 9, 9), (99, "Thunderstorm", 10, 10),
]
TAGE = ["Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"]
RICHTUNG = ["N", "NE", "E", "SE", "S", "SW", "W", "NW"]

# Kurzform-Symbole: 16 x 8 Punkte, zwei Zeichen, Farbe wie der Menuetext
MINI = [
    [".......##.......", "..#..######..#..", "....########....", "##.##########.##",
     "##.##########.##", "....########....", "..#..######..#..", ".......##......."],   # 0 klar
    ["....#####.......", "..#####.........", ".####........#..", ".####...........",
     ".####......#....", ".#####..........", "..#########.....", "....#####......."],   # 1 Nacht
    [".#.##.#.........", "..####..........", "######..####....", "..###.########..",
     ".#.#.##########.", "....############", "....############", ".....##########."],   # 2 heiter
    ["..####..........", ".###............", "###.....####....", "###...########..",
     ".###.##########.", "....############", "....############", ".....##########."],   # 3 Nacht, Wolken
    ["................", "......####......", "....########....", "..###########...",
     ".##############.", "################", "################", ".##############."],   # 4 bedeckt
    ["......####......", "....########....", "..############..", "................",
     "###############.", "................", "..##############", "................"],   # 5 Nebel
    ["......####......", "....########....", "..############..", ".##############.",
     "..############..", "................", "...#....#....#..", "..#....#....#..."],   # 6 Niesel
    ["......####......", "....########....", "..############..", ".##############.",
     "..############..", "..#..#..#..#..#.", ".#..#..#..#..#..", "#..#..#..#..#..."],   # 7 Regen
    ["#.#.............", ".###..#####.....", "#.#.#########...", "...###########..",
     "..############..", "................", "...#...#...#....", "..#...#...#....."],   # 8 Schauer
    ["......####......", "....########....", "..############..", ".##############.",
     "..############..", "................", ".#.#...#.#...#.#", "..#.....#.....#."],   # 9 Schnee
    ["......####......", "....########....", "..############..", ".##############.",
     "..#####..#####..", ".......##.......", "......###.......", ".......#........"],   # 10 Gewitter
]


def mini_bytes(nr):
    """16 Bytes: linkes Zeichen (8 Zeilen), dann rechtes"""
    z = MINI[nr]
    links = bytes(int(r[:8].replace(".", "0").replace("#", "1"), 2) for r in z)
    rechts = bytes(int(r[8:].replace(".", "0").replace("#", "1"), 2) for r in z)
    return links + rechts


def wetterlage(code, tag=True):
    for hoechst, text, s_tag, s_nacht in WMO:
        if code <= hoechst:
            return text, (s_tag if tag else s_nacht)
    return "Unknown", 4


class Wetter:
    def __init__(self, fenster, zu_meridian, ordner):
        self.m = fenster
        self.zu = zu_meridian
        self.pfad = os.path.join(ordner, "wetter.json")
        self.einst = {"ort": "", "lat": None, "lon": None, "aus_ip": True, "einheit": "C", "leiste": True}
        try:
            self.einst.update(json.load(open(self.pfad)))
        except (OSError, ValueError):
            pass
        self.daten = None               # letzte Antwort von Open-Meteo
        self.zeit = 0.0                 # wann geholt
        self.fehler = ""
        self.fehler_zeit = 0.0
        self.holt = False
        self.sucht = False
        self.treffer = []
        self.such_fehler = ""
        self.zaehler = 0
        self.kurz_alt = None
        self.lock = threading.Lock()
        self.benutzt = os.path.exists(self.pfad)    # erst nach der ersten Anfrage der App

    # -------------------------------------------------- Netz
    def json_holen(self, url):
        o = urllib.request.build_opener(urllib.request.ProxyHandler({}))
        with o.open(urllib.request.Request(url, headers=KOPF), timeout=10) as r:
            return json.loads(r.read().decode("utf-8"))

    def speichern(self):
        try:
            with open(self.pfad, "w") as f:
                json.dump(self.einst, f, ensure_ascii=False)
        except OSError as e:
            print("Wetter: Einstellungen nicht gespeichert:", e, flush=True)

    def holen(self):
        """im eigenen Faden: Ort (wenn noetig) und Wetter"""
        try:
            if self.einst["lat"] is None:
                ip = self.json_holen("http://ip-api.com/json/?fields=status,countryCode,regionName,city,lat,lon&lang=de")
                if ip.get("status") != "success":
                    raise RuntimeError("Location by IP failed")
                self.einst.update(ort="%s, %s, %s" % (ip["city"], ip["regionName"], ip["countryCode"]),
                                  lat=ip["lat"], lon=ip["lon"], aus_ip=True)
                self.speichern()
            url = ("https://api.open-meteo.com/v1/forecast?" + urllib.parse.urlencode({
                "latitude": self.einst["lat"], "longitude": self.einst["lon"],
                "current": "temperature_2m,apparent_temperature,relative_humidity_2m,weather_code,"
                           "is_day,wind_speed_10m,wind_direction_10m",
                "daily": "weather_code,temperature_2m_max,temperature_2m_min,precipitation_probability_max",
                "timezone": "auto", "forecast_days": 5}))
            d = self.json_holen(url)
            with self.lock:
                self.daten = d
                self.zeit = time.time()
                self.fehler = ""
            print("Wetter:", self.einst["ort"], d["current"]["temperature_2m"], flush=True)
        except Exception as e:
            with self.lock:
                self.fehler = "No connection to the weather service" if isinstance(e, OSError) else str(e)
                self.fehler_zeit = time.time()
            print("Wetter: Fehler", e, flush=True)
        finally:
            self.holt = False
            self.zaehler += 1

    def neu_holen(self):
        if not self.holt:
            self.holt = True
            threading.Thread(target=self.holen, daemon=True).start()

    def suchen(self, name):
        try:
            g = self.json_holen("https://geocoding-api.open-meteo.com/v1/search?" + urllib.parse.urlencode(
                {"name": name, "count": 5, "language": "de"}))
            treffer = []
            for r in g.get("results", [])[:5]:
                name, land = r.get("name", ""), r.get("country_code", "")
                # Deutschland und Oesterreich: der Landkreis, sonst Bundesland/Staat
                if land in ("DE", "AT"):
                    kreis = r.get("admin3") or r.get("admin2") or r.get("admin1") or ""
                else:
                    kreis = r.get("admin1") or ""
                for vorsatz in ("Landkreis ", "Regierungsbezirk ", "Kreisfreie Stadt ", "Kreis ",
                                "Politischer Bezirk ", "Statutarstadt "):
                    kreis = kreis.replace(vorsatz, "")
                if kreis == name or kreis.startswith(name + ","):   # "Berlin, Stadt"
                    kreis = ""
                platz = 38 - len(name) - len(land)          # passt in die Liste der App (43)
                if len(kreis) > platz:
                    kreis = kreis[:max(0, platz - 1)] + "." if platz > 3 else ""
                ort = ", ".join(t for t in (name, kreis, land) if t)
                treffer.append({"ort": ort, "lat": r["latitude"], "lon": r["longitude"]})
            self.treffer = treffer
            self.such_fehler = ""
        except Exception as e:
            self.treffer = []
            self.such_fehler = "No connection to the weather service" if isinstance(e, OSError) else str(e)
        finally:
            self.sucht = False

    # -------------------------------------------------- Zeigen
    def grad(self, c, stellen=0):
        """Celsius -> Text in der gewaehlten Einheit, mit Gradzeichen"""
        if self.einst["einheit"] == "F":
            c = c * 9 / 5 + 32
        if stellen:
            return "%.1f\xb0" % c
        n = round(c)
        return "%d\xb0" % (0 if n == 0 else n)        # nie "-0"

    def stand(self):
        """Antwort auf STAND (feste Stellen, siehe oben)"""
        d = self.daten
        c, t = d["current"], d["daily"]
        f = self.einst["einheit"] == "F"
        b = bytearray(0xDC)

        def feld(stelle, laenge, s):
            s = self.zu(s)[:laenge - 1]
            b[stelle:stelle + len(s)] = s

        lage, symbol = wetterlage(c["weather_code"], c.get("is_day", 1) == 1)
        feld(0x00, 40, self.einst["ort"])
        feld(0x28, 6, c["time"][11:16])
        b[0x2E] = symbol
        b[0x2F] = (1 if f else 0) | (2 if self.einst["leiste"] else 0) | (4 if self.einst["aus_ip"] else 0) | \
                  (8 if time.time() - self.zeit > 2 * NEU_NACH else 0)
        feld(0x30, 8, self.grad(c["temperature_2m"], 1) + ("F" if f else "C"))
        feld(0x38, 20, lage)
        feld(0x4C, 8, self.grad(c["apparent_temperature"]) + ("F" if f else "C"))
        feld(0x54, 8, "%d %%" % c["relative_humidity_2m"])
        wind = c["wind_speed_10m"] / 1.609344 if f else c["wind_speed_10m"]
        feld(0x5C, 12, "%d %s %s" % (round(wind), "mph" if f else "km/h",
                                     RICHTUNG[int((c["wind_direction_10m"] + 22.5) // 45) % 8]))
        feld(0x68, 6, "%d %%" % (t["precipitation_probability_max"][0] or 0))
        for i in range(min(5, len(t["time"]))):
            s = 0x6E + i * 22
            name = "Today" if i == 0 else TAGE[datetime.date.fromisoformat(t["time"][i]).weekday()]
            feld(s, 6, name)
            b[s + 6] = wetterlage(t["weather_code"][i])[1]
            feld(s + 7, 5, self.grad(t["temperature_2m_max"][i]))
            feld(s + 12, 5, self.grad(t["temperature_2m_min"][i]))
            feld(s + 17, 5, "%d %%" % (t["precipitation_probability_max"][i] or 0))
        return bytes(b)

    def kurz_schreiben(self):
        """Kurzform fuer die Menueleiste (nur der Hauptfaden schreibt)"""
        with self.lock:
            d = self.daten
        if not (d and self.einst["leiste"]):
            neu = None
        else:
            c = d["current"]
            symbol = wetterlage(c["weather_code"], c.get("is_day", 1) == 1)[1]
            neu = (self.zu(self.grad(c["temperature_2m"]))[:4], mini_bytes(symbol))
        if neu == self.kurz_alt and self.m[KURZ] == (ord("W") if neu else 0):
            return
        self.kurz_alt = neu
        m = self.m
        if not neu:
            m[KURZ] = 0
            return
        text, bild = neu
        m[KURZ + 4:KURZ + 9] = (text + b"\0" * 5)[:5]
        m[KURZ + 0x0E:KURZ + 0x1E] = bild
        m[KURZ + 1] = (m[KURZ + 1] + 1) & 0xFF
        m[KURZ] = ord("W")                              # zuletzt

    def pflegen(self):
        """aus der Hauptschleife: neu holen, wenn faellig; Kurzform schreiben"""
        if not self.benutzt:
            return
        jetzt = time.time()
        if not self.holt and jetzt - self.zeit > NEU_NACH and jetzt - self.fehler_zeit > FEHLER_PAUSE:
            self.neu_holen()
        self.kurz_schreiben()

    # -------------------------------------------------- Anfragen der App
    def anfrage(self, text):
        teile = text.decode("latin-1").strip().split(None, 1)
        wort = teile[0].upper() if teile else "STAND"
        rest = teile[1].strip() if len(teile) > 1 else ""
        if not self.benutzt and not (wort == "LEISTE" and rest == "0"):
            self.benutzt = True         # (LEISTE 0 aus SETTINGS weckt ihn nicht)
            self.speichern()
        if wort == "STAND":
            with self.lock:
                if self.daten:
                    return 0, self.stand()
                fehler = self.fehler
            if self.holt or not fehler:
                if not self.holt:
                    self.neu_holen()
                return 1, b""
            return 2, self.zu(fehler)
        if wort == "NEU":
            self.fehler_zeit = 0.0
            self.neu_holen()
            return 1, b""
        if wort == "SUCHE":
            if not rest:
                return 2, b"Type a place name"
            if not self.sucht:
                self.sucht = True
                self.treffer = []
                self.such_fehler = ""
                threading.Thread(target=self.suchen, args=(rest,), daemon=True).start()
            return 1, b""
        if wort == "TREFFER":
            if self.sucht:
                return 1, b""
            if self.such_fehler:
                return 2, self.zu(self.such_fehler)
            b = bytearray([len(self.treffer)])
            for t in self.treffer:
                s = self.zu(t["ort"])[:47]
                b += s + bytes(48 - len(s))
            return 0, bytes(b)
        if wort == "WAEHLE":
            try:
                t = self.treffer[int(rest)]
            except (ValueError, IndexError):
                return 2, b"No such place"
            self.einst.update(ort=t["ort"], lat=t["lat"], lon=t["lon"], aus_ip=False)
            self.speichern()
            with self.lock:
                self.daten = None
            self.fehler_zeit = 0.0
            self.neu_holen()
            return 1, b""
        if wort == "IP":
            self.einst.update(ort="", lat=None, lon=None, aus_ip=True)
            with self.lock:
                self.daten = None
            self.fehler_zeit = 0.0
            self.neu_holen()
            return 1, b""
        if wort == "EINHEIT":
            self.einst["einheit"] = "F" if rest.upper().startswith("F") else "C"
            self.speichern()
            self.kurz_alt = None
            return 0, b""
        if wort == "LEISTE":
            self.einst["leiste"] = rest != "0"
            self.speichern()
            return 0, b""
        return 2, b"Unknown command"
