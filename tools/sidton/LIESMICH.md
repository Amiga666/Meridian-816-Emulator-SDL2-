# sidton – Registerstrom (SJE) → Ton mit reSID

`sidton` liest den Strom von `sidkern` (siehe `../sidkern/SJE.md`) und lässt die
Schreibzugriffe zur richtigen Zeit auf einen bis drei reSID-Chips los. Heraus
kommt der Klang „ORIGINAL“ von SID JAM: Mono, 8 Bit mit Vorzeichen, so wie die
Samplekanäle der ORGEL ihn spielen (zum Prüfen mit `--s16` auch 16 Bit).

## Bauen

Im Emulator baut `make` das Programm als `dienst/sidton`:

```
c++ -O2 -std=c++11 -w -Itools/sidton/resid -DVERSION='"1.0-sidjam"' \
    -o dienst/sidton tools/sidton/sidton.cpp \
    tools/sidton/resid/{sid,voice,wave,envelope,filter,dac,extfilt,pot,version}.cc
```

Nur ein C++-Compiler, keine Bibliothek. Die Wellenform-Tabellen (`wave*.h`)
liegen schon fertig daneben; im MERIDIAN-Projekt erzeugt `bauen.sh` sie aus den
`.dat`-Dateien von reSID (wie dessen `samp2src.pl`).

## Aufruf

```
sidton [-r RATE] [--s16] [--fein] [--fast|--interp|--resample] [--ueber N]
       [--modell 6581|8580] [--verstaerkung F] [--stumm MASKE]   < DATEI.sje
```

Der Netzdienst ruft es als `sidton --fein -r 22050 --stumm N` auf und schiebt
den Strom durch eine Leitung hinein. `--fein` rechnet mit Überabtastung
(sauberer, teurer), `--stumm` ist eine Bitmaske für die drei Chips.

## Herkunft und Lizenz

`sidton.cpp`: © 2026 retro-modern.net, GPL-3.0-or-later. Gegenüber dem Paket
steht hier nur ein Schutz mehr: `M_PI` wird nachdefiniert, falls der Compiler es
in `-std=c++11` nicht kennt (mingw).
`resid/`: reSID von Dag Lem, GPL-2.0-or-later — unverändert aus
<https://github.com/libsidplayfp/resid> (Stand siehe `resid/QUELLE.txt`), nur
`siddefs.h` ist aus `siddefs.h.in` entstanden. Lizenztext in `resid/COPYING`.
Dieselben Quellen liegen dem Netzdienst-Paket des MiSTer als `sidton-quelle/`
bei.
