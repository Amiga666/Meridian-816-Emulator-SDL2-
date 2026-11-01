#include "dienst.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(_WIN32)
int dienst_start(Machine *m, const char *s, const char *o, const char *p, int port, char *err, size_t n) {
    (void)m; (void)s; (void)o; (void)p; (void)port; snprintf(err, n, "DRAHT-Dienst: unter Windows noch nicht eingebaut (Bank $FD bleibt ein einfacher Speicher)"); return -1; }
void dienst_stop(Machine *m) { (void)m; }
const char *dienst_log(void) { return ""; }
int dienst_lebt(char *msg, size_t n) { snprintf(msg, n, "nicht gestartet"); return 0; }
int dienst_rundlauf(Machine *m, char *o, size_t n) { (void)m; snprintf(o, n, "nicht eingebaut"); return -1; }
int dienst_mkdirs(const char *p) { (void)p; return -1; }
const char *dienst_zertifikate(void) { return ""; }
int dienst_index_kopieren(const char *q, const char *o) { (void)q; (void)o; return -1; }
int dienst_beispiele_kopieren(const char *q, const char *o) { (void)q; (void)o; return 0; }
int dienst_python(char *o, size_t n, char *e, size_t en) { (void)o; (void)n; snprintf(e, en, "unter Windows nicht eingebaut"); return -1; }
#else
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
extern char **environ;
static pid_t g_pid; static char g_win[512], g_log[512];
const char *dienst_log(void) { return g_log; }

int dienst_mkdirs(const char *pfad) {
    char b[1200]; snprintf(b, sizeof b, "%s", pfad);
    for (char *p = b + 1; *p; p++) if (*p == '/') { *p = 0; mkdir(b, 0777); *p = '/'; }
    if (mkdir(b, 0777) != 0 && errno != EEXIST) return -1;
    struct stat st; return (stat(b, &st) == 0 && S_ISDIR(st.st_mode)) ? 0 : -1;
}
/* HVSC-Index fuer SID JAM (Allein-Modus) nach <ordner>/HVSC/ legen, wenn dort noch keiner liegt.
   0 = kopiert, 1 = war schon da, -1 = Fehler. Eine vorhandene (eigene) Datei wird nie ueberschrieben. */
int dienst_index_kopieren(const char *quelle, const char *ordner) {
    char dir[1300], dst[1400], tmp[1420];
    snprintf(dir, sizeof dir, "%s/HVSC", ordner); snprintf(dst, sizeof dst, "%s/hvsc_index.tsv.gz", dir); snprintf(tmp, sizeof tmp, "%s.neu", dst);
    if (access(dst, F_OK) == 0) return 1;
    if (dienst_mkdirs(dir) != 0) return -1;
    FILE *in = fopen(quelle, "rb"); if (!in) return -1;
    FILE *out = fopen(tmp, "wb"); if (!out) { fclose(in); return -1; }
    char b[65536]; size_t n; int ok = 1;
    while ((n = fread(b, 1, sizeof b, in)) > 0) if (fwrite(b, 1, n, out) != n) { ok = 0; break; }
    ok = ok && !ferror(in); fclose(in); if (fclose(out) != 0) ok = 0;
    if (!ok || rename(tmp, dst) != 0) { unlink(tmp); return -1; }
    return 0;
}
/* Beispieldateien fuer KONTOR (C64GAMES.FIL fuer FILE, MERIDIAN.DRW fuer DRAW) nach <ordner>/files/ legen, wenn dort
   noch keine gleichnamige liegt (eigene Dateien werden nie ueberschrieben). Rueckgabe: Anzahl der neu angelegten. */
int dienst_beispiele_kopieren(const char *quelldir, const char *ordner) {
    static const char *namen[] = { "C64GAMES.FIL", "MERIDIAN.DRW", NULL };
    char dir[1300]; snprintf(dir, sizeof dir, "%s/files", ordner);
    if (dienst_mkdirs(dir) != 0) return 0;
    int neu = 0;
    for (int i = 0; namen[i]; i++) {
        char src[1400], dst[1400], tmp[1420];
        snprintf(src, sizeof src, "%s/%s", quelldir, namen[i]); snprintf(dst, sizeof dst, "%s/%s", dir, namen[i]); snprintf(tmp, sizeof tmp, "%s.neu", dst);
        if (access(dst, F_OK) == 0) continue;
        FILE *in = fopen(src, "rb"); if (!in) continue;
        FILE *out = fopen(tmp, "wb"); if (!out) { fclose(in); continue; }
        char b[16384]; size_t n; int ok = 1;
        while ((n = fread(b, 1, sizeof b, in)) > 0) if (fwrite(b, 1, n, out) != n) { ok = 0; break; }
        ok = ok && !ferror(in); fclose(in); if (fclose(out) != 0) ok = 0;
        if (ok && rename(tmp, dst) == 0) neu++; else unlink(tmp);
    }
    return neu;
}
/* Zertifikatspaket fuer HTTPS: Pythons von python.org/Homebrew finden den Schluesselbund nicht und scheitern mit
   CERTIFICATE_VERIFY_FAILED. Das Paket des Systems wird per SSL_CERT_FILE mitgegeben (Umgebung hat Vorrang). */
static const char *g_ca = "";
static void zertifikate_suchen(void) {
    const char *env = getenv("SSL_CERT_FILE");
    if (env && *env) { g_ca = env; return; }
    static const char *cand[] = { "/etc/ssl/cert.pem", "/private/etc/ssl/cert.pem", "/opt/homebrew/etc/ca-certificates/cert.pem",
        "/usr/local/etc/ca-certificates/cert.pem", "/opt/homebrew/etc/openssl@3/cert.pem", "/usr/local/etc/openssl@3/cert.pem",
        "/etc/ssl/certs/ca-certificates.crt", "/etc/pki/tls/certs/ca-bundle.crt", NULL };
    for (int i = 0; cand[i]; i++) if (access(cand[i], R_OK) == 0) { setenv("SSL_CERT_FILE", cand[i], 0); g_ca = cand[i]; return; }
}
const char *dienst_zertifikate(void) { return g_ca; }
/* Python 3 (>= 3.8) suchen: Umgebung, PATH, dann feste Orte (aus dem Finder gestartet ist der PATH oft leer) */
int dienst_python(char *out, size_t n, char *err, size_t en) {
    zertifikate_suchen();
    const char *env = getenv("MERIDIAN_PYTHON");
    const char *cand[] = { env && *env ? env : NULL, "python3", "/usr/bin/python3", "/opt/homebrew/bin/python3", "/usr/local/bin/python3", "python", NULL };
    err[0] = 0;
    for (int i = 0; i < 6; i++) {
        if (!cand[i]) continue;
        char *argv[] = { (char *)cand[i], "-c", "import sys, mmap, ssl\nif sys.version_info < (3, 8): sys.exit(3)\nsys.exit(0 if ssl.create_default_context().cert_store_stats()['x509_ca'] > 0 else 4)", NULL };
        posix_spawn_file_actions_t fa; posix_spawn_file_actions_init(&fa);
        posix_spawn_file_actions_addopen(&fa, 0, "/dev/null", O_RDONLY, 0);
        posix_spawn_file_actions_addopen(&fa, 1, "/dev/null", O_WRONLY, 0);
        posix_spawn_file_actions_addopen(&fa, 2, "/dev/null", O_WRONLY, 0);
        pid_t pid; int rc = posix_spawnp(&pid, cand[i], &fa, NULL, argv, environ); posix_spawn_file_actions_destroy(&fa);
        if (rc != 0) { snprintf(err + strlen(err), en - strlen(err), " %s: nicht gefunden;", cand[i]); continue; }
        int st = 0; waitpid(pid, &st, 0);
        if (WIFEXITED(st) && WEXITSTATUS(st) == 0) { snprintf(out, n, "%s", cand[i]); return 0; }
        if (WIFEXITED(st) && WEXITSTATUS(st) == 4) {                     /* laeuft, aber ohne Zertifikate: HTTPS-Seiten scheitern */
            snprintf(out, n, "%s", cand[i]);
            snprintf(err, en, "Python hat keine Zertifikate fuer HTTPS (CERTIFICATE_VERIFY_FAILED bei Wetter, Nachrichten, Radio). Abhilfe: in /Applications/Python 3.x/ \"Install Certificates.command\" ausfuehren oder \"pip3 install certifi\"; oder SSL_CERT_FILE=/pfad/cert.pem setzen.");
            return 0; }
        snprintf(err + strlen(err), en - strlen(err), " %s: %s;", cand[i], WIFEXITED(st) && WEXITSTATUS(st) == 3 ? "zu alt (<3.8)" : "unbrauchbar");
    }
    return -1;
}

/* Wachhund und Anpassungen im Python-Prozess: beendet den Dienst, sobald der Emulator nicht mehr da ist (auch nach einem
   Absturz); laedt draht.py als Modul, bindet die Browser-Ablage nur an 127.0.0.1 (MERIDIAN_ABLAGE_LAN=1 = wie am MiSTer)
   und nennt als Adresse "localhost". draht.py selbst bleibt unveraendert. */
static const char WACHE[] =
    "import os,sys,threading,time,importlib.util\n"
    "pp=int(sys.argv[1]); script=os.path.abspath(sys.argv[2]); sys.argv=[script]+sys.argv[3:]\n"
    "def wache():\n"
    "    while True:\n"
    "        time.sleep(1)\n"
    "        try: os.kill(pp,0)\n"
    "        except OSError: os._exit(0)\n"
    "threading.Thread(target=wache,daemon=True).start()\n"
    "sys.path.insert(0,os.path.dirname(script))\n"
    "b=os.path.join(os.path.dirname(script),'bin')\n"
    "if os.path.isdir(b):\n"                                   /* eingebauter MP3-Dekoder (als mpg123) vor dem Systempfad */
    "    os.environ['PATH']=b+os.pathsep+os.environ.get('PATH','')\n"
    "    print('Dekoder-Suchpfad:',b,flush=True)\n"
    "if not os.environ.get('SSL_CERT_FILE'):\n"
    "    try:\n"
    "        import certifi; os.environ['SSL_CERT_FILE']=certifi.where()\n"
    "    except Exception: pass\n"
    "import http.server as hs\n"
    "lokal = not os.environ.get('MERIDIAN_ABLAGE_LAN')\n"
    "if lokal:\n"
    "    _T=hs.ThreadingHTTPServer\n"
    "    class T(_T):\n"
    "        def __init__(self,addr,*a,**k): super().__init__(('127.0.0.1',addr[1]),*a,**k)\n"
    "    hs.ThreadingHTTPServer=T\n"
    "spec=importlib.util.spec_from_file_location('draht',script)\n"
    "mod=importlib.util.module_from_spec(spec); sys.modules['draht']=mod; spec.loader.exec_module(mod)\n"
    "if lokal: mod.Dienst.eigene_adresse=lambda self:'localhost'\n"
    "mod.main()\n";

int dienst_start(Machine *m, const char *script, const char *ordner, const char *python, int port, char *err, size_t errlen) {
    const char *tmp = getenv("TMPDIR"); if (!tmp || !*tmp) tmp = "/tmp";
    snprintf(g_win, sizeof g_win, "%s/meridian-draht-%d.win", tmp, (int)getpid());
    snprintf(g_log, sizeof g_log, "%s/meridian-draht.log", tmp);
    int fd = open(g_win, O_RDWR | O_CREAT | O_TRUNC, 0600);
    if (fd < 0 || ftruncate(fd, 65536) != 0) { snprintf(err, errlen, "DRAHT: Fensterdatei '%s': %s", g_win, strerror(errno)); if (fd >= 0) close(fd); return -1; }
    uint8_t *w = mmap(NULL, 65536, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0); close(fd);
    if (w == MAP_FAILED) { snprintf(err, errlen, "DRAHT: mmap: %s", strerror(errno)); unlink(g_win); return -1; }
    memcpy(w, m->draht, 65536);
    if (dienst_mkdirs(ordner) != 0) {
        struct stat sb; int ex = stat(ordner, &sb) == 0 && !S_ISDIR(sb.st_mode);
        snprintf(err, errlen, "DRAHT: Ordner '%s' laesst sich nicht anlegen: %s", ordner, ex ? "an dieser Stelle liegt schon eine Datei gleichen Namens (macOS unterscheidet Gross-/Kleinschreibung nicht) - mit --ordner einen anderen Namen waehlen" : strerror(errno));
        munmap(w, 65536); unlink(g_win); return -1; }
    char fl[1300]; snprintf(fl, sizeof fl, "%s/files", ordner); dienst_mkdirs(fl);
    char ppid[32], pstr[16]; snprintf(ppid, sizeof ppid, "%d", (int)getpid()); snprintf(pstr, sizeof pstr, "%d", port);
    char *argv[] = { (char *)python, "-u", "-c", (char *)WACHE, ppid, (char *)script, "--datei", g_win, "--ordner", (char *)ordner, "--port", pstr, NULL };
    posix_spawn_file_actions_t fa; posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_addopen(&fa, 0, "/dev/null", O_RDONLY, 0);
    posix_spawn_file_actions_addopen(&fa, 1, g_log, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    posix_spawn_file_actions_adddup2(&fa, 1, 2);
    int rc = posix_spawnp(&g_pid, python, &fa, NULL, argv, environ);
    posix_spawn_file_actions_destroy(&fa);
    if (rc != 0) { snprintf(err, errlen, "DRAHT: '%s' laesst sich nicht starten (%s) - Python 3 installiert?", python, strerror(rc)); munmap(w, 65536); unlink(g_win); g_pid = 0; return -1; }
    m->dr = w; return 0;
}
void dienst_stop(Machine *m) {
    if (g_pid) { kill(g_pid, SIGTERM); int st; waitpid(g_pid, &st, 0); g_pid = 0; }
    if (m->dr != m->draht) { memcpy(m->draht, (const void *)m->dr, 65536); munmap((void *)m->dr, 65536); m->dr = m->draht; }
    if (g_win[0]) { unlink(g_win); g_win[0] = 0; }
}

/* Probe auf dem Kanal Emulator <-> Dienst: wie ein NET-Befehl aus BASIC (Fenster: ANR 0, ANTW 1, ART 2, KANAL 3, LEN 4, STATUS 6,
   ALEN 8, HERZ $10, TEXT $100, ANTWORT $200). Danach wird der Stand wieder auf "alles beantwortet" (0/0) gesetzt. */
int dienst_rundlauf(Machine *m, char *out, size_t n) {
    volatile uint8_t *w = m->dr; uint8_t hb = w[0x10]; int t;
    for (t = 0; t < 300 && w[0x10] == hb; t++) usleep(10000);                 /* Herzschlag abwarten (max. 3 s) */
    if (w[0x10] == hb) { snprintf(out, n, "Herzschlag des Dienstes steht (Fenster %s)", g_win); return -1; }
    const char *cmd = "HELP"; uint8_t nr = (uint8_t)(w[1] + 1); if (!nr) nr = 1;
    for (int i = 0; cmd[i]; i++) w[0x100 + i] = (uint8_t)cmd[i];
    w[2] = 1; w[3] = 0; w[4] = (uint8_t)strlen(cmd); w[0] = nr;                 /* ANR zuletzt */
    for (t = 0; t < 300 && w[1] != nr; t++) usleep(10000);
    if (w[1] != nr) { snprintf(out, n, "keine Antwort auf die Anfrage nach 3 s (Dienst lebt, sieht die Anfrage aber nicht - Fenster %s)", g_win); return -2; }
    int st = (int16_t)(w[6] | (w[7] << 8)); char tx[260]; int l = w[8]; for (int i = 0; i < l; i++) tx[i] = (char)w[0x200 + i]; tx[l] = 0;
    w[0] = 0; w[1] = 0;
    snprintf(out, n, "Rundlauf ok: HELP -> Status %d, \"%.60s\"", st, tx); return 0;
}
static void logtail(char *buf, size_t n) {
    buf[0] = 0; FILE *f = fopen(g_log, "r"); if (!f) return;
    char z[8][200]; int k = 0; while (fgets(z[k & 7], 200, f)) k++; fclose(f);
    for (int i = (k > 6 ? k - 6 : 0); i < k; i++) { size_t l = strlen(buf); snprintf(buf + l, n - l, "   %s", z[i & 7]); }
}
int dienst_lebt(char *msg, size_t n) {
    if (!g_pid) { snprintf(msg, n, "nicht gestartet"); return 0; }
    int st = 0; pid_t r = waitpid(g_pid, &st, WNOHANG);
    if (r == 0) return 1;
    char t[1200]; logtail(t, sizeof t);
    snprintf(msg, n, "Dienst vorzeitig beendet (%s %d)\n%s", WIFEXITED(st) ? "Code" : "Signal", WIFEXITED(st) ? WEXITSTATUS(st) : WTERMSIG(st), t);
    g_pid = 0; return 0;
}
#endif
