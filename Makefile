# MERIDIAN 816 Emulator – macOS / Linux / Windows (MSYS2)
#   macOS:   brew install sdl2
#   Linux:   sudo apt install libsdl2-dev
#   Windows: MSYS2: pacman -S mingw-w64-x86_64-SDL2 make gcc
CC      ?= cc
CXX     ?= c++
CFLAGS  ?= -O2 -Wall -Wno-misleading-indentation
SDLCFG  ?= sdl2-config
SRC      = src/cpu65816.c src/meridian.c src/orgel.c src/dienst.c
SIDKERN  = $(wildcard tools/sidkern/*.c)
SIDTON   = tools/sidton/sidton.cpp $(addprefix tools/sidton/resid/,sid.cc voice.cc wave.cc envelope.cc filter.cc dac.cc extfilt.cc pot.cc version.cc)

all: meridian dienst/bin/mpg123 dienst/bildwandler dienst/sidkern dienst/sidton

meridian: $(SRC) src/main.c src/meridian.h src/cpu65816.h src/orgel.h src/dienst.h src/version.h
	$(CC) $(CFLAGS) $$($(SDLCFG) --cflags) -o $@ $(SRC) src/main.c $$($(SDLCFG) --libs)

headless: $(SRC) src/headless.c           # Testlauf ohne SDL
	$(CC) $(CFLAGS) -Isrc -o meridian-headless $(SRC) src/headless.c

# Radio-Dekoder: winziger MP3-Dekoder, der sich wie "mpg123" aufrufen laesst (siehe tools/mpg123mini.c)
dienst/bin/mpg123: tools/mpg123mini.c tools/minimp3.h
	mkdir -p dienst/bin
	$(CC) $(CFLAGS) -o $@ tools/mpg123mini.c -lm

# Bildwandler (JPG/PNG/GIF fuer PICTURES, PAINT, WRITE und den Druck): der Netzdienst findet ihn neben bild_dienst.py.
# Quelle und stb_image von Sean Barrett (Public Domain/MIT) liegen in tools/bildwandler/.
dienst/bildwandler: tools/bildwandler/bildwandler.c tools/bildwandler/stb/stb_image.h tools/bildwandler/stb/stb_image_write.h
	$(CC) -O2 -Wno-unused-function -o $@ tools/bildwandler/bildwandler.c -lm

# SID JAM: sidkern laesst die Abspielroutine eines SIDs auf einem 6510 laufen und schreibt den
# Registerstrom (SJE) heraus, sidton macht daraus mit reSID den Klang "Original". Der Netzdienst
# findet beide neben sidjam_dienst.py. Quellen in tools/sidkern/ und tools/sidton/ (GPL-3.0-or-later
# bzw. reSID von Dag Lem, GPL-2.0-or-later); beide brauchen nur einen Compiler, keine Bibliothek.
dienst/sidkern: $(SIDKERN) tools/sidkern/kern.h tools/sidkern/siddatei.h
	$(CC) $(CFLAGS) -o $@ $(SIDKERN)

dienst/sidton: $(SIDTON)
	$(CXX) -isysroot /Library/Developer/CommandLineTools/SDKs/MacOSX.sdk -I/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk/usr/include/c++/v1 -std=c++11 -w -Itools/sidton/resid -DVERSION='"1.0-sidjam"' -o dienst/sidton tools/sidton/sidton.cpp tools/sidton/resid/sid.cc tools/sidton/resid/voice.cc tools/sidton/resid/wave.cc tools/sidton/resid/envelope.cc tools/sidton/resid/filter.cc tools/sidton/resid/dac.cc tools/sidton/resid/extfilt.cc tools/sidton/resid/pot.cc tools/sidton/resid/version.cc

clean:
	rm -f meridian meridian-headless dienst/bin/mpg123 dienst/bildwandler dienst/sidkern dienst/sidton
