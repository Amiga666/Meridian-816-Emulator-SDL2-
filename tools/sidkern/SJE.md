# SJE – der Registerstrom von sidkern

Auszug aus `SCHNITTSTELLEN.md` von SID JAM (Abschnitte 1 und 2): das Format,
in dem `sidkern` den Registerstrom herausschreibt, und sein Aufruf. `sidton`
liest genau diesen Strom.

## 1. SJE – der Registerstrom („SID JAM Ereignisse“)

`sidkern` lässt die Abspielroutine eines SIDs laufen und schreibt jeden
Schreibzugriff auf ein SID-Register mit seinem Zeitpunkt in Taktzyklen des
C64 heraus.

```
+0   4 Bytes   "SJE1"
+4   2 Bytes   n = Länge des JSON-Kopfs (Little Endian)
+6   n Bytes   JSON-Kopf (UTF-8), siehe unten
danach Datensätze bis zum Ende:
     varint d  Zyklen seit dem vorigen Datensatz (LEB128: 7 Bit je Byte,
               niedrigste zuerst, Bit 7 = es folgt ein Byte)
     byte r    0x00-0x5F: Register. Bit 0-4 Registernummer 0-24,
                          Bit 5-6 Chip 0-2 (0 = erster SID, meist $D400)
               0x7F:      Ende des Stroms (kein Wert, kein weiteres Byte)
     byte v    geschriebener Wert (nur bei Registern)
```

Der erste Datensatz zählt ab dem Startpunkt des Stroms (Zyklus
`start_cycles` im Lied). Mit `--ab` beginnt der Strom mit einem
vollständigen Abbild aller Register (je Chip 0 bis 24 in dieser
Reihenfolge, alle mit d = 0, außer dem ersten), danach geht es normal
weiter. Doppelte Schreibzugriffe mit gleichem Wert bleiben drin
(Gate-Wechsel innerhalb eines Bildes sind wichtig). Liedzeit 0 ist der
Aufruf von init. Die Ende-Marke trägt in d den Abstand vom letzten
Datensatz bis zum Ende des Stroms (`-t` oder Halt der CPU).

JSON-Kopf:

| Feld | |
|---|---|
| `title`, `author`, `released` | aus dem SID-Kopf (Latin-1 → UTF-8) |
| `format` | `"PSID"` oder `"RSID"`, `version` 1–4 |
| `song`, `songs`, `start_song` | Lied (1-basiert), Anzahl, Startlied laut Kopf |
| `clock` | `"PAL"` oder `"NTSC"` (wie gespielt) |
| `cps` | Zyklen je Sekunde (PAL 985248, NTSC 1022727) |
| `model` | `"6581"` oder `"8580"` (wie gespielt) |
| `sids` | Liste der Basisadressen, z. B. `[54272]` oder `[54272, 54304]` |
| `speed` | `"vbi"` oder `"cia"` für das gewählte Lied |
| `load`, `init`, `play` | Adressen (play 0 = eigener Interrupt) |
| `start_cycles` | Zyklus, ab dem der Strom zählt (0 oder `--ab`) |
| `md5` | MD5 der ganzen Datei (Schlüssel in `Songlengths.md5`) |
| `basic` | true, wenn das Lied BASIC braucht (dann nicht spielbar) |

## 2. sidkern – Aufruf

```
sidkern DATEI.sid [-s LIED] [-t SEKUNDEN] [--ab SEKUNDEN] [-o AUS.sje]
                  [--model 6581|8580] [--pal|--ntsc] [--text] [--info]
```

- `-s` Lied (1-basiert, Vorgabe Startlied), `-t` Ende in Liedzeit (Vorgabe 600 s),
  `--ab` stilles Vorspulen, `-o` Datei (Vorgabe stdout), `--text` lesbare Zeilen
  statt SJE, `--info` nur den JSON-Kopf ausgeben.
- Schreibt in Stücken und wartet, wenn die Leitung voll ist – so bremst der Leser.
- `--text`: erste Zeile `# {JSON-Kopf}`, dann je Schreibzugriff
  `ZYKLUS CHIP REG WERT` (Zyklus dezimal, Register und Wert zweistellig hex),
  zum Schluss `# ende ZYKLUS`. `--stat` meldet eine Zusammenfassung auf stderr.
- Rückgabe: 0 gut (auch wenn der Leser die Leitung schließt – dann still),
  1 Aufruf- oder Dateifehler, 2 keine gültige SID-Datei, 3 Lied braucht BASIC
  (Strom nur Kopf und Ende-Marke). Hält die CPU an (JAM), endet der Strom dort
  mit der Ende-Marke (Rückgabe 0).
- Gebaut für macOS, Linux x86_64 (Server) und Linux ARMv7 (MiSTer, statisch).

