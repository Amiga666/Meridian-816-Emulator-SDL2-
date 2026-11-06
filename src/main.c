/* MERIDIAN 816 Emulator – SDL2-Frontend. GPL-3.0-or-later */
#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "meridian.h"
#include "version.h"
#include "dienst.h"

typedef struct { uint8_t code; bool ext; } Ps2;
static Ps2 ps2_of(SDL_Scancode sc) {
    static const uint8_t letters[26] = { 0x1C,0x32,0x21,0x23,0x24,0x2B,0x34,0x33,0x43,0x3B,0x42,0x4B,0x3A,
                                         0x31,0x44,0x4D,0x15,0x2D,0x1B,0x2C,0x3C,0x2A,0x1D,0x22,0x35,0x1A };
    static const uint8_t digits[10] = { 0x16,0x1E,0x26,0x25,0x2E,0x36,0x3D,0x3E,0x46,0x45 };
    static const uint8_t fkeys[12] = { 0x05,0x06,0x04,0x0C,0x03,0x0B,0x83,0x0A,0x01,0x09,0x78,0x07 };
    if (sc >= SDL_SCANCODE_A && sc <= SDL_SCANCODE_Z) return (Ps2){ letters[sc - SDL_SCANCODE_A], false };
    if (sc >= SDL_SCANCODE_1 && sc <= SDL_SCANCODE_9) return (Ps2){ digits[sc - SDL_SCANCODE_1], false };
    if (sc == SDL_SCANCODE_0) return (Ps2){ 0x45, false };
    if (sc >= SDL_SCANCODE_F1 && sc <= SDL_SCANCODE_F12) return (Ps2){ fkeys[sc - SDL_SCANCODE_F1], false };
    switch (sc) {
    case SDL_SCANCODE_RETURN: return (Ps2){ 0x5A, false };  case SDL_SCANCODE_ESCAPE: return (Ps2){ 0x76, false };
    case SDL_SCANCODE_BACKSPACE: return (Ps2){ 0x66, false }; case SDL_SCANCODE_TAB: return (Ps2){ 0x0D, false };
    case SDL_SCANCODE_SPACE: return (Ps2){ 0x29, false };   case SDL_SCANCODE_MINUS: return (Ps2){ 0x4E, false };
    case SDL_SCANCODE_EQUALS: return (Ps2){ 0x55, false };  case SDL_SCANCODE_LEFTBRACKET: return (Ps2){ 0x54, false };
    case SDL_SCANCODE_RIGHTBRACKET: return (Ps2){ 0x5B, false };
    case SDL_SCANCODE_BACKSLASH: case SDL_SCANCODE_NONUSHASH: return (Ps2){ 0x5D, false };
    case SDL_SCANCODE_SEMICOLON: return (Ps2){ 0x4C, false }; case SDL_SCANCODE_APOSTROPHE: return (Ps2){ 0x52, false };
    case SDL_SCANCODE_GRAVE: return (Ps2){ 0x0E, false };     case SDL_SCANCODE_COMMA: return (Ps2){ 0x41, false };
    case SDL_SCANCODE_PERIOD: return (Ps2){ 0x49, false };    case SDL_SCANCODE_SLASH: return (Ps2){ 0x4A, false };
    case SDL_SCANCODE_NONUSBACKSLASH: return (Ps2){ 0x61, false };
    case SDL_SCANCODE_CAPSLOCK: return (Ps2){ 0x58, false };
    case SDL_SCANCODE_LSHIFT: return (Ps2){ 0x12, false };    case SDL_SCANCODE_RSHIFT: return (Ps2){ 0x59, false };
    case SDL_SCANCODE_LCTRL: return (Ps2){ 0x14, false };     case SDL_SCANCODE_RCTRL: return (Ps2){ 0x14, true };
    case SDL_SCANCODE_LALT: case SDL_SCANCODE_RALT: return (Ps2){ 0x11, true };  /* beide Alt/Option = AltGr */
    case SDL_SCANCODE_INSERT: return (Ps2){ 0x70, true };     case SDL_SCANCODE_DELETE: return (Ps2){ 0x71, true };
    case SDL_SCANCODE_HOME: return (Ps2){ 0x6C, true };       case SDL_SCANCODE_END: return (Ps2){ 0x69, true };
    case SDL_SCANCODE_PAGEUP: return (Ps2){ 0x7D, true };     case SDL_SCANCODE_PAGEDOWN: return (Ps2){ 0x7A, true };
    case SDL_SCANCODE_UP: return (Ps2){ 0x75, true };         case SDL_SCANCODE_DOWN: return (Ps2){ 0x72, true };
    case SDL_SCANCODE_LEFT: return (Ps2){ 0x6B, true };       case SDL_SCANCODE_RIGHT: return (Ps2){ 0x74, true };
    case SDL_SCANCODE_KP_ENTER: return (Ps2){ 0x5A, true };
    default: return (Ps2){ 0, false };
    }
}

static SDL_Rect view_rect(int ww, int wh) {            /* 4:3 mit Rand */
    int w = ww, h = ww * 3 / 4; if (h > wh) { h = wh; w = wh * 4 / 3; }
    return (SDL_Rect){ (ww - w) / 2, (wh - h) / 2, w, h };
}
static const char *disk_name[3]; static char mod_name[64], dr_status[160];
static void update_title(SDL_Window *w) {
    char t[600]; snprintf(t, sizeof t, "MERIDIAN 816 – Emulator v" MERIDIAN_EMU_VERSION "  |  D1: %s  D2: %s  D3: %s%s%s",
                          disk_name[1] ? disk_name[1] : "-", disk_name[2] ? disk_name[2] : "-", dr_status[0] ? dr_status : "-", mod_name[0] ? "  |  Modul: " : "", mod_name);
    SDL_SetWindowTitle(w, t);
}
/* Cmd+D: den Ordner von Laufwerk 3 im Dateimanager des Rechners zeigen (dort liegen z. B. die PDF-Dateien, die HORIZONT
   beim Drucken auf Laufwerk 3 ablegt). Pfad wird absolut gemacht und fuer die file:-Adresse %-kodiert. */
static void open_folder(const char *path) {
    char abs[2048];
#ifdef _WIN32
    if (!_fullpath(abs, path, sizeof abs)) return;
    for (char *c = abs; *c; c++) if (*c == '\\') *c = '/';
#else
    if (!realpath(path, abs)) { fprintf(stderr, "Laufwerk 3: Ordner %s nicht gefunden\n", path); return; }
#endif
    char url[6200], *o = url; o += sprintf(o, "file://%s", abs[0] == '/' ? "" : "/");
    for (const unsigned char *c = (const unsigned char *)abs; *c && o < url + sizeof url - 4; c++) {
        if ((*c >= 'A' && *c <= 'Z') || (*c >= 'a' && *c <= 'z') || (*c >= '0' && *c <= '9') || strchr("/-_.~:", *c)) *o++ = (char)*c;
        else o += sprintf(o, "%%%02X", *c);
    }
    *o = 0;
#if SDL_VERSION_ATLEAST(2, 0, 14)
    if (SDL_OpenURL(url) != 0) fprintf(stderr, "Ordner oeffnen: %s\n", SDL_GetError());
#else
    fprintf(stderr, "Laufwerk 3: %s\n", abs);                    /* SDL < 2.0.14 kann keine Adressen oeffnen */
#endif
}
static const char *base_name(const char *p) { const char *s = strrchr(p, '/'), *b = strrchr(p, '\\'); if (b > s) s = b; return s ? s + 1 : p; }

/* Dateien als Satz verarbeiten (wie senden.py): Module und Images zuerst, dann .mer-Daten ohne Autostart,
   zuletzt .mer mit Autostart - alles im selben Augenblick, damit der Kern zwischendurch nicht die Werkstatt aufmacht
   (die Datendateien ueberschreiben seine Tastentabelle bei $03:3E00). */
static int mer_autostart(const char *p) {
    FILE *f = fopen(p, "rb"); if (!f) return -1; uint8_t h[16]; size_t n = fread(h, 1, 16, f); fclose(f);
    return (n == 16 && !memcmp(h, "MER", 3) && h[3] == 1) ? (h[7] & 1) : -1;
}
static void ext_of(const char *f, char ext[8]) { const char *x = strrchr(f, '.'); ext[0] = 0; if (x) for (int k = 0; k < 7 && x[k + 1]; k++) { ext[k] = x[k + 1] | 0x20; ext[k + 1] = 0; } }
static void load_set(Machine *m, SDL_Window *win, char **files, int n, bool shift) {
    char ext[8];
    for (int i = 0; i < n; i++) {                                           /* 1. Module und Images */
        ext_of(files[i], ext);
        if (!strcmp(ext, "mer")) continue;
        if (!strcmp(ext, "mod")) {
            if (m_insert_module(m, files[i])) { snprintf(mod_name, sizeof mod_name, "%s", m->module_name); update_title(win); }
            else fprintf(stderr, "Modul '%s' nicht lesbar\n", files[i]);
        } else {
            int d = shift ? 2 : 1;
            if (m_mount(m, d, files[i])) { static char names[3][256]; snprintf(names[d], 256, "%s", base_name(files[i])); disk_name[d] = names[d]; update_title(win); }
            else fprintf(stderr, "Image '%s' nicht lesbar\n", files[i]);
        }
    }
    for (int pass = 0; pass < 2; pass++)                                    /* 2. Daten, 3. Programme mit Autostart */
        for (int i = 0; i < n; i++) {
            ext_of(files[i], ext); if (strcmp(ext, "mer")) continue;
            int a = mer_autostart(files[i]);
            if (a < 0) { if (pass == 0) fprintf(stderr, "'%s' ist keine gueltige MER-Datei\n", files[i]); continue; }
            if (a == pass && !m_load_program(m, files[i])) fprintf(stderr, "'%s' konnte nicht geladen werden\n", files[i]);
        }
}
/* ---- Host-Hotkeys (Beenden, Vollbild, Reset ...). Sie duerfen weder vom Betriebssystem abgefangen werden noch Tasten brauchen, die der MERIDIAN
   selbst benutzt (er bekommt Buchstaben, Ziffern, F1-F12, Strg, Umschalt und Alt/Option als AltGr). Standard je System:
     macOS    Cmd + Buchstabe
     Linux    Win (Super) + Buchstabe
     Windows  Strg + Umschalt + Buchstabe   (Win+Buchstabe ist dort fast durchgehend vom System belegt: Win+D, E, R, S, M, P, Q, F, C, 1, 2 ...)
   Aenderbar mit --hotkey oder MERIDIAN_HOTKEY:
     gui | ctrl+shift | ctrl | alt | shift+alt ...   Modifier gedrueckt halten, dann die Taste (Q, F, R ...)
     f12 | scrolllock | pause | printscreen | menu   Haltetaste: gedrueckt halten, dann die Taste; sie geht nicht an den MERIDIAN
   Die GUI-Taste (Cmd/Win/Super) gilt zusaetzlich immer, wo das System sie durchlaesst. */
typedef struct { int mods; SDL_Scancode lead; } HostKey;
#ifdef _WIN32
#define HOTKEY_STANDARD "ctrl+shift"
#else
#define HOTKEY_STANDARD "gui"                                  /* macOS: Cmd, Linux: Win/Super */
#endif
static bool hk_parse(const char *spec, HostKey *h) {
    char b[96]; snprintf(b, sizeof b, "%s", spec); h->mods = 0; h->lead = SDL_SCANCODE_UNKNOWN;
    for (char *t = strtok(b, "+- "); t; t = strtok(NULL, "+- ")) {
        for (char *c = t; *c; c++) *c = (char)tolower((unsigned char)*c);
        if (!strcmp(t, "gui") || !strcmp(t, "cmd") || !strcmp(t, "win") || !strcmp(t, "super") || !strcmp(t, "meta")) h->mods |= KMOD_GUI;
        else if (!strcmp(t, "ctrl") || !strcmp(t, "strg") || !strcmp(t, "control")) h->mods |= KMOD_CTRL;
        else if (!strcmp(t, "shift") || !strcmp(t, "umschalt")) h->mods |= KMOD_SHIFT;
        else if (!strcmp(t, "alt") || !strcmp(t, "option")) h->mods |= KMOD_ALT;
        else if (!strcmp(t, "f12")) h->lead = SDL_SCANCODE_F12;
        else if (!strcmp(t, "scrolllock") || !strcmp(t, "rollen")) h->lead = SDL_SCANCODE_SCROLLLOCK;
        else if (!strcmp(t, "pause")) h->lead = SDL_SCANCODE_PAUSE;
        else if (!strcmp(t, "printscreen") || !strcmp(t, "prtsc") || !strcmp(t, "druck")) h->lead = SDL_SCANCODE_PRINTSCREEN;
        else if (!strcmp(t, "menu") || !strcmp(t, "application")) h->lead = SDL_SCANCODE_APPLICATION;
        else return false;
    }
    return (h->mods != 0) != (h->lead != SDL_SCANCODE_UNKNOWN);        /* entweder Modifier oder Haltetaste */
}
/* sind alle gewuenschten Modifier-Gruppen (links oder rechts) gedrueckt? */
static bool hk_mods_down(int want, int have) {
    static const int grp[4] = { KMOD_GUI, KMOD_CTRL, KMOD_SHIFT, KMOD_ALT };
    for (int i = 0; i < 4; i++) if ((want & grp[i]) && !(have & grp[i])) return false;
    return want != 0;
}
static bool hk_is_action(SDL_Scancode sc) {
    switch (sc) { case SDL_SCANCODE_Q: case SDL_SCANCODE_C: case SDL_SCANCODE_M: case SDL_SCANCODE_R: case SDL_SCANCODE_K: case SDL_SCANCODE_E:
    case SDL_SCANCODE_1: case SDL_SCANCODE_2: case SDL_SCANCODE_D: case SDL_SCANCODE_P: case SDL_SCANCODE_F: case SDL_SCANCODE_S: return true;
    default: return false; }
}
static void hk_describe(const HostKey *h, char *out, size_t n) {
    out[0] = 0;
    if (h->lead) { snprintf(out, n, "%s", SDL_GetScancodeName(h->lead)); return; }
#ifdef __APPLE__
    if (h->mods & KMOD_GUI) strncat(out, "Cmd+", n - strlen(out) - 1);
#else
    if (h->mods & KMOD_GUI) strncat(out, "Win+", n - strlen(out) - 1);
#endif
    if (h->mods & KMOD_CTRL) strncat(out, "Strg+", n - strlen(out) - 1);
    if (h->mods & KMOD_SHIFT) strncat(out, "Umschalt+", n - strlen(out) - 1);
    if (h->mods & KMOD_ALT) strncat(out, "Alt+", n - strlen(out) - 1);
    size_t l = strlen(out); if (l) out[l - 1] = 0;                  /* letztes '+' weg */
}

static void usage(const char *p) {
    fprintf(stderr, "MERIDIAN 816 Emulator v" MERIDIAN_EMU_VERSION "\nAufruf: %s [--roms DIR] [--pal] [--no-sdram] [--module DATEI] [--scale N] [--run DATEN.MER ... --run PROG.MER] [--module M.MOD] [--no-module-start] [--no-module-fix] [--no-audio] [--ordner DIR (Standard: sdcard neben dem Programm)] [--no-dienst] [--show-cursor] [--hotkey MODIFIER] [--ablage-port N (0 = aus, Standard 8816)] [--dienst-test] [--disk1 IMG] [--disk2 IMG] [--disk0 IMG] [--fullscreen] [--version]\n"
        "Hotkeys mit der Host-Taste (macOS: Cmd, Linux: Win/Super, Windows: Strg+Umschalt; aenderbar mit --hotkey gui|ctrl+shift|ctrl|alt|f12|scrolllock|pause|printscreen|menu):\n"
        "  Cmd+R Reset   Cmd+P PAL/NTSC   Cmd+F Vollbild   Cmd+S Screenshot   Cmd+Q Ende\n  Cmd+C Mauszeiger zeigen/verbergen   Cmd+M Ton aus/an   Laufwerk 3 = Ordner DIR/files (Cmd+D zeigt ihn: dort liegen auch gedruckte PDFs)   Cmd+K Kaltstart (mit Cmd/Win auch Cmd+Shift+R)   Cmd+E Modul auswerfen   Cmd+1 / Cmd+2 Laufwerk 1 / 2 auswerfen.   Datei auf das Fenster ziehen: .mer = Programm laden, .mod = Modul stecken, sonst Laufwerk 1 (Shift: 2)\n", p);
}
int main(int argc, char **argv) {
    const char *roms = NULL, *module = NULL, *ordner = NULL, *runfile[8] = { NULL },  *disk[3] = { NULL, NULL, NULL }; int nrun = 0, nrun_done = 0; int ablage_port = 8816; bool showcursor = false; bool dtest = false, nodienst = false, noaudio = false, nofix = false, nomod = false, pal = false, sdram = true, full = false; int scale = 2;
    const char *hotkey_s = getenv("MERIDIAN_HOTKEY");
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--roms") && i + 1 < argc) roms = argv[++i];
        else if (!strcmp(argv[i], "--module") && i + 1 < argc) module = argv[++i];
        else if (!strcmp(argv[i], "--scale") && i + 1 < argc) scale = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--run") && i + 1 < argc) { if (nrun < 8) runfile[nrun++] = argv[++i]; else ++i; }
        else if (!strcmp(argv[i], "--no-module-start")) nomod = true;
        else if (!strcmp(argv[i], "--no-module-fix")) nofix = true;
        else if (!strcmp(argv[i], "--no-audio")) noaudio = true;
        else if (!strcmp(argv[i], "--no-dienst")) nodienst = true;
        else if (!strcmp(argv[i], "--show-cursor")) showcursor = true;
        else if (!strcmp(argv[i], "--hotkey") && i + 1 < argc) hotkey_s = argv[++i];
        else if (!strcmp(argv[i], "--ablage-port") && i + 1 < argc) ablage_port = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--dienst-test")) dtest = true;
        else if (!strcmp(argv[i], "--ordner") && i + 1 < argc) ordner = argv[++i];
        else if (!strcmp(argv[i], "--disk0") && i + 1 < argc) disk[0] = argv[++i];
        else if (!strcmp(argv[i], "--disk1") && i + 1 < argc) disk[1] = argv[++i];
        else if (!strcmp(argv[i], "--disk2") && i + 1 < argc) disk[2] = argv[++i];
        else if (!strcmp(argv[i], "--pal")) pal = true;
        else if (!strcmp(argv[i], "--no-sdram")) sdram = false;
        else if (!strcmp(argv[i], "--fullscreen")) full = true;
        else if (!strcmp(argv[i], "--version")) { printf("meridian-emu %s\n", MERIDIAN_EMU_VERSION); return 0; }
        else { usage(argv[0]); return 1; }
    }
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_AUDIO) != 0) { fprintf(stderr, "SDL: %s\n", SDL_GetError()); return 1; }
    char path[1024]; Machine *m = NULL;
    const char *base = SDL_GetBasePath();
    if (roms) m = m_new(roms, sdram);
    if (!m) m = m_new("roms", sdram);
    if (!m && base) { snprintf(path, sizeof path, "%sroms", base); m = m_new(path, sdram); }
    if (!m && base) { snprintf(path, sizeof path, "%s../roms", base); m = m_new(path, sdram); }
    if (!m) { fprintf(stderr, "ROM-Dateien (kern.rom, basic.rom, dos.rom, song.rom, werkstatt.rom) nicht gefunden. --roms DIR benutzen.\n"); return 1; }
    for (int d = 0; d < 3; d++) if (disk[d]) {
        if (m_mount(m, d, disk[d])) { static char nm[3][256]; snprintf(nm[d], 256, "%s", base_name(disk[d])); disk_name[d] = nm[d]; } else fprintf(stderr, "Image '%s' nicht lesbar\n", disk[d]); }
    int dr_state = 0;                                         /* 0 aus, 1 gestartet (wird geprueft), 2 laeuft, 3 Fehler */
    char dr_path[1200] = "";
    if (!nodienst) {                                          /* DRAHT: Netzdienst (Python) fuer Laufwerk 3 und NET */
        char sp[1100] = "", err[600], py[300], pyerr[400], c1[1100], c2[1100], def[1100];
        const char *cand[3] = { NULL, NULL, "dienst/draht.py" };
        if (base) { snprintf(c1, sizeof c1, "%sdienst/draht.py", base); snprintf(c2, sizeof c2, "%s../dienst/draht.py", base); cand[0] = c1; cand[1] = c2; }
        for (int k = 0; k < 3 && !sp[0]; k++) if (cand[k]) { FILE *f = fopen(cand[k], "r"); if (f) { fclose(f); snprintf(sp, sizeof sp, "%s", cand[k]); } }
        if (sp[0]) { char dp[1200]; snprintf(dp, sizeof dp, "%.*s/bin/mpg123", (int)(strrchr(sp, '/') ? strrchr(sp, '/') - sp : 0), sp);
            FILE *f = fopen(dp, "r");
#ifdef _WIN32
            if (!f) { strncat(dp, ".exe", sizeof dp - strlen(dp) - 1); f = fopen(dp, "r"); }
#endif
            if (f) { fclose(f); fprintf(stderr, "DRAHT: Radio-Dekoder: eingebauter MP3-Dekoder (%s)\n", dp); }
            else fprintf(stderr, "DRAHT: Radio: kein eingebauter MP3-Dekoder (mit make bauen) - sonst wird ffmpeg oder mpg123 aus dem PATH gebraucht\n"); }
        if (!ordner) { snprintf(def, sizeof def, "%ssdcard", base ? base : "./"); ordner = def; }   /* neben dem Programm */
        if (!sp[0]) { fprintf(stderr, "DRAHT: dienst/draht.py nicht gefunden (gesucht neben dem Programm und im aktuellen Ordner) - Laufwerk 3 und NET sind aus\n"); snprintf(dr_status, sizeof dr_status, "AUS (kein draht.py)"); dr_state = 3; }
        else if (dienst_python(py, sizeof py, pyerr, sizeof pyerr)) {
            fprintf(stderr, "DRAHT: kein brauchbares Python 3 gefunden:%s\n  Python 3.8 oder neuer installieren (macOS: xcode-select --install oder brew install python; Windows: python.org oder MSYS2 pacman -S mingw-w64-x86_64-python) oder MERIDIAN_PYTHON=/pfad/python3 setzen.\n", pyerr);
            snprintf(dr_status, sizeof dr_status, "AUS (kein Python 3)"); dr_state = 3;
        } else if ((pyerr[0] ? (fprintf(stderr, "DRAHT: Hinweis: %s\n", pyerr), 0) : 0), dienst_start(m, sp, ordner, py, ablage_port, err, sizeof err)) { fprintf(stderr, "%s\n", err); snprintf(dr_status, sizeof dr_status, "AUS (Startfehler)"); dr_state = 3; }
        else {
            snprintf(dr_path, sizeof dr_path, "%s/files", ordner); dr_state = 1;
            {   char ix[1200]; snprintf(ix, sizeof ix, "%.*s/hvsc_index.tsv.gz", (int)(strrchr(sp, '/') ? strrchr(sp, '/') - sp : 0), sp);
                int r = dienst_index_kopieren(ix, ordner);           /* HVSC-Index fuer SID JAM neben die (selbst beizulegende) hvsc.zip */
                if (r == 0) fprintf(stderr, "DRAHT: HVSC-Index angelegt: %s/HVSC/hvsc_index.tsv.gz (fuer SID JAM allein hvsc.zip dazulegen)\n", ordner); }
            {   char bd[1200]; snprintf(bd, sizeof bd, "%.*s/beispiele", (int)(strrchr(sp, '/') ? strrchr(sp, '/') - sp : 0), sp);
                int k = dienst_beispiele_kopieren(bd, ordner);        /* Beispiele fuer FILE und DRAW auf Laufwerk 3 */
                if (k > 0) fprintf(stderr, "DRAHT: %d Beispieldatei(en) fuer KONTOR auf Laufwerk 3 angelegt\n", k); }
            fprintf(stderr, "DRAHT: Laufwerk 3 = %s  (Python: %s, Protokoll: %s)\n", dr_path, py, dienst_log());
            fprintf(stderr, "DRAHT: HTTPS-Zertifikate: %s\n", dienst_zertifikate()[0] ? dienst_zertifikate() : "(keine gefunden - Python-eigene Ablage)");
            if (ablage_port) fprintf(stderr, "DRAHT: Browser-Ablage (ATLAS) nur auf diesem Rechner: http://localhost:%d/\n", ablage_port);
            snprintf(dr_status, sizeof dr_status, "%s", "startet ...");
        }
    }
    if (dtest) {
        char rl[500], msg[1500];
        if (dr_state != 1) { fprintf(stderr, "Selbsttest: Dienst nicht gestartet (%s)\n", dr_status); return 2; }
        int rc = dienst_rundlauf(m, rl, sizeof rl);
        if (rc && !dienst_lebt(msg, sizeof msg)) fprintf(stderr, "Selbsttest FEHLER: %s\n%s\n", rl, msg); else fprintf(stderr, "Selbsttest %s: %s\n", rc ? "FEHLER" : "OK", rl);
        dienst_stop(m); return rc ? 1 : 0;
    }
    m_set_pal(m, pal); m->module_off = nomod; m->module_fix = !nofix;
    if (module) { if (m_insert_module(m, module)) snprintf(mod_name, sizeof mod_name, "%s", m->module_name); else fprintf(stderr, "Modul '%s' nicht lesbar\n", module); }
    int frames = 0;

    SDL_Window *win = SDL_CreateWindow("MERIDIAN 816 – Emulator v" MERIDIAN_EMU_VERSION, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                       480 * scale, 360 * scale, SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    if (!win) { fprintf(stderr, "Fenster: %s\n", SDL_GetError()); return 1; }
    /* Fenstergroesse vor dem Vollbild: manche Fenstermanager (Linux, HiDPI/Wayland) geben beim Verlassen des Vollbilds eine
       viel kleinere Groesse zurueck. Deshalb merken wir sie uns und setzen sie danach selbst wieder durch. */
    int win_w = 480 * scale, win_h = 360 * scale, win_x = 0, win_y = 0, win_back = 0; bool win_msg = false;
    SDL_GetWindowSize(win, &win_w, &win_h); SDL_GetWindowPosition(win, &win_x, &win_y);
    /* Tastatur: gebraucht werden nur Scancodes (Tastenposition), keine Texteingabe. SDL2 hat die Texteingabe aber von sich aus an, und unter
       Linux fragt es damit bei jeder Taste die Eingabemethode (IBus, Fcitx ...). Die nimmt Tasten, die ein Nicht-ASCII-Zeichen ergeben
       (beim deutschen Layout ae, oe, ue, ss mit Punkten bzw. Haken), fuer sich - SDL liefert dann gar kein KEYDOWN, und der MERIDIAN sieht keinen
       Tastendruck. Mit US-Layout (dieselben Tasten ergeben ASCII) merkt man nichts. Darum: Texteingabe aus, und bei jedem Fokuswechsel noch
       einmal, falls Fenstermanager oder Eingabemethode sie wieder einschalten. Unter macOS gibt es das Problem nicht (und dort ist der Code
       erprobt), deshalb gilt es nur auf den anderen Systemen. MERIDIAN_TASTEN_DEBUG=1 zeigt jedes Tastenereignis auf stderr,
       MERIDIAN_TEXTEINGABE=1 laesst die Texteingabe an (zum Vergleichen). */
#ifndef __APPLE__
    const bool text_aus = getenv("MERIDIAN_TEXTEINGABE") == NULL;     /* MERIDIAN_TEXTEINGABE=1: SDL-Texteingabe wie frueher an (nur zum Vergleichen) */
#else
    const bool text_aus = false;
#endif
#define TEXTEINGABE_AUS() do { if (text_aus) { SDL_StopTextInput(); SDL_EventState(SDL_TEXTINPUT, SDL_DISABLE); SDL_EventState(SDL_TEXTEDITING, SDL_DISABLE); } } while (0)
    TEXTEINGABE_AUS();
    const bool key_debug = getenv("MERIDIAN_TASTEN_DEBUG") != NULL;
    int drchk = 0; int astat = 0; SDL_AudioDeviceID adev = 0; bool muted = false, aplay = false;
    if (!noaudio) {
        SDL_AudioSpec want, have; SDL_zero(want); want.freq = 48000; want.format = AUDIO_S16SYS; want.channels = 2; want.samples = 1024;
        adev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
        if (adev && have.freq == 48000 && have.channels == 2 && have.format == AUDIO_S16SYS) m->audio_on = true;
        else { if (adev) SDL_CloseAudioDevice(adev); adev = 0; fprintf(stderr, "Kein passendes Audiogeraet: %s (weiter ohne Ton)\n", SDL_GetError()); }
    }
    SDL_ShowCursor(showcursor ? SDL_ENABLE : SDL_DISABLE);       /* der MERIDIAN zeichnet seinen Zeiger selbst; nur ueber dem Fenster verborgen */
    update_title(win);
    if (full) SDL_SetWindowFullscreen(win, SDL_WINDOW_FULLSCREEN_DESKTOP);
    SDL_Renderer *ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED);
    if (!ren) ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
    SDL_Texture *tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, SCR_W, SCR_H);

    char *dropped[32]; int ndrop = 0; bool dropping = false;
    long shot_after = getenv("MERIDIAN_SHOT_AFTER") ? atol(getenv("MERIDIAN_SHOT_AFTER")) : 0, total = 0;   /* Testhilfe: Screenshot nach N Bildern, dann Ende */
    HostKey hk; bool lead_down = false;
    if (!hk_parse(hotkey_s && *hotkey_s ? hotkey_s : HOTKEY_STANDARD, &hk)) {
        fprintf(stderr, "Hotkey '%s' unbekannt (erlaubt: gui, ctrl, shift, alt, ctrl+shift, ... oder f12, scrolllock, pause, printscreen, menu) - nehme %s\n", hotkey_s, HOTKEY_STANDARD);
        hk_parse(HOTKEY_STANDARD, &hk);
    }
    { char hn[64]; hk_describe(&hk, hn, sizeof hn);
      fprintf(stderr, "Hotkeys: %s + Q Ende, F Vollbild, R Reset, K Kaltstart, S Screenshot, M Ton, P PAL/NTSC, C Mauszeiger, D Ordner, E Modul, 1/2 Disk aus\n", hn); }
    bool run = true; uint64_t freq = SDL_GetPerformanceFrequency(), next = SDL_GetPerformanceCounter();
    while (run) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) run = false;
            else if (e.type == SDL_WINDOWEVENT) { if (e.window.event == SDL_WINDOWEVENT_FOCUS_GAINED) TEXTEINGABE_AUS(); }
            else if (e.type == SDL_KEYDOWN || e.type == SDL_KEYUP) {
                bool dn = e.type == SDL_KEYDOWN;
                if (key_debug) fprintf(stderr, "Taste: %s scancode=%d (%s) Zeichen=%s\n", dn ? "runter" : "hoch", (int)e.key.keysym.scancode,
                                       SDL_GetScancodeName(e.key.keysym.scancode), SDL_GetKeyName(e.key.keysym.sym));
                const SDL_Scancode hsc = e.key.keysym.scancode; const int ms = (int)SDL_GetModState();
                if (hk.lead && hsc == hk.lead) { lead_down = dn; continue; }                  /* Haltetaste: geht nicht an den MERIDIAN */
                const bool gui_on = (ms & KMOD_GUI) != 0;                                     /* Cmd/Windows/Super: immer Host (wo das System sie durchlaesst) */
                const bool host_on = hk.lead ? lead_down : hk_mods_down(hk.mods, ms);
                if (gui_on || (host_on && (hk.lead || hk_is_action(hsc)))) {                   /* Host-Hotkeys; andere Tasten mit Strg+Umschalt gehen weiter an den MERIDIAN */
                    const bool shift_is_host = !gui_on && !hk.lead && (hk.mods & KMOD_SHIFT);   /* Umschalt gehoert zur Host-Taste: Kaltstart dann mit K */
                    const bool cold = (ms & KMOD_SHIFT) && !shift_is_host;
                    if (dn && !e.key.repeat) switch (hsc) {
                    case SDL_SCANCODE_Q: run = false; break;
                    case SDL_SCANCODE_C: showcursor = !showcursor; SDL_ShowCursor(showcursor ? SDL_ENABLE : SDL_DISABLE); break;
                    case SDL_SCANCODE_M: muted = !muted; fprintf(stderr, muted ? "Ton aus\n" : "Ton an\n"); break;
                    case SDL_SCANCODE_R: m_reset(m, cold); break;
                    case SDL_SCANCODE_K: m_reset(m, true); break;
                    case SDL_SCANCODE_E: m_eject_module(m); mod_name[0] = 0; update_title(win); break;
                    case SDL_SCANCODE_1: m_eject(m, 1); disk_name[1] = NULL; update_title(win); break;
                    case SDL_SCANCODE_2: m_eject(m, 2); disk_name[2] = NULL; update_title(win); break;
                    case SDL_SCANCODE_D: if (dr_path[0]) open_folder(dr_path); else fprintf(stderr, "Laufwerk 3: kein Ordner (--no-dienst?)\n"); break;
                    case SDL_SCANCODE_P: pal = !pal; m_set_pal(m, pal); break;
                    case SDL_SCANCODE_F:
                        full = !full;
                        if (full) {
                            if (!(SDL_GetWindowFlags(win) & SDL_WINDOW_FULLSCREEN)) { SDL_GetWindowSize(win, &win_w, &win_h); SDL_GetWindowPosition(win, &win_x, &win_y); }
                            SDL_SetWindowFullscreen(win, SDL_WINDOW_FULLSCREEN_DESKTOP); win_back = 0;
                        } else { SDL_SetWindowFullscreen(win, 0); win_back = 120; win_msg = false; }      /* danach 2 s lang die alte Groesse durchsetzen (der Fenstermanager meldet sich oft erst spaeter) */
                        break;
                    case SDL_SCANCODE_S: { SDL_Surface *s = SDL_CreateRGBSurfaceWithFormatFrom(m->fb, SCR_W, SCR_H, 32, SCR_W * 4, SDL_PIXELFORMAT_ARGB8888);
                        if (s) { SDL_SaveBMP(s, "meridian-screenshot.bmp"); SDL_FreeSurface(s); fprintf(stderr, "meridian-screenshot.bmp\n"); } } break;
                    default: break;
                    }
                    continue;
                }
                Ps2 k = ps2_of(e.key.keysym.scancode);
                if (k.code && (dn || !e.key.repeat)) m_key(m, k.ext, k.code, !dn);
            } else if (e.type == SDL_MOUSEMOTION || e.type == SDL_MOUSEBUTTONDOWN || e.type == SDL_MOUSEBUTTONUP) {
                int mx, my, ww, wh; uint32_t b = SDL_GetMouseState(&mx, &my); SDL_GetWindowSize(win, &ww, &wh);
                SDL_Rect r = view_rect(ww, wh);
                m_mouse(m, (mx - r.x) * 320 / r.w, (my - r.y) * 240 / r.h,
                        ((b & SDL_BUTTON_LMASK) ? 1 : 0) | ((b & SDL_BUTTON_RMASK) ? 2 : 0) | ((b & SDL_BUTTON_MMASK) ? 4 : 0));
            } else if (e.type == SDL_DROPBEGIN) { ndrop = 0; dropping = true;
            } else if (e.type == SDL_DROPFILE) {
                if (dropping && ndrop < 32) dropped[ndrop++] = strdup(e.drop.file);
                else { char *one = e.drop.file; load_set(m, win, &one, 1, (SDL_GetModState() & KMOD_SHIFT) != 0); }
                SDL_free(e.drop.file);
            } else if (e.type == SDL_DROPCOMPLETE) {
                dropping = false; load_set(m, win, dropped, ndrop, (SDL_GetModState() & KMOD_SHIFT) != 0);
                for (int k = 0; k < ndrop; k++) free(dropped[k]); ndrop = 0;
            } else if (e.type == SDL_MOUSEWHEEL) m->mwheel += e.wheel.y > 0 ? 1 : e.wheel.y < 0 ? -1 : 0;
        }
        if (nrun_done < nrun && ++frames >= 150) { load_set(m, win, (char **)runfile, nrun, false); nrun_done = nrun; }
        m_frame(m);
        if (dr_state == 1) ++drchk;
        if (dr_state == 1 && drchk == 60) {                  /* Lebenszeichen: Herzschlag des Dienstes ($FD:0010) muss sich bewegen */
            char msg[1500];
            if (!dienst_lebt(msg, sizeof msg)) { fprintf(stderr, "DRAHT: %s\nLaufwerk 3 steht nicht zur Verfuegung.\n", msg); snprintf(dr_status, sizeof dr_status, "AUS (Dienst beendet)"); dr_state = 3; }
            else {
                char rl[500]; int rc = dienst_rundlauf(m, rl, sizeof rl);
                if (rc) { fprintf(stderr, "DRAHT: %s\nProtokoll: %s\n", rl, dienst_log()); snprintf(dr_status, sizeof dr_status, "AUS (%s)", rc == -1 ? "Dienst antwortet nicht" : "Fenster tot"); dr_state = 3; }
                else { snprintf(dr_status, sizeof dr_status, "%s", "Ordner files"); dr_state = 2; fprintf(stderr, "DRAHT: bereit. %s\n", rl); }
            }
            update_title(win);
        }
        if (shot_after && ++total == shot_after) { SDL_Surface *s = SDL_CreateRGBSurfaceWithFormatFrom(m->fb, SCR_W, SCR_H, 32, SCR_W * 4, SDL_PIXELFORMAT_ARGB8888); if (s) { SDL_SaveBMP(s, "meridian-screenshot.bmp"); SDL_FreeSurface(s); } run = false; }
        if (adev) {                                           /* Ton: Mischer-Ausgabe in die Warteschlange, Vorpuffer 100 ms, Obergrenze 300 ms */
            static int16_t abuf[4096 * 2]; size_t n;
            while ((n = m_audio_read(m, abuf, 4096)) > 0) {
                if (muted) continue;
                if (SDL_GetQueuedAudioSize(adev) > 48000 * 4 * 3 / 10) { SDL_ClearQueuedAudio(adev); SDL_PauseAudioDevice(adev, 1); aplay = false; }
                SDL_QueueAudio(adev, abuf, (uint32_t)(n * 4));
            }
            if (!aplay && SDL_GetQueuedAudioSize(adev) >= 48000 * 4 / 10) { SDL_PauseAudioDevice(adev, 0); aplay = true; }
            if (getenv("MERIDIAN_AUDIO_STATS") && (++astat % 60) == 0) fprintf(stderr, "Ton-Warteschlange: %u ms\n", SDL_GetQueuedAudioSize(adev) / 192);   /* Testhilfe */
        }
        if (win_back > 0) {                                        /* nach dem Vollbild: Fenster wieder so gross wie vorher */
            win_back--;
            if (getenv("MERIDIAN_FENSTER_DEBUG") && win_back % 6 == 0) { int dw, dh; SDL_GetWindowSize(win, &dw, &dh); fprintf(stderr, "Fenster-Debug: noch %d, Flags 0x%x, Groesse %dx%d, soll %dx%d\n", win_back, SDL_GetWindowFlags(win), dw, dh, win_w, win_h); }
            if (win_back % 6 == 0 && !(SDL_GetWindowFlags(win) & SDL_WINDOW_FULLSCREEN)) {
                int cw, ch; SDL_GetWindowSize(win, &cw, &ch);
                if (cw != win_w || ch != win_h) {
                    if (!win_msg) { fprintf(stderr, "Fenster: nach dem Vollbild %dx%d statt %dx%d, setze zurueck\n", cw, ch, win_w, win_h); win_msg = true; }
                    SDL_SetWindowSize(win, win_w, win_h); SDL_SetWindowPosition(win, win_x, win_y);
                    if (win_back < 30) win_back = 30;                  /* der Fenstermanager antwortet verzoegert: noch ein paar Versuche */
                }
            }
        }
        SDL_UpdateTexture(tex, NULL, m->fb, SCR_W * 4);
        int ww, wh; SDL_GetRendererOutputSize(ren, &ww, &wh); SDL_Rect r = view_rect(ww, wh);
        SDL_SetRenderDrawColor(ren, 0, 0, 0, 255); SDL_RenderClear(ren); SDL_RenderCopy(ren, tex, NULL, &r); SDL_RenderPresent(ren);
        next += (uint64_t)(freq / (pal ? 50.3 : 59.95));
        uint64_t now = SDL_GetPerformanceCounter();
        if (next > now) SDL_Delay((uint32_t)((next - now) * 1000 / freq)); else if (now - next > freq / 4) next = now;
    }
    dienst_stop(m);
    SDL_Quit(); return 0;
}
