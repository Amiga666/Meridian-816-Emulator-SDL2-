/* DRAHT-Netzdienst: Fenster (Bank $FD) als gemeinsame Datei + Python-Dienst (dienst/draht.py). GPL-3.0-or-later */
#pragma once
#include "meridian.h"
/* Startet den Dienst; ordner = Laufwerk 3, port = Browser-Ablage (0 = aus). Liefert 0 bei Erfolg, sonst eine Meldung in err. */
int dienst_start(Machine *m, const char *script, const char *ordner, const char *python, int port, char *err, size_t errlen);
void dienst_stop(Machine *m);
const char *dienst_log(void);
int dienst_lebt(char *msg, size_t n);                 /* 1 = Prozess laeuft; 0 = beendet (msg: Grund + Protokollauszug) */
int dienst_rundlauf(Machine *m, char *out, size_t n);   /* Anfrage "HELP" ins Fenster, Antwort abwarten; 0 = ok */
const char *dienst_zertifikate(void);                 /* gewaehltes CA-Paket fuer HTTPS (leer = keines gefunden) */
int dienst_index_kopieren(const char *quelle, const char *ordner);   /* hvsc_index.tsv.gz nach <ordner>/HVSC/ (nur wenn dort keiner liegt) */
int dienst_beispiele_kopieren(const char *quelldir, const char *ordner);   /* C64GAMES.FIL und MERIDIAN.DRW nach <ordner>/files (nur wenn sie fehlen) */
int dienst_mkdirs(const char *pfad);                  /* Ordner samt Eltern anlegen (0 = ok) */
int dienst_python(char *out, size_t n, char *err, size_t en);   /* erstes brauchbares Python 3 suchen */
