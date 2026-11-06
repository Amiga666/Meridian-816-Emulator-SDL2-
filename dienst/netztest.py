#!/usr/bin/env python3
"""Netzwerk-Selbsttest fuer WEATHER und RADIO (MERIDIAN 816 Emulator).

Macht dieselben Aufrufe wie wetter_dienst.py und radio_dienst.py (ohne Proxy,
Zeitlimit 10 s) und zeigt Schritt fuer Schritt, wo es haengt: Namensaufloesung,
IPv4/IPv6, TCP-Verbindung, HTTPS-Zertifikate, Antwort des Servers.

Aufruf (mit demselben Python wie der Emulator):
    python dienst/netztest.py            (unter Windows ggf.:  py -3 dienst\\netztest.py)
Die Ausgabe bitte komplett kopieren.
"""
import os
import platform
import socket
import ssl
import sys
import time
import urllib.error
import urllib.request

KOPF = {"User-Agent": "MERIDIAN-816 netztest"}
ZIELE = [                                   # (Name, Host, Port, Zweck)
    ("Wetter-Ort (http)", "ip-api.com", 80),
    ("Wetter-Daten (https)", "api.open-meteo.com", 443),
    ("Radio-Sender (http)", "relay3.slayradio.org", 8000),
]
URL_ORT = "http://ip-api.com/json/?fields=status,countryCode,regionName,city,lat,lon"
URL_WETTER = ("https://api.open-meteo.com/v1/forecast?latitude=52.52&longitude=13.41"
              "&current=temperature_2m&timezone=auto&forecast_days=1")
URL_RADIO = "http://relay3.slayradio.org:8000/"


def zeit():
    return time.time()


def kopf(text):
    print("\n=== " + text)


def fehler_text(e):
    return "%s: %s" % (type(e).__name__, e)


def umgebung():
    kopf("Python und Umgebung")
    print("Python      :", sys.version.split()[0], "(" + platform.machine() + ")", sys.executable)
    print("System      :", platform.platform())
    print("OpenSSL     :", ssl.OPENSSL_VERSION)
    for v in ("SSL_CERT_FILE", "SSL_CERT_DIR", "HTTP_PROXY", "HTTPS_PROXY", "ALL_PROXY", "NO_PROXY",
              "http_proxy", "https_proxy", "PYTHONUTF8", "MERIDIAN_PYTHON"):
        if os.environ.get(v):
            print("Umgebung    : %s=%s" % (v, os.environ[v]))
    try:
        c = ssl.create_default_context()
        print("Zertifikate :", c.cert_store_stats())
    except Exception as e:
        print("Zertifikate : FEHLER", fehler_text(e))
    try:
        import certifi
        print("certifi     :", certifi.where())
    except ImportError:
        print("certifi     : nicht installiert (unter Windows nicht noetig)")
    try:
        import urllib.request as u
        print("System-Proxy:", u.getproxies() or "keiner")
    except Exception as e:
        print("System-Proxy: ?", fehler_text(e))


def namen():
    kopf("1. Namensaufloesung (DNS)")
    adressen = {}
    for name, host, port in ZIELE:
        t = zeit()
        try:
            r = socket.getaddrinfo(host, port, type=socket.SOCK_STREAM)
            liste = []
            for fam, _, _, _, sa in r:
                liste.append(("IPv6 " if fam == socket.AF_INET6 else "IPv4 ") + sa[0])
            adressen[host] = [(fam, sa) for fam, _, _, _, sa in r]
            print("%-24s %5.2f s  %s" % (host, zeit() - t, ", ".join(dict.fromkeys(liste))))
        except Exception as e:
            adressen[host] = []
            print("%-24s %5.2f s  FEHLER %s" % (host, zeit() - t, fehler_text(e)))
    return adressen


def tcp(adressen):
    kopf("2. TCP-Verbindung zu jeder Adresse (5 s Zeitlimit)")
    for name, host, port in ZIELE:
        for fam, sa in adressen.get(host, []):
            t = zeit()
            s = socket.socket(fam, socket.SOCK_STREAM)
            s.settimeout(5)
            try:
                s.connect(sa)
                ergebnis = "ok"
            except Exception as e:
                ergebnis = "FEHLER " + fehler_text(e)
            finally:
                s.close()
            print("%-24s %-6s %-40s %5.2f s  %s" % (host + ":" + str(port), "IPv6" if fam == socket.AF_INET6 else "IPv4",
                                                    sa[0], zeit() - t, ergebnis))


def http(titel, url, lesen=300):
    t = zeit()
    try:
        o = urllib.request.build_opener(urllib.request.ProxyHandler({}))
        with o.open(urllib.request.Request(url, headers=KOPF), timeout=10) as r:
            daten = r.read(lesen)
            print("%-22s %5.2f s  ok  HTTP %s  %d Bytes gelesen  %s" % (titel, zeit() - t, r.status, len(daten),
                  (r.headers.get("icy-name") or "")[:30]))
            return True
    except Exception as e:
        print("%-22s %5.2f s  FEHLER %s" % (titel, zeit() - t, fehler_text(e)))
        return False


def anfragen():
    kopf("3. Die Aufrufe des Dienstes (wie wetter_dienst.py / radio_dienst.py)")
    a = http("Wetter: Ort per IP", URL_ORT)
    b = http("Wetter: Daten", URL_WETTER)
    c = http("Radio: Stream", URL_RADIO, 4096)
    return a, b, c


def nur_ipv4():
    kopf("4. Dasselbe, aber nur mit IPv4")
    alt = socket.getaddrinfo

    def v4(host, port, family=0, type=0, proto=0, flags=0):
        return alt(host, port, socket.AF_INET, type, proto, flags)
    socket.getaddrinfo = v4
    try:
        return anfragen_kurz()
    finally:
        socket.getaddrinfo = alt


def anfragen_kurz():
    a = http("Wetter: Ort per IP", URL_ORT)
    b = http("Wetter: Daten", URL_WETTER)
    c = http("Radio: Stream", URL_RADIO, 4096)
    return a, b, c


def auswertung(normal, v4):
    kopf("Auswertung")
    if all(normal):
        print("Alles in Ordnung: Python kommt ins Netz. Dann liegt das Problem nicht an der Verbindung -")
        print("bitte meridian-draht.log nach dem Start von WEATHER/RADIO schicken (mindestens 15 Sekunden laufen lassen).")
    elif all(v4) and not all(normal):
        print("Mit IPv4 allein klappt es, mit dem Standard nicht: IPv6 ist bei dir defekt oder wird gefiltert.")
        print("Abhilfe (kurzfristig): IPv6 im Netzwerkadapter abschalten, oder mir die Ausgabe schicken -")
        print("dann baue ich eine IPv4-Bevorzugung in den Emulator ein.")
    elif not any(normal) and not any(v4):
        print("Python kommt gar nicht ins Netz, obwohl der Browser es tut. Wahrscheinlich:")
        print(" - Firewall/Virenscanner blockiert python.exe (Fenster 'Zugriff erlauben?' ggf. hinter anderen Fenstern),")
        print("   dann python.exe (siehe oben 'Python :') fuer ausgehende Verbindungen freigeben;")
        print(" - Firmennetz mit Proxy (der Emulator fragt den Proxy absichtlich nicht).")
    elif normal[1] is False and normal[0] and normal[2]:
        print("HTTP klappt, HTTPS nicht: Zertifikatsproblem. SSL_CERT_FILE zeigt evtl. auf eine fehlende Datei")
        print("(Umgebung oben pruefen), oder ein Virenscanner prueft HTTPS mit eigenem Zertifikat.")
    else:
        print("Gemischtes Ergebnis - bitte die ganze Ausgabe schicken.")


def main():
    umgebung()
    adressen = namen()
    tcp(adressen)
    normal = anfragen()
    v4 = nur_ipv4() if not all(normal) else normal
    auswertung(normal, v4)


if __name__ == "__main__":
    main()
