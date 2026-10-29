/* mpg123mini - winziger MP3-Dekoder, gerade genug fuer das MERIDIAN-Radio (dienst/radio_dienst.py).
   Ruft sich wie "mpg123 -q -s -r 22050 --stereo -" auf: MP3-Strom von stdin, rohes 16-Bit-Stereo (little endian) auf stdout,
   umgerechnet auf die Rate aus -r. Dekodierung: minimp3 (CC0). GPL-3.0-or-later fuer den Rest. */
#define MINIMP3_IMPLEMENTATION
#include "minimp3.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

static int g_rate = 0;                         /* Zielrate; 0 = Rate des Stroms */
static int g_src = 0; static double g_r;       /* Quellrate, Quellsamples je Ausgabesample */
static double g_next, g_aw, g_al, g_ar;        /* Kastenfilter (Verkleinern) */
static double g_pl, g_pr, g_pos; static int g_have;   /* lineare Interpolation (Vergroessern) */
static int16_t g_out[8192 * 2]; static int g_n;

static void flush_out(void) { if (g_n) { fwrite(g_out, 4, g_n, stdout); fflush(stdout); g_n = 0; } }
static void emit(double l, double r) {
    if (l > 32767) l = 32767; if (l < -32768) l = -32768; if (r > 32767) r = 32767; if (r < -32768) r = -32768;
    g_out[g_n * 2] = (int16_t)(l < 0 ? l - 0.5 : l + 0.5); g_out[g_n * 2 + 1] = (int16_t)(r < 0 ? r - 0.5 : r + 0.5);
    if (++g_n == 8192) flush_out();
}
static void start_rate(int hz) { g_src = hz; int dst = g_rate ? g_rate : hz; g_r = (double)hz / dst; g_next = g_r; g_aw = g_al = g_ar = 0; g_have = 0; g_pos = 0; }
static void put(int hz, double l, double r) {
    if (hz != g_src) { flush_out(); start_rate(hz); }
    if (g_r >= 1.0) {                                            /* Verkleinern: Mittelwert ueber das Quellintervall */
        double s = g_pos, e = g_pos + 1;
        while (s < e - 1e-12) {
            double ie = g_next < e ? g_next : e, w = ie - s;
            g_al += l * w; g_ar += r * w; g_aw += w; s = ie;
            if (s >= g_next - 1e-12) { emit(g_al / g_aw, g_ar / g_aw); g_al = g_ar = g_aw = 0; g_next += g_r; }
        }
        g_pos = e;
    } else {                                                     /* Vergroessern: linear zwischen zwei Quellsamples */
        if (!g_have) { g_pl = l; g_pr = r; g_have = 1; g_pos = 0; return; }
        while (g_pos < 1.0) { emit(g_pl + (l - g_pl) * g_pos, g_pr + (r - g_pr) * g_pos); g_pos += g_r; }
        g_pos -= 1.0; g_pl = l; g_pr = r;
    }
}
int main(int argc, char **argv) {
    for (int i = 1; i < argc; i++) {
        if ((!strcmp(argv[i], "-r") || !strcmp(argv[i], "--rate")) && i + 1 < argc) g_rate = atoi(argv[++i]);
        /* -q, -s, --stereo, -m und "-" brauchen nichts: Ausgabe ist immer Stereo auf stdout */
    }
#ifdef _WIN32
    _setmode(0, _O_BINARY); _setmode(1, _O_BINARY);
#endif
    static mp3dec_t dec; mp3dec_init(&dec);
    static uint8_t buf[64 * 1024]; int len = 0, eof = 0; static mp3d_sample_t pcm[MINIMP3_MAX_SAMPLES_PER_FRAME];
    while (1) {
        while (!eof && len < (int)sizeof buf - 8192) {          /* nachfuellen, nach Moeglichkeit mindestens 2 KB vorhalten */
            ssize_t r = read(0, buf + len, sizeof buf - len);
            if (r <= 0) { eof = 1; break; }
            len += (int)r; if (len >= 2048) break;
        }
        int pos = 0;
        while (len - pos >= (eof ? 4 : 2048)) {
            mp3dec_frame_info_t info; int n = mp3dec_decode_frame(&dec, buf + pos, len - pos, pcm, &info);
            if (info.frame_bytes <= 0) break;                    /* nichts Brauchbares, mehr Daten holen */
            pos += info.frame_bytes;
            for (int k = 0; k < n; k++) {
                double l = pcm[k * info.channels], r = info.channels > 1 ? pcm[k * info.channels + 1] : l;
                put(info.hz, l, r);
            }
        }
        if (pos) { memmove(buf, buf + pos, len - pos); len -= pos; }
        else if (eof) break;
        else if (len >= (int)sizeof buf - 8192) len = 0;         /* Muell ohne Sync: verwerfen */
        flush_out();
        if (eof && len < 4) break;
    }
    flush_out(); return 0;
}
