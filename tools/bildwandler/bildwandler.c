/*
 * bildwandler - Bilder fuer den MERIDIAN 816 (Netzdienst, PICTURES und PAINT)
 *
 *   bildwandler bmp QUELLE ZIEL.BMP --erste E --farben N --breite B --hoehe H
 *       jedes Format von stb_image (JPG, PNG, BMP, GIF, TGA ...) -> 8-Bit-BMP:
 *       in B x H eingepasst (nur verkleinert, Flaechenmittel), Farben mit
 *       4 Bit je Anteil wie die Palette des MERIDIAN, hoechstens N Farben auf
 *       den Plaetzen E .. E+N-1. Wenige Farben bleiben genau, sonst Median-Cut
 *       und Floyd-Steinberg. Gibt "W H" aus.
 *       --fest 1: statt eigener Farben die feste 16er-Palette (WRITE) auf
 *       E .. E+15; bis 64 Farben naechste Farbe, sonst Floyd-Steinberg.
 *
 *   bildwandler ppm QUELLE ZIEL.PPM --breite B --hoehe H
 *       volle Farben (P6) fuer den Druck, in B x H eingepasst (nur verkleinert).
 *
 *   bildwandler png|jpg QUELLE ZIEL [--qualitaet 90]
 *       QUELLE (meist eine BMP aus PAINT) als PNG oder JPG schreiben.
 *
 * Fehler: eine Zeile auf stderr, Rueckgabe 1.
 */
#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#define STBI_FAILURE_USERMSG
#include "stb/stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb/stb_image_write.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fehler(const char *text)
{
    fprintf(stderr, "%s\n", text);
    return 1;
}

/* ------------------------------------------------------------ einlesen */
/* RGB, Alpha schon ueber Schwarz gelegt */
static uint8_t *laden(const char *pfad, int *w, int *h)
{
    int n = 0;
    if (!stbi_info(pfad, w, h, &n))
        return NULL;
    int mit_alpha = (n == 2 || n == 4);
    uint8_t *d = stbi_load(pfad, w, h, &n, mit_alpha ? 4 : 3);
    if (!d)
        return NULL;
    if (!mit_alpha)
        return d;
    size_t px = (size_t)*w * *h;
    for (size_t i = 0; i < px; i++) {
        unsigned a = d[4 * i + 3];
        d[3 * i + 0] = (uint8_t)((d[4 * i + 0] * a + 127) / 255);
        d[3 * i + 1] = (uint8_t)((d[4 * i + 1] * a + 127) / 255);
        d[3 * i + 2] = (uint8_t)((d[4 * i + 2] * a + 127) / 255);
    }
    return d;
}

/* ------------------------------------------------------------ verkleinern */
/* Flaechenmittel: jeder Zielpunkt mittelt sein Rechteck der Quelle */
static uint8_t *verkleinern(const uint8_t *q, int w, int h, int nw, int nh)
{
    uint8_t *z = malloc((size_t)nw * nh * 3);
    uint32_t *summe = calloc((size_t)nw * 3, sizeof(uint32_t));
    int *x0 = malloc(sizeof(int) * (nw + 1));
    if (!z || !summe || !x0)
        return NULL;
    for (int x = 0; x <= nw; x++)
        x0[x] = (int)((int64_t)x * w / nw);
    for (int y = 0; y < nh; y++) {
        int ya = (int)((int64_t)y * h / nh), yb = (int)((int64_t)(y + 1) * h / nh);
        if (yb <= ya)
            yb = ya + 1;
        memset(summe, 0, (size_t)nw * 3 * sizeof(uint32_t));
        for (int sy = ya; sy < yb; sy++) {
            const uint8_t *zeile = q + (size_t)sy * w * 3;
            for (int x = 0; x < nw; x++) {
                int xa = x0[x], xb = x0[x + 1] > xa ? x0[x + 1] : xa + 1;
                uint32_t r = 0, g = 0, b = 0;
                for (int sx = xa; sx < xb; sx++) {
                    r += zeile[3 * sx];
                    g += zeile[3 * sx + 1];
                    b += zeile[3 * sx + 2];
                }
                summe[3 * x] += r;
                summe[3 * x + 1] += g;
                summe[3 * x + 2] += b;
            }
        }
        for (int x = 0; x < nw; x++) {
            int xa = x0[x], xb = x0[x + 1] > xa ? x0[x + 1] : xa + 1;
            uint32_t n = (uint32_t)(xb - xa) * (yb - ya);
            for (int k = 0; k < 3; k++)
                z[((size_t)y * nw + x) * 3 + k] = (uint8_t)((summe[3 * x + k] + n / 2) / n);
        }
    }
    free(summe);
    free(x0);
    return z;
}

/* ------------------------------------------------------------ Farben */
static int v4(int v) { return (v * 15 + 127) / 255; }
static int farbe12(const uint8_t *p) { return (v4(p[0]) << 8) | (v4(p[1]) << 4) | v4(p[2]); }

typedef struct { int von, bis; } Kiste;   /* Bereich in der Liste der Farben */

static uint32_t hist[4096];
static int farben[4096];                  /* 12-Bit-Farben mit Vorkommen */
static int achse_sort;

static int anteil(int f, int a) { return (f >> (8 - 4 * a)) & 15; }

static int vergleich(const void *a, const void *b)
{
    int x = anteil(*(const int *)a, achse_sort), y = anteil(*(const int *)b, achse_sort);
    if (x != y)
        return x - y;
    return *(const int *)a - *(const int *)b;
}

/* laengste Achse einer Kiste und ihre Spanne */
static int spanne(Kiste k, int *achse)
{
    int beste = -1;
    for (int a = 0; a < 3; a++) {
        int lo = 15, hi = 0;
        for (int i = k.von; i < k.bis; i++) {
            int v = anteil(farben[i], a);
            if (v < lo) lo = v;
            if (v > hi) hi = v;
        }
        if (hi - lo > beste) {
            beste = hi - lo;
            *achse = a;
        }
    }
    return beste;
}

/* Median-Cut: liefert die Zahl der Farben in pal (8-Bit-Werte, v4*17) */
static int median_cut(int anzahl, int ziel, uint8_t pal[][3])
{
    Kiste kisten[256];
    int nk = 1;
    kisten[0].von = 0;
    kisten[0].bis = anzahl;
    while (nk < ziel) {
        /* die Kiste mit der groessten Spanne mal Gewicht teilen */
        int wahl = -1, wahl_achse = 0;
        double wahl_wert = 0;
        for (int i = 0; i < nk; i++) {
            if (kisten[i].bis - kisten[i].von < 2)
                continue;
            int achse;
            int s = spanne(kisten[i], &achse);
            if (s <= 0)
                continue;
            double gewicht = 0;
            for (int j = kisten[i].von; j < kisten[i].bis; j++)
                gewicht += hist[farben[j]];
            double wert = s * gewicht;
            if (wert > wahl_wert) {
                wahl_wert = wert;
                wahl = i;
                wahl_achse = achse;
            }
        }
        if (wahl < 0)
            break;
        Kiste k = kisten[wahl];
        achse_sort = wahl_achse;
        qsort(farben + k.von, k.bis - k.von, sizeof(int), vergleich);
        double gesamt = 0, halb = 0;
        for (int j = k.von; j < k.bis; j++)
            gesamt += hist[farben[j]];
        int teil = k.von + 1;
        for (int j = k.von; j < k.bis - 1; j++) {
            halb += hist[farben[j]];
            teil = j + 1;
            if (halb >= gesamt / 2)
                break;
        }
        kisten[wahl].bis = teil;
        kisten[nk].von = teil;
        kisten[nk].bis = k.bis;
        nk++;
    }
    for (int i = 0; i < nk; i++) {
        double s[3] = {0, 0, 0}, n = 0;
        for (int j = kisten[i].von; j < kisten[i].bis; j++) {
            double c = hist[farben[j]];
            for (int a = 0; a < 3; a++)
                s[a] += anteil(farben[j], a) * c;
            n += c;
        }
        for (int a = 0; a < 3; a++)
            pal[i][a] = (uint8_t)((int)(s[a] / n + 0.5) * 17);
    }
    return nk;
}

static int naechste(const uint8_t pal[][3], int n, int r, int g, int b)
{
    int beste = 0, bd = 1 << 30;
    for (int i = 0; i < n; i++) {
        int dr = r - pal[i][0], dg = g - pal[i][1], db = b - pal[i][2];
        int d = 3 * dr * dr + 4 * dg * dg + 2 * db * db;
        if (d < bd) {
            bd = d;
            beste = i;
            if (!d)
                break;
        }
    }
    return beste;
}

/* ------------------------------------------------------------ BMP schreiben */
static void w16(uint8_t *p, uint32_t v) { p[0] = v; p[1] = v >> 8; }
static void w32(uint8_t *p, uint32_t v) { p[0] = v; p[1] = v >> 8; p[2] = v >> 16; p[3] = v >> 24; }

static int bmp_schreiben(const char *ziel, const uint8_t *idx, int w, int h,
                         const uint8_t pal[][3], int erste, int n)
{
    int zeile = (w + 3) & ~3;
    uint32_t daten = 14 + 40 + 1024;
    uint32_t groesse = daten + (uint32_t)zeile * h;
    uint8_t kopf[14 + 40 + 1024];
    memset(kopf, 0, sizeof kopf);
    kopf[0] = 'B';
    kopf[1] = 'M';
    w32(kopf + 2, groesse);
    w32(kopf + 10, daten);
    w32(kopf + 14, 40);
    w32(kopf + 18, w);
    w32(kopf + 22, h);
    w16(kopf + 26, 1);
    w16(kopf + 28, 8);
    w32(kopf + 34, (uint32_t)zeile * h);
    w32(kopf + 38, 2835);
    w32(kopf + 42, 2835);
    w32(kopf + 46, 256);
    for (int i = 0; i < n; i++) {
        uint8_t *p = kopf + 54 + 4 * (erste + i);
        p[0] = pal[i][2];
        p[1] = pal[i][1];
        p[2] = pal[i][0];
    }
    FILE *f = fopen(ziel, "wb");
    if (!f)
        return fehler("CANNOT WRITE FILE");
    fwrite(kopf, 1, sizeof kopf, f);
    uint8_t *puffer = calloc(zeile, 1);
    for (int y = h - 1; y >= 0; y--) {
        memcpy(puffer, idx + (size_t)y * w, w);
        fwrite(puffer, 1, zeile, f);
    }
    free(puffer);
    if (fclose(f) != 0)
        return fehler("CANNOT WRITE FILE");
    return 0;
}

/* ------------------------------------------------------------ bmp */
/* feste 16 Farben (Werte der PICO-8-Palette, 4 Bit je Anteil) fuer --fest 1 */
static const char *const FEST16[16] = {
    "000", "125", "725", "085", "a53", "554", "ccc", "ffe",
    "f05", "fa0", "fe2", "0e3", "2af", "879", "f7a", "fca"};

static int hexwert(char c) { return c <= '9' ? c - '0' : c - 'a' + 10; }

static int als_bmp(const char *quelle, const char *ziel, int erste, int n, int bb, int bh,
                   int fest)
{
    if (fest)
        n = 16;
    if (erste < 0 || n < 1 || erste + n > 256 || bb < 1 || bh < 1)
        return fehler("BAD ARGUMENTS");
    int w, h;
    uint8_t *bild = laden(quelle, &w, &h);
    if (!bild)
        return fehler("CANNOT READ IMAGE");
    int nw = w, nh = h;
    if (w > bb || h > bh) {
        if ((int64_t)w * bh > (int64_t)h * bb) {      /* breiter als der Kasten */
            nw = bb;
            nh = (int)(((int64_t)h * bb + w / 2) / w);
        } else {
            nh = bh;
            nw = (int)(((int64_t)w * bh + h / 2) / h);
        }
        if (nw < 1) nw = 1;
        if (nh < 1) nh = 1;
        uint8_t *klein = verkleinern(bild, w, h, nw, nh);
        stbi_image_free(bild);
        if (!klein)
            return fehler("OUT OF MEMORY");
        bild = klein;
    }
    size_t px = (size_t)nw * nh;
    memset(hist, 0, sizeof hist);
    for (size_t i = 0; i < px; i++)
        hist[farbe12(bild + 3 * i)]++;
    int anzahl = 0;
    for (int f = 0; f < 4096; f++)
        if (hist[f])
            farben[anzahl++] = f;

    uint8_t pal[256][3];
    uint8_t *idx = malloc(px);
    if (!idx)
        return fehler("OUT OF MEMORY");
    int np;
    if (fest) {                                         /* feste Palette */
        for (int i = 0; i < 16; i++)
            for (int a = 0; a < 3; a++)
                pal[i][a] = (uint8_t)(hexwert(FEST16[i][a]) * 17);
        np = 16;
    }
    if (fest && anzahl <= 64) {                         /* Pixelkunst: naechste, ohne Rastern */
        for (size_t i = 0; i < px; i++) {
            const uint8_t *p = bild + 3 * i;
            idx[i] = (uint8_t)(erste + naechste((const uint8_t(*)[3])pal, np, p[0], p[1], p[2]));
        }
    } else if (!fest && anzahl <= n) {                  /* genau, ohne Rastern */
        int platz[4096];
        for (int i = 0; i < anzahl; i++) {
            platz[farben[i]] = i;
            for (int a = 0; a < 3; a++)
                pal[i][a] = (uint8_t)(anteil(farben[i], a) * 17);
        }
        np = anzahl;
        for (size_t i = 0; i < px; i++)
            idx[i] = (uint8_t)(erste + platz[farbe12(bild + 3 * i)]);
    } else {
        if (!fest)
            np = median_cut(anzahl, n, pal);
        /* Floyd-Steinberg, Zeilen abwechselnd hin und her */
        int *fehl = calloc((size_t)(nw + 2) * 2 * 3, sizeof(int));   /* zwei Zeilen, *16 */
        if (!fehl)
            return fehler("OUT OF MEMORY");
        int *jetzt = fehl, *naechst = fehl + (nw + 2) * 3;
        for (int y = 0; y < nh; y++) {
            memset(naechst, 0, (size_t)(nw + 2) * 3 * sizeof(int));
            int rechts = !(y & 1);
            for (int k = 0; k < nw; k++) {
                int x = rechts ? k : nw - 1 - k;
                int d = rechts ? 1 : -1;
                const uint8_t *p = bild + ((size_t)y * nw + x) * 3;
                int v[3];
                for (int a = 0; a < 3; a++) {
                    int t = p[a] + (jetzt[(x + 1) * 3 + a] + 8) / 16;
                    v[a] = t < 0 ? 0 : t > 255 ? 255 : t;
                }
                int i = naechste((const uint8_t(*)[3])pal, np, v[0], v[1], v[2]);
                idx[(size_t)y * nw + x] = (uint8_t)(erste + i);
                for (int a = 0; a < 3; a++) {
                    int e = v[a] - pal[i][a];
                    jetzt[(x + 1 + d) * 3 + a] += e * 7;
                    naechst[(x + 1 - d) * 3 + a] += e * 3;
                    naechst[(x + 1) * 3 + a] += e * 5;
                    naechst[(x + 1 + d) * 3 + a] += e;
                }
            }
            int *t = jetzt;
            jetzt = naechst;
            naechst = t;
        }
        free(fehl);
    }
    stbi_image_free(bild);
    int r = bmp_schreiben(ziel, idx, nw, nh, (const uint8_t(*)[3])pal, erste, np);
    free(idx);
    if (r)
        return r;
    printf("%d %d\n", nw, nh);
    return 0;
}

/* ------------------------------------------------------------ png / jpg */
static int als_datei(const char *art, const char *quelle, const char *ziel, int qualitaet)
{
    int w, h;
    uint8_t *bild = laden(quelle, &w, &h);
    if (!bild)
        return fehler("CANNOT READ IMAGE");
    int ok = !strcmp(art, "png") ? stbi_write_png(ziel, w, h, 3, bild, w * 3)
                                 : stbi_write_jpg(ziel, w, h, 3, bild, qualitaet);
    stbi_image_free(bild);
    if (!ok)
        return fehler("CANNOT WRITE FILE");
    printf("%d %d\n", w, h);
    return 0;
}

/* ------------------------------------------------------------ ppm */
/* volle Farben fuer den Druck: in B x H eingepasst (nur verkleinert), P6 */
static int als_ppm(const char *quelle, const char *ziel, int bb, int bh)
{
    if (bb < 1 || bh < 1)
        return fehler("BAD ARGUMENTS");
    int w, h;
    uint8_t *bild = laden(quelle, &w, &h);
    if (!bild)
        return fehler("CANNOT READ IMAGE");
    int nw = w, nh = h;
    if (w > bb || h > bh) {
        if ((int64_t)w * bh > (int64_t)h * bb) {
            nw = bb;
            nh = (int)(((int64_t)h * bb + w / 2) / w);
        } else {
            nh = bh;
            nw = (int)(((int64_t)w * bh + h / 2) / h);
        }
        if (nw < 1) nw = 1;
        if (nh < 1) nh = 1;
        uint8_t *klein = verkleinern(bild, w, h, nw, nh);
        stbi_image_free(bild);
        if (!klein)
            return fehler("OUT OF MEMORY");
        bild = klein;
    }
    FILE *f = fopen(ziel, "wb");
    if (!f)
        return fehler("CANNOT WRITE FILE");
    fprintf(f, "P6\n%d %d\n255\n", nw, nh);
    fwrite(bild, 3, (size_t)nw * nh, f);
    stbi_image_free(bild);
    if (fclose(f) != 0)
        return fehler("CANNOT WRITE FILE");
    printf("%d %d\n", nw, nh);
    return 0;
}

static void hilfe(void)
{
    fprintf(stderr,
            "bildwandler bmp QUELLE ZIEL.BMP --erste E --farben N --breite B --hoehe H [--fest 1]\n"
            "bildwandler png|jpg QUELLE ZIEL [--qualitaet 90]\n"
            "bildwandler ppm QUELLE ZIEL.PPM --breite B --hoehe H\n");
}

int main(int argc, char **argv)
{
    if (argc < 4) {
        hilfe();
        return 1;
    }
    int erste = 0, n = 256, bb = 320, bh = 240, qualitaet = 90, fest = 0;
    for (int i = 4; i < argc; i += 2) {
        if (i + 1 >= argc) {
            hilfe();
            return 1;
        }
        int v = atoi(argv[i + 1]);
        if (!strcmp(argv[i], "--erste")) erste = v;
        else if (!strcmp(argv[i], "--farben")) n = v;
        else if (!strcmp(argv[i], "--breite")) bb = v;
        else if (!strcmp(argv[i], "--hoehe")) bh = v;
        else if (!strcmp(argv[i], "--fest")) fest = v;
        else if (!strcmp(argv[i], "--qualitaet")) qualitaet = v < 1 ? 1 : v > 100 ? 100 : v;
        else {
            hilfe();
            return 1;
        }
    }
    if (!strcmp(argv[1], "bmp"))
        return als_bmp(argv[2], argv[3], erste, n, bb, bh, fest);
    if (!strcmp(argv[1], "ppm"))
        return als_ppm(argv[2], argv[3], bb, bh);
    if (!strcmp(argv[1], "png") || !strcmp(argv[1], "jpg"))
        return als_datei(argv[1], argv[2], argv[3], qualitaet);
    hilfe();
    return 1;
}
