# MERIDIAN 816 Emulator – macOS / Linux / Windows (MSYS2)
#   macOS:   brew install sdl2
#   Linux:   sudo apt install libsdl2-dev
#   Windows: MSYS2 (MINGW64-Shell): pacman -S make mingw-w64-x86_64-gcc mingw-w64-x86_64-SDL2 mingw-w64-x86_64-python
#            (Python 3.8+ fuer den Netzdienst DRAHT; ohne Python laeuft der Emulator, nur Laufwerk 3 und NET fehlen)
CC      ?= cc
CXX     ?= c++
CFLAGS  ?= -O2 -Wall -Wno-misleading-indentation
SDLCFG  ?= sdl2-config
SRC      = src/cpu65816.c src/meridian.c src/orgel.c src/dienst.c
SIDKERN  = $(wildcard tools/sidkern/*.c)
# Plattform: auf dem Mac findet der C++-Compiler manchmal die Standard-Header nicht (<cmath>), wenn die
# Kommandozeilenwerkzeuge unvollstaendig oder nach einem Update verstellt sind. Dann hilft das SDK von Hand.
# Nur auf macOS; auf Linux und Windows (MSYS2) bleibt CXXSYS leer.
UNAME_S  ?= $(shell uname -s)
UNAME_M  ?= $(shell uname -m)
CXXSYS   =
# Windows (MSYS2/MinGW/Cygwin): der Compiler haengt .exe an; die Ziele muessen so heissen, sonst baut make jedes Mal neu
# und der Netzdienst findet bildwandler.exe, sidkern.exe, sidton.exe nicht.
EXE      =
ifneq (,$(filter MINGW% MSYS% CYGWIN%,$(UNAME_S)))
EXE      = .exe
endif
ifeq ($(UNAME_S),Darwin)
MACSDK  ?= $(firstword $(shell xcrun --show-sdk-path 2>/dev/null) /Library/Developer/CommandLineTools/SDKs/MacOSX.sdk)
ifneq ($(wildcard $(MACSDK)/usr/include/c++/v1/cmath),)
CXXSYS   = -isysroot $(MACSDK) -I$(MACSDK)/usr/include/c++/v1
endif
endif
SIDTON   = tools/sidton/sidton.cpp $(addprefix tools/sidton/resid/,sid.cc voice.cc wave.cc envelope.cc filter.cc dac.cc extfilt.cc pot.cc version.cc)

all: meridian$(EXE) dienst/bin/mpg123$(EXE) dienst/bildwandler$(EXE) dienst/sidkern$(EXE) dienst/sidton$(EXE)

# Kurzname auch unter Windows: "make meridian" baut meridian.exe
ifneq ($(EXE),)
meridian: meridian$(EXE)
endif

# Die gebauten Programme gelten nur fuer die Plattform, auf der sie entstanden sind (macOS, Linux, ...).
# Liegt der Ordner auf einem geteilten Laufwerk und wird auf einem zweiten System weitergebaut, wuerde make die
# fremden Programme sonst fuer aktuell halten (Linux: "Permission denied"/"Exec format error", Fehler 13 bzw. 8).
# Der Stempel .plattform aendert sich dann, und alles wird neu gebaut. Er bleibt unberuehrt, solange die Plattform gleich ist.
.plattform: FORCE
	@echo "$(UNAME_S)-$(UNAME_M)" | cmp -s - $@ || echo "$(UNAME_S)-$(UNAME_M)" > $@
FORCE:

meridian$(EXE): .plattform $(SRC) src/main.c src/meridian.h src/cpu65816.h src/orgel.h src/dienst.h src/version.h
	$(CC) $(CFLAGS) $$($(SDLCFG) --cflags) -o $@ $(SRC) src/main.c $$($(SDLCFG) --libs)

headless: .plattform $(SRC) src/headless.c           # Testlauf ohne SDL
	$(CC) $(CFLAGS) -Isrc -o meridian-headless $(SRC) src/headless.c

# Radio-Dekoder: winziger MP3-Dekoder, der sich wie "mpg123" aufrufen laesst (siehe tools/mpg123mini.c)
dienst/bin/mpg123$(EXE): .plattform tools/mpg123mini.c tools/minimp3.h
	mkdir -p dienst/bin
	$(CC) $(CFLAGS) -o $@ tools/mpg123mini.c -lm

# Bildwandler (JPG/PNG/GIF fuer PICTURES, PAINT, WRITE und den Druck): der Netzdienst findet ihn neben bild_dienst.py.
# Quelle und stb_image von Sean Barrett (Public Domain/MIT) liegen in tools/bildwandler/.
dienst/bildwandler$(EXE): .plattform tools/bildwandler/bildwandler.c tools/bildwandler/stb/stb_image.h tools/bildwandler/stb/stb_image_write.h
	$(CC) -O2 -Wno-unused-function -o $@ tools/bildwandler/bildwandler.c -lm

# SID JAM: sidkern laesst die Abspielroutine eines SIDs auf einem 6510 laufen und schreibt den
# Registerstrom (SJE) heraus, sidton macht daraus mit reSID den Klang "Original". Der Netzdienst
# findet beide neben sidjam_dienst.py. Quellen in tools/sidkern/ und tools/sidton/ (GPL-3.0-or-later
# bzw. reSID von Dag Lem, GPL-2.0-or-later); beide brauchen nur einen Compiler, keine Bibliothek.
dienst/sidkern$(EXE): .plattform $(SIDKERN) tools/sidkern/kern.h tools/sidkern/siddatei.h
	$(CC) $(CFLAGS) -o $@ $(SIDKERN)

dienst/sidton$(EXE): .plattform $(SIDTON)
	$(CXX) -O2 $(CXXSYS) -std=c++11 -w -Itools/sidton/resid -DVERSION='"1.0-sidjam"' -o $@ $(SIDTON)

clean:
	rm -f meridian meridian.exe meridian-headless meridian-headless.exe dienst/bin/mpg123 dienst/bin/mpg123.exe dienst/bildwandler dienst/bildwandler.exe dienst/sidkern dienst/sidkern.exe dienst/sidton dienst/sidton.exe .plattform
