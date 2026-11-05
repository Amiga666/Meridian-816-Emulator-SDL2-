# Changelog

## 0.8.4
- **Fix (Linux): umlauts (ä, ö, ü, ß) could not be typed with the German keyboard layout, although they worked with the US layout.** The emulator translates keys by
  physical position only, so both layouts produce the same scancodes. The cause was SDL's text input, which SDL2 leaves on by default: with an input method running
  (IBus is the default on Linux Mint; `XMODIFIERS`, `GTK_IM_MODULE` and `QT_IM_MODULE` set to `ibus`) SDL asks it about every key, and IBus claims the keys that yield a non-ASCII
  character. SDL then sends no `KEYDOWN` at all, so the MERIDIAN never sees the key. With the US layout the same physical keys yield `;`, `'` and `[` (ASCII), which IBus passes on,
  so nothing was noticed. macOS has no such input method, so it was never affected.
  The emulator now turns SDL's text input off after creating the window (`SDL_StopTextInput()`, plus `SDL_EventState` for `SDL_TEXTINPUT` and `SDL_TEXTEDITING`) and does it again
  whenever the window gains focus, in case a window manager or input method switches it back on. This is done on every system except macOS, where the code is proven and the problem does not exist.
  New environment variables: `MERIDIAN_TASTEN_DEBUG=1` prints every key event to stderr (scancode, key name); `MERIDIAN_TEXTEINGABE=1` leaves the text input on, as before (for comparison).
- Tested on Linux X11 with the real emulator (TigerVNC as X server, IBus running with the variables above, German layout; six keys ö ä ü ß y/z a, then three more after the
  window had lost and regained focus; counted by `MERIDIAN_TASTEN_DEBUG`):
  - SDL 2.30.0, 2.26.5, 2.0.20, 2.0.14: with `MERIDIAN_TEXTEINGABE=1` (old behaviour) 2 of 6 keys and 0 of 3 after the focus change arrive; with the fix 6 of 6 and 3 of 3.
  - SDL 2.0.10 and 2.0.8: the problem does not occur here, 6 of 6 either way; the fix is harmless there. (These two were built from source without audio, so the test used a copy of
    `main.c` that initialises video only; the key handling is identical.)
  - With the fix, also 6 of 6 for: US layout with IBus, German layout without IBus, US layout without IBus (SDL 2.30.0).
  - Without a rebuild, `IBUS_ADDRESS=unix:path=/tmp/nothing ./meridian` also worked (it hides the IBus daemon from SDL); clearing `XMODIFIERS`, `GTK_IM_MODULE`, `QT_IM_MODULE` or setting
    `SDL_IM_MODULE=none` did not help.
- **Limits of these tests:** run in a sandbox, not on Linux Mint itself; IBus there may be configured differently. **Fcitx** (the other common input method) could not be reproduced: it was in the
  "off" state and passed all keys with and without the fix, so nothing can be said about it; the fix disables the same mechanism for it. Dead keys (`^`, `´`) were not measured.
  Wayland sessions, other input methods (SCIM, uim, XIM servers) and Windows were not tested; on Windows the same call is made, untested.

## 0.8.3
- **Documentation:** `README.md` is now bilingual (German and English) and lists every third-party program, library and data source with its origin and license
  (section "Sources and licenses"). This changelog is English only. Two outdated passages in the README were corrected: it claimed drive 3 did not exist yet,
  and one sentence in "Programs and modules" was duplicated. The version number is unchanged; this is a re-issue of the 0.8.3 package.
- **Fix: the calendar clock stopped.** `update_rtc()` ran only at reset, after which PFORTE `$30–$36` showed the emulator's start time forever, so the clock in
  HORIZONT's menu bar stood still. Now it behaves like `rtl/pforte.sv` of the core: the clock keeps running on its own (host clock plus an offset), writing `$37`
  latches the current value, reading `$30–$36` always returns the latched value (so the seven bytes belong together even if a second rolls over in between), and
  writing `$30–$36` sets the clock (`$30` commits). The fix is by Marcel Rockenschuh (patch 0002); it was checked against `pforte.sv`.
  Tested with HORIZONT 2.4 in the emulator: without the fix the menu bar still showed the start time (23:57) after more than two minutes; with the fix it matches
  the host clock, also across midnight (00:00, "Mon 02 Nov").
- **Fix (Linux): the window became much smaller after leaving full screen.** When leaving full screen (Cmd+F), `main.c` only called `SDL_SetWindowFullscreen(win, 0)`
  and relied on the window manager to restore the old size; some do not (HiDPI, Wayland). The emulator now remembers the window size and position before going
  full screen and enforces them for two seconds after leaving it, if the window manager returns something else (message on stderr, in German:
  `Fenster: nach dem Vollbild WxH statt WxH, setze zurueck`). This also covers a window enlarged by the user and starting with `--fullscreen` (the start size is used;
  `--scale` is respected). `MERIDIAN_FENSTER_DEBUG=1` prints size and flags while doing so.
  Tested on Linux X11 (Xvfb with Openbox): a normal window manager is unaffected; a simulated "misbehaving" window manager (the window is resized to 400×300 0.65 s after
  leaving full screen) is put back to 960×720 at its old position; 1200×800 and `--scale 3` are restored as well.
  **The original problem could not be reproduced** (Openbox restores the size by itself); the change is therefore a safeguard against the described behaviour and has
  not been tested on a real GNOME, KDE or Wayland desktop.
- Not included: the mouse fix (patch 0003).

## 0.8.2
Makefile only.
- **macOS:** if the C++ compiler cannot find `<cmath>` (incomplete command line tools, or tools broken by an update), `make` now passes the SDK to the compiler explicitly for
  `sidton` (`-isysroot`, `-I…/usr/include/c++/v1`): first the one from `xcrun --show-sdk-path`, otherwise `/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk`, and only if
  `cmath` really exists there. The fix was contributed by the maintainer and tried on a Mac; it now appears in the command only on macOS instead of being hard-coded for every
  system, and `-O2` is back (without it `sidton` runs a good two and a half times slower here).
- **Linux, error 13:** if the same folder was built on the Mac and on Linux (shared folder, copy), `make` considered the Mac programs up to date and did not rebuild them.
  On Linux this produced, for example, `[Errno 13] Permission denied: 'mpg123'` (file without execute permission) or `[Errno 8] Exec format error`. New: the file `.plattform`
  records system and processor (e.g. `Darwin-arm64`, `Linux-x86_64`); when either changes, `meridian`, `mpg123`, `bildwandler`, `sidkern` and `sidton` are rebuilt.
  On the same platform nothing changes.
- Tested on Linux x86_64: fresh build without warnings, a second run rebuilds nothing, a simulated switch from `Darwin-arm64` rebuilds all five programs
  (a broken `mpg123` is afterwards a working Linux executable again), the Mac branch in a dry run with and without an SDK. Not run on macOS itself.

## 0.8.1
SID JAM only.
- **SID JAM works.** `make` now also builds `dienst/sidkern` and `dienst/sidton`; the network service finds both next to `sidjam_dienst.py`.
  Sources from the developer of the network service: `tools/sidkern/` (own code, GPL-3.0-or-later) and `tools/sidton/` (`sidton.cpp` GPL-3.0-or-later,
  `resid/` = reSID by Dag Lem, GPL-2.0-or-later, unchanged). Both need only a compiler and libc/libstdc++, no libraries. The package's ARM programs do not run on the Mac,
  hence the local build – as with `bildwandler`.
- Tested here (Linux x86_64, same sources as on the Mac; not yet built on macOS itself):
  - `make` builds both without warnings. `sidkern --cputest` passes Klaus Dormann's 6502 functional test ($3469, 30,646,177 instructions, 96,241,367 cycles).
  - With a self-made SID file (440 Hz, gate toggled every 64 frames): header, `--text`, `--ab` (register image), error messages
    ("kein PSID/RSID", "Datei zu kurz", "Lied 2 gibt es nicht (1-1)") and the stream as read by the network service's `SJELeser` and `orgelwandler`.
  - `sidkern | sidton`: peak frequency 440.0 Hz.
  - **SID JAM inside HORIZONT 2.4's window** (mouse and keyboard simulated, HVSC = a zip with the test file, the emulator's sound recorded): the app opens,
    search and playback work, display "6581 PAL reSID", voice 1 shows A-4, the progress bar runs. Sound in the ORIGINAL (reSID) mode: 440 Hz with the harmonics of a
    square wave (1320, 2200, 3080 Hz …); in the ORGEL mode: 440 Hz.
  - Not tested: real HVSC tunes (only one test file), the TOGETHER sound (needs the JAM server), Auto-DJ, multi-SID.
- According to the developer, compared against libsidplayfp (31 tunes exact or nearly so, 2000 HVSC tunes 96 % exact); this was not re-checked here.
- The supplied sources contain two guards for Windows (MSYS2) so that `make` does not fail there: `SIGPIPE` in `sidkern.c` and `M_PI` in `sidton.cpp` are used only if the
  compiler knows them. Not built on MSYS2 itself; the network service does not run on Windows yet anyway.
- Note: the emulator is paced at 50.3 or 59.95 frames per second. If it is made to run faster than real time (test programs), SID JAM considers the network service dead,
  because the service's heartbeat runs in real time.

## 0.8.0
- Core 20261030: `kern.rom` and `werkstatt.rom` are new (fix for the remote start under HORIZONT). The ROMs are converted from `rom/*.hex` of the core repository.
- Network service 2.0 (31 Oct 2026) for HORIZONT 2.4: **printing** from NOTES, PAINT and KONTOR (WRITE, CALC, FILE, DRAW) as an A4 PDF on drive 3 or
  by IPP to a network printer (`druck_dienst.py`); pictures (JPG/PNG/GIF) for PICTURES and PAINT (`bild_dienst.py`); RADIO 1.2 with recording and 12 bit
  (`radio_dienst.py` 1.9). All Python files are unchanged from the package.
- New: `make` builds `dienst/bildwandler` from `tools/bildwandler/` (source and stb_image from the network service package) for the Mac/Linux; the package's ARM programs do not run here.
- New: the examples `C64GAMES.FIL` and `MERIDIAN.DRW` (FILE, DRAW) are put into `sdcard/files` at startup if they are missing.
- New: Cmd+D shows the folder of drive 3 in the file manager.
- Tested with `HORIZONT.MOD` 2.4 in the emulator: NOTES "Print to PDF …" (the text is correct) and "Print" to a test printer by IPP, PAINT "Print to PDF …" (the picture); RADIO opens without errors.
- Unchanged: `sidkern` existed only as an ARM program, SID JAM still did not run.

## 0.7.3
- The HVSC index for SID JAM (`hvsc_index.tsv.gz`, HVSC #85) is now included and is placed into `sdcard/HVSC/` at startup if there is none there (also in an already
  existing `sdcard`); an index of your own is never overwritten.
- The operating system's mouse pointer is hidden over the emulator window (Cmd+C toggles it, `--show-cursor` keeps it visible). It stays visible in the title bar and outside the window.
- Unchanged: `sidkern`/`sidton` (SID JAM still does not run, waiting for feedback from the developer).

## 0.7.2
- New: built-in MP3 decoder for RADIO (`tools/mpg123mini.c` + minimp3, CC0). `make` builds it as `dienst/bin/mpg123`, and the service puts that folder first in its search path;
  ffmpeg is no longer needed (if present, it is still preferred). Converts to 22,050 Hz stereo.
- Tested: MP3 at 22.05/32/44.1/48 kHz and mono (frequency and channel separation agree with ffmpeg), slow stream, ID3 header, junk before the stream, joining in the
  middle of the stream, truncated end, empty and random input. End to end: radio request to the service → state "playing", ring buffer in the window with 440.1 Hz left and 659.5 Hz right.

## 0.7.1
- Fix: weather, news (and radio) failed on the Mac with `CERTIFICATE_VERIFY_FAILED … unable to get local issuer certificate`, because Python there has no certificates without extra work.
  The emulator now passes the system's certificate bundle to the service (`SSL_CERT_FILE`, fallback `certifi`) and warns at startup if Python has no certificates at all.

## 0.7.0
- New: the complete network service 1.7 (weather, news, radio, ATLAS storage, TAKTSTOCK jam, SID JAM parts) is included in `dienst/`.
  Without these modules `draht.py` served only BASIC `NET` and drive 3, and the HORIZONT apps got no answer.
  Tested: WEATHER in the HORIZONT module sends its request to the service (the log shows the attempt to fetch Open-Meteo).
- Browser storage (ATLAS): default port 8816, bound to 127.0.0.1 only, address "localhost" (`--ablage-port`, `MERIDIAN_ABLAGE_LAN`).
- ORGEL also reads samples from bank `$FD` (the DRAHT window), like the hardware's DMA. Measured: 220.8 Hz where 220.5 Hz was expected.
- The service is loaded as a module (`draht.py` stays unchanged; adaptations are in the emulator's start script).
- Not possible: SID JAM (needs `sidkern`/`sidton` for macOS).

## 0.6.2
- The default folder is now called `sdcard` (drive 3 = `sdcard/files`). It used to be `MERIDIAN`: on macOS that is the same name as the program `meridian`
  (case does not matter there), so the folder could not be created. On such a collision there is now a clear message.
- New: self-test of the DRAHT channel (`--dienst-test`, also run automatically at startup): a `HELP` request goes through the window to the service and the
  answer is checked. The result is shown in the window title and in the console.
- Tested: `NET "GET 127.0.0.1:8765/index.txt"` against a local web server (status 200, content arrives, request appears in the server's log).

## 0.6.1
- DRAHT more robust and more visible: the folder `MERIDIAN/files` is created by the emulator itself and lives next to the program (no longer in the current
  working directory). Python 3.8+ is searched for (PATH, `/usr/bin`, Homebrew, `MERIDIAN_PYTHON`) and checked beforehand.
- After startup the emulator checks whether the service is alive and its heartbeat runs; the result is shown in the window title ("D3: …"); on errors the
  reason and a log excerpt appear in the console.

## 0.6.0
- New: DRAHT. Bank `$FD` is a shared file (mmap); the emulator starts the real network service `dienst/draht.py`
  (Python 3) and thereby provides **drive 3** (folder `MERIDIAN/files`) and the **NET commands**. A watchdog ends the service together with the emulator.
- Options `--ordner DIR`, `--no-dienst`; environment variable `MERIDIAN_PYTHON`.
- Bank `$FD` now exists even without expansion memory (`--no-sdram`).
- Tested: `DIR 3` (with 8.3 short names), `LOAD`/`SAVE` on drive 3 (file byte-identical on the computer), `NET "FILES"`, `NET "HELP"`.
  Regression tests unchanged and passed.

## 0.5.0
- New: ORGEL – sound. Four synthesis voices (triangle, saw, pulse, noise, combinable, sync/ring, ADSR like the SID), state-variable filter, eight sample channels
  (chip RAM and expansion memory, loop, crusher, distortion), echo, panning and mixer after `rtl/orgel.sv`; output 48 kHz stereo via SDL (100 ms pre-buffer).
  `SFX`, `SOUND`, `PLAY`, `SONG`, `SAMPLE` are audible.
- `--no-audio`, Cmd+M (sound off/on), test aid `MERIDIAN_AUDIO_STATS=1`.
- Tested by recording: `SOUND 1,440` → 440 Hz; BALLETT music, the ORGEL demo, FILTERTEST and Zusammenspiel produce stereo sound.

## 0.4.3
- ROMs updated to the state of 25 Oct 2026 (core 20261025): `kern`, `dos` and `werkstatt` are new, `basic` and `song` unchanged.
  The developer fixed the module return in the kernel itself (`jsr anzeige_zuruecksetzen` before `_basic`).
- The workaround from 0.4.2 now applies only to the old kernel ROM and is switched off automatically with the new ROM. Tested: new ROM with
  TIEFGANG 1.1 and 1.2, old ROM with and without the workaround.
- Regression test with the new ROMs: boot, DIR, MONTEST (0 errors), KRANTEST, DOSTEST, LADETEST, COUNTER module, Werkstatt.

## 0.4.2
- Fix: after Esc in the TIEFGANG title (and in general whenever a module returns with RTL) the module's picture stayed on screen
  although BASIC was running – input seemed dead. Cause: the kernel ROM does not reset the display on this path. The
  emulator now does it before the BASIC start (`--no-module-fix` turns this off).

## 0.4.1
- Fix: files are loaded as a set (drag and drop of several files and several `--run`): modules/images, then data files,
  finally programs with autostart, all at the same moment. Before, `ebene_a.mer` alone opened the Werkstatt (overwritten key table of the kernel)
  and thereby destroyed the data for KOBOLDE and GRAFIK.
- Test aid: the environment variable `MERIDIAN_SHOT_AFTER=N` saves a screenshot after N frames and quits.

## 0.4.0
- New: BOTE – load `.MER` programs (chip RAM and expansion memory, autostart via NMI like "REMOTE START"), `.MOD` modules
  (write-protected from `$40:0000`, survive reset and cold start, eject with Cmd+E), saved state as `.sav` = drive 0.
- New: `--run` (repeatable), `--module`, `--no-module-start`; drag and drop recognises `.mer`, `.mod` and images.
- Reset is now like on the machine (RAM is kept), Cmd+Shift+R is the cold start.
- TRUHE: the saved-state drive grows when written, unwritten blocks read as zeros.
- Tested: MONTEST (2×256 opcodes, 0 errors), KRANTEST, DOSTEST, LADETEST (including the chip RAM/expansion memory boundary),
  KOBOLDE, GRAFIK, BALLETT, COUNTER module.

## 0.3.0
- New: KOBOLD – 32 sprites with mirroring, double size, "behind layer B", collision flags (`HIT`), outline.
- New: LOTSE (copper) – `GRADIENT` and your own copper lists work.
- New: PINSEL look – shine (`SHINE`), glow (`GLOW`), ink (`INK`: outline, drop shadow).
- PINSEL now draws one line ahead like the hardware; palette changes (CPU or copper) take effect to the pixel within the line.
- The "frame end" interrupt is now reported only when it is enabled (as in the Verilog code).
- Tested with STARFALL, REGENBOG, ABEND, GLOWMINE (title and game) and a sprite/INK/GLOW test of its own.

## 0.2.0
- New: TRUHE – drives 0–2 with block reading/writing (up to 16 blocks per job), INSERTED/CHANGED flags,
  write-protected images. `DIR`, `LOAD`, `SAVE` work against real `.DSK` images.
- New: `--disk0/1/2`, drag and drop of images onto the window, Cmd+1/Cmd+2 to eject, drives in the window title.

## 0.1.1
- Fix: the digit keys 1–9 produced the next digit each (scancode table shifted by one).
- Version number in the window title, with `--version` and in the name of the ZIP.

## 0.1.0
- First version: 65C816, memory map, PINSEL, PFORTE, KRAN, SDL2 front end.
