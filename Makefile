# MERIDIAN 816 Emulator – macOS / Linux / Windows (MSYS2)
#   macOS:   brew install sdl2
#   Linux:   sudo apt install libsdl2-dev
#   Windows: MSYS2: pacman -S mingw-w64-x86_64-SDL2 make gcc
CC      ?= cc
CFLAGS  ?= -O2 -Wall -Wno-misleading-indentation
SDLCFG  ?= sdl2-config
SRC      = src/cpu65816.c src/meridian.c src/orgel.c src/dienst.c

all: meridian dienst/bin/mpg123 dienst/bildwandler

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

clean:
	rm -f meridian meridian-headless dienst/bin/mpg123 dienst/bildwandler
