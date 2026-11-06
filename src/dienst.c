/* DRAHT-Netzdienst: Fenster (Bank $FD) als gemeinsame Datei + Python-Dienst (dienst/draht.py). GPL-3.0-or-later
   macOS/Linux: mmap + posix_spawn.  Windows: CreateFileMapping + CreateProcess (siehe CHANGELOG). */
#include "dienst.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <io.h>
#include <direct.h>
#define MKDIR(p)       _mkdir(p)
#define DIRSEP2        '\\'
#define FILE_EXISTS(p) (_access((p), 0) == 0)
#define UNLINK(p)      _unlink(p)
static void sleep_ms(int ms) { Sleep((DWORD)ms); }
#else
#include <signal.h>
#include <spawn.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>
#define MKDIR(p)       mkdir((p), 0777)
#define DIRSEP2        '/'
#define FILE_EXISTS(p) (access((p), F_OK) == 0)
#define UNLINK(p)      unlink(p)
static void sleep_ms(int ms) { usleep((useconds_t)ms * 1000); }
extern char **environ;
#endif

static char g_win[1100], g_log[1100];
const char *dienst_log(void) { return g_log; }

int dienst_mkdirs(const char *pfad) {
    char b[1200]; snprintf(b, sizeof b, "%s", pfad);
    for (char *p = b + 1; *p; p++) if (*p == '/' || *p == DIRSEP2) { char c = *p; *p = 0; MKDIR(b); *p = c; }
    if (MKDIR(b) != 0 && errno != EEXIST) return -1;
    struct stat st; return (stat(b, &st) == 0 && S_ISDIR(st.st_mode)) ? 0 : -1;
}
/* HVSC-Index fuer SID JAM (Allein-Modus) nach <ordner>/HVSC/ legen, wenn dort noch keiner liegt.
   0 = kopiert, 1 = war schon da, -1 = Fehler. Eine vorhandene (eigene) Datei wird nie ueberschrieben. */
int dienst_index_kopieren(const char *quelle, const char *ordner) {
    char dir[1300], dst[1400], tmp[1420];
    snprintf(dir, sizeof dir, "%s/HVSC", ordner); snprintf(dst, sizeof dst, "%s/hvsc_index.tsv.gz", dir); snprintf(tmp, sizeof tmp, "%s.neu", dst);
    if (FILE_EXISTS(dst)) return 1;
    if (dienst_mkdirs(dir) != 0) return -1;
    FILE *in = fopen(quelle, "rb"); if (!in) return -1;
    FILE *out = fopen(tmp, "wb"); if (!out) { fclose(in); return -1; }
    char b[65536]; size_t n; int ok = 1;
    while ((n = fread(b, 1, sizeof b, in)) > 0) if (fwrite(b, 1, n, out) != n) { ok = 0; break; }
    ok = ok && !ferror(in); fclose(in); if (fclose(out) != 0) ok = 0;
    if (!ok || rename(tmp, dst) != 0) { UNLINK(tmp); return -1; }
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
        if (FILE_EXISTS(dst)) continue;
        FILE *in = fopen(src, "rb"); if (!in) continue;
        FILE *out = fopen(tmp, "wb"); if (!out) { fclose(in); continue; }
        char b[16384]; size_t n; int ok = 1;
        while ((n = fread(b, 1, sizeof b, in)) > 0) if (fwrite(b, 1, n, out) != n) { ok = 0; break; }
        ok = ok && !ferror(in); fclose(in); if (fclose(out) != 0) ok = 0;
        if (ok && rename(tmp, dst) == 0) neu++; else UNLINK(tmp);
    }
    return neu;
}
/* Probe auf dem Kanal Emulator <-> Dienst: wie ein NET-Befehl aus BASIC (Fenster: ANR 0, ANTW 1, ART 2, KANAL 3, LEN 4, STATUS 6,
   ALEN 8, HERZ $10, TEXT $100, ANTWORT $200). Danach wird der Stand wieder auf "alles beantwortet" (0/0) gesetzt. */
int dienst_rundlauf(Machine *m, char *out, size_t n) {
    volatile uint8_t *w = m->dr; uint8_t hb = w[0x10]; int t;
    for (t = 0; t < 300 && w[0x10] == hb; t++) sleep_ms(10);                 /* Herzschlag abwarten (max. 3 s) */
    if (w[0x10] == hb) { snprintf(out, n, "Herzschlag des Dienstes steht (Fenster %s)", g_win); return -1; }
    const char *cmd = "HELP"; uint8_t nr = (uint8_t)(w[1] + 1); if (!nr) nr = 1;
    for (int i = 0; cmd[i]; i++) w[0x100 + i] = (uint8_t)cmd[i];
    w[2] = 1; w[3] = 0; w[4] = (uint8_t)strlen(cmd); w[0] = nr;                 /* ANR zuletzt */
    for (t = 0; t < 300 && w[1] != nr; t++) sleep_ms(10);
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

#define WACHE_REST \
    "sys.path.insert(0,os.path.dirname(script))\n" \
    "b=os.path.join(os.path.dirname(script),'bin')\n" \
    "if os.path.isdir(b):\n" \
    "    os.environ['PATH']=b+os.pathsep+os.environ.get('PATH','')\n" \
    "    print('Dekoder-Suchpfad:',b,flush=True)\n" \
    "if not os.environ.get('SSL_CERT_FILE'):\n" \
    "    try:\n" \
    "        import certifi; os.environ['SSL_CERT_FILE']=certifi.where()\n" \
    "    except Exception: pass\n" \
    "import http.server as hs\n" \
    "lokal = not os.environ.get('MERIDIAN_ABLAGE_LAN')\n" \
    "if lokal:\n" \
    "    _T=hs.ThreadingHTTPServer\n" \
    "    class T(_T):\n" \
    "        def __init__(self,addr,*a,**k): super().__init__(('127.0.0.1',addr[1]),*a,**k)\n" \
    "    hs.ThreadingHTTPServer=T\n" \
    "spec=importlib.util.spec_from_file_location('draht',script)\n" \
    "mod=importlib.util.module_from_spec(spec); sys.modules['draht']=mod; spec.loader.exec_module(mod)\n" \
    "if lokal: mod.Dienst.eigene_adresse=lambda self:'localhost'\n" \
    "mod.main()\n"

#if defined(_WIN32)
/* ------------------------------------------------------------------------------------------------ Windows */
/* Fensterdatei: CreateFile + CreateFileMapping (Emulator) und mmap.mmap(fd) in Python zeigen auf dieselbe Datei; Windows haelt beide
   Ansichten einer Datei zusammen (gleicher Abschnitt im Dateisystem-Cache), also genau wie MAP_SHARED unter macOS/Linux. */
static HANDLE g_proc, g_fh, g_map; static char g_wache[1100];
static const char *g_ca = "";
const char *dienst_zertifikate(void) { return g_ca; }

/* ein Argument nach den Regeln von CommandLineToArgvW anhaengen (Leerzeichen, Anfuehrungszeichen, Backslashes vor Anfuehrungszeichen) */
static void wq(char *d, size_t n, const char *a) {
    size_t o = strlen(d);
#define WPUT(c) do { if (o + 2 < n) d[o++] = (char)(c); } while (0)
    int need = !*a || strpbrk(a, " \t\n\v\"") != NULL;
    if (o) WPUT(' ');
    if (!need) { while (*a) { WPUT(*a); a++; } d[o] = 0; return; }
    WPUT('"');
    for (;; a++) {
        int bs = 0, i; while (*a == '\\') { bs++; a++; }
        if (!*a) { for (i = 0; i < bs * 2; i++) WPUT('\\'); break; }
        if (*a == '"') { for (i = 0; i < bs * 2 + 1; i++) WPUT('\\'); WPUT('"'); }
        else { for (i = 0; i < bs; i++) WPUT('\\'); WPUT(*a); }
    }
    WPUT('"'); d[o] = 0;
#undef WPUT
}
static HANDLE win_nul(void) {
    SECURITY_ATTRIBUTES sa = { sizeof sa, NULL, TRUE };
    return CreateFileA("NUL", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, NULL);
}
/* Programm starten (ohne Fenster, Ein-/Ausgabe umgeleitet); wait > 0: so viele ms auf das Ende warten und den Exitcode liefern */
static int win_spawn(const char *cmdline, HANDLE hout, DWORD wait_ms, DWORD *code, HANDLE *proc_out, char *err, size_t en) {
    HANDLE nul = win_nul();
    STARTUPINFOA si; PROCESS_INFORMATION pi; char *cl = _strdup(cmdline);
    memset(&si, 0, sizeof si); si.cb = sizeof si; si.dwFlags = STARTF_USESTDHANDLES; si.hStdInput = nul;
    si.hStdOutput = hout ? hout : nul; si.hStdError = hout ? hout : nul;
    BOOL ok = cl && CreateProcessA(NULL, cl, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi);
    DWORD le = GetLastError(); free(cl); if (nul != INVALID_HANDLE_VALUE) CloseHandle(nul);
    if (!ok) { if (err) snprintf(err, en, "Windows-Fehler %lu", (unsigned long)le); return -1; }
    CloseHandle(pi.hThread);
    if (wait_ms) {
        if (WaitForSingleObject(pi.hProcess, wait_ms) != WAIT_OBJECT_0) { TerminateProcess(pi.hProcess, 99); CloseHandle(pi.hProcess); if (code) *code = 99; return 0; }
        if (code) { GetExitCodeProcess(pi.hProcess, code); }
        CloseHandle(pi.hProcess); return 0;
    }
    *proc_out = pi.hProcess; return 0;
}
/* Python 3 (>= 3.8) suchen: Umgebung, python, python3, py -3. Die "Python"-Platzhalter des Microsoft Store (WindowsApps) melden Fehler und fallen durch. */
int dienst_python(char *out, size_t n, char *err, size_t en) {
    const char *env = getenv("SSL_CERT_FILE"); if (env && *env) g_ca = env;
    env = getenv("MERIDIAN_PYTHON");
    const char *cand[] = { env && *env ? env : NULL, "python", "python3", "py -3", NULL };
    static const char probe[] = "import sys,mmap,ssl;sys.exit(3 if sys.version_info<(3,8) else (0 if ssl.create_default_context().cert_store_stats()['x509_ca']>0 else 4))";
    err[0] = 0;
    for (int i = 0; i < 4; i++) {
        if (!cand[i]) continue;
        char cl[1400] = ""; DWORD code = 0; char e2[80] = "";
        if (!strcmp(cand[i], "py -3")) { wq(cl, sizeof cl, "py"); wq(cl, sizeof cl, "-3"); } else wq(cl, sizeof cl, cand[i]);
        wq(cl, sizeof cl, "-c"); wq(cl, sizeof cl, probe);
        if (win_spawn(cl, NULL, 30000, &code, NULL, e2, sizeof e2) != 0) { snprintf(err + strlen(err), en - strlen(err), " %s: nicht gefunden;", cand[i]); continue; }
        if (code == 0) { snprintf(out, n, "%s", cand[i]); return 0; }
        if (code == 4) {                                              /* laeuft, aber ohne Zertifikate: HTTPS-Seiten scheitern */
            snprintf(out, n, "%s", cand[i]);
            snprintf(err, en, "Python hat keine Zertifikate fuer HTTPS (CERTIFICATE_VERIFY_FAILED bei Wetter, Nachrichten, Radio). Abhilfe: \"pip install certifi\" oder SSL_CERT_FILE=C:\\pfad\\cacert.pem setzen.");
            return 0; }
        snprintf(err + strlen(err), en - strlen(err), " %s: %s;", cand[i], code == 3 ? "zu alt (<3.8)" : "unbrauchbar (z. B. Platzhalter des Microsoft Store)");
    }
    return -1;
}

/* Wachhund und Anpassungen im Python-Prozess (siehe WACHE_REST). Unter Windows wartet ein Thread auf das Ende des Emulators
   (os.kill(pid, 0) wuerde hier den Emulator beenden!) und fuenf Kleinigkeiten, die es nur unter Unix gibt, werden nachgebildet:
   os.statvfs (freier Platz fuer DIR und die Radio-Aufnahme), os.ftruncate auf die schon richtige Groesse (die Fensterdatei ist
   bereits abgebildet), die Hilfsprogramme mit .exe (bildwandler, sidkern, sidton), UTF-8 fuer Textdateien und Ausgabe und die
   HTTPS-Zertifikate: Windows laedt Wurzelzertifikate erst bei Bedarf nach, Python sieht dann nur einen Teil (CERTIFICATE_VERIFY_FAILED
   bei api.open-meteo.com); deshalb dienen certifi oder dienst/cacert.pem (Mozilla-Bundle) als SSL_CERT_FILE, wenn keines gesetzt ist. */
static const char WACHE_KOPF[] =
    "import os,sys,threading,shutil,ctypes,importlib.util\n"
    "pp=int(sys.argv[1]); script=os.path.abspath(sys.argv[2]); sys.argv=[script]+sys.argv[3:]\n"
    "k=ctypes.WinDLL('kernel32')\n"
    "k.OpenProcess.restype=ctypes.c_void_p\n"
    "k.OpenProcess.argtypes=[ctypes.c_uint32,ctypes.c_int,ctypes.c_uint32]\n"
    "k.WaitForSingleObject.argtypes=[ctypes.c_void_p,ctypes.c_uint32]\n"
    "h=k.OpenProcess(0x100000,0,pp)\n"
    "def wache():\n"
    "    k.WaitForSingleObject(h,0xFFFFFFFF)\n"
    "    os._exit(0)\n"
    "if h: threading.Thread(target=wache,daemon=True).start()\n"
    "if not hasattr(os,'statvfs'):\n"
    "    class _SV: pass\n"
    "    def _statvfs(p):\n"
    "        s=_SV(); s.f_frsize=4096; s.f_bavail=shutil.disk_usage(p).free//4096; return s\n"
    "    os.statvfs=_statvfs\n"
    "if not os.environ.get('SSL_CERT_FILE'):\n"
    "    _ca=None\n"
    "    try:\n"
    "        import certifi; _ca=certifi.where()\n"
    "    except Exception: pass\n"
    "    if not (_ca and os.path.isfile(_ca)): _ca=os.path.join(os.path.dirname(script),'cacert.pem')\n"
    "    if os.path.isfile(_ca): os.environ['SSL_CERT_FILE']=_ca\n"
    "_ft=os.ftruncate\n"
    "os.ftruncate=lambda fd,n: None if os.fstat(fd).st_size==n else _ft(fd,n)\n"
    "for _v,_n in (('MERIDIAN_BILDWANDLER','bildwandler.exe'),('SIDJAM_SIDKERN','sidkern.exe'),('SIDJAM_SIDTON','sidton.exe')):\n"
    "    _p=os.path.join(os.path.dirname(script),_n)\n"
    "    if not os.environ.get(_v) and os.path.isfile(_p): os.environ[_v]=_p\n";

int dienst_start(Machine *m, const char *script, const char *ordner, const char *python, int port, char *err, size_t errlen) {
    char tmp[600]; DWORD tl = GetTempPathA(sizeof tmp, tmp); if (!tl || tl >= sizeof tmp) snprintf(tmp, sizeof tmp, ".\\");
    DWORD pid = GetCurrentProcessId();
    snprintf(g_win, sizeof g_win, "%smeridian-draht-%lu.win", tmp, (unsigned long)pid);
    snprintf(g_log, sizeof g_log, "%smeridian-draht.log", tmp);
    snprintf(g_wache, sizeof g_wache, "%smeridian-draht-%lu-wache.py", tmp, (unsigned long)pid);
    g_fh = CreateFileA(g_win, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY, NULL);
    if (g_fh == INVALID_HANDLE_VALUE) { snprintf(err, errlen, "DRAHT: Fensterdatei '%s': Windows-Fehler %lu", g_win, (unsigned long)GetLastError()); g_fh = NULL; return -1; }
    g_map = CreateFileMappingA(g_fh, NULL, PAGE_READWRITE, 0, 65536, NULL);           /* verlaengert die Datei auf 64 KB */
    uint8_t *w = g_map ? (uint8_t *)MapViewOfFile(g_map, FILE_MAP_WRITE, 0, 0, 65536) : NULL;
    if (!w) { snprintf(err, errlen, "DRAHT: Dateiabbildung '%s': Windows-Fehler %lu", g_win, (unsigned long)GetLastError());
        if (g_map) { CloseHandle(g_map); }
        CloseHandle(g_fh); g_map = g_fh = NULL; DeleteFileA(g_win); return -1; }
    memcpy(w, m->draht, 65536);
    if (dienst_mkdirs(ordner) != 0) {
        snprintf(err, errlen, "DRAHT: Ordner '%s' laesst sich nicht anlegen - mit --ordner einen anderen waehlen", ordner);
        UnmapViewOfFile(w); CloseHandle(g_map); CloseHandle(g_fh); g_map = g_fh = NULL; DeleteFileA(g_win); return -1; }
    char fl[1300]; snprintf(fl, sizeof fl, "%s/files", ordner); dienst_mkdirs(fl);
    FILE *wf = fopen(g_wache, "wb");                                             /* Wachhund als Datei: Zeilenumbrueche in der Kommandozeile vermeiden */
    if (!wf || fputs(WACHE_KOPF, wf) < 0 || fputs(WACHE_REST, wf) < 0 || fclose(wf) != 0) {
        snprintf(err, errlen, "DRAHT: Hilfsdatei '%s' laesst sich nicht schreiben", g_wache);
        UnmapViewOfFile(w); CloseHandle(g_map); CloseHandle(g_fh); g_map = g_fh = NULL; DeleteFileA(g_win); return -1; }
    if (!getenv("PYTHONUTF8")) SetEnvironmentVariableA("PYTHONUTF8", "1");           /* wie unter macOS/Linux: Textdateien und Ausgabe als UTF-8 */
    if (!getenv("PYTHONIOENCODING")) SetEnvironmentVariableA("PYTHONIOENCODING", "utf-8");
    char cl[4000] = "", ppid[32], pstr[16]; snprintf(ppid, sizeof ppid, "%lu", (unsigned long)pid); snprintf(pstr, sizeof pstr, "%d", port);
    if (!strcmp(python, "py -3")) { wq(cl, sizeof cl, "py"); wq(cl, sizeof cl, "-3"); } else wq(cl, sizeof cl, python);
    wq(cl, sizeof cl, "-u"); wq(cl, sizeof cl, g_wache); wq(cl, sizeof cl, ppid); wq(cl, sizeof cl, script);
    wq(cl, sizeof cl, "--datei"); wq(cl, sizeof cl, g_win); wq(cl, sizeof cl, "--ordner"); wq(cl, sizeof cl, ordner); wq(cl, sizeof cl, "--port"); wq(cl, sizeof cl, pstr);
    SECURITY_ATTRIBUTES sa = { sizeof sa, NULL, TRUE };
    HANDLE lg = CreateFileA(g_log, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, &sa, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    char e2[100] = "";
    int rc = win_spawn(cl, lg != INVALID_HANDLE_VALUE ? lg : NULL, 0, NULL, &g_proc, e2, sizeof e2);
    if (lg != INVALID_HANDLE_VALUE) CloseHandle(lg);
    if (rc != 0) { snprintf(err, errlen, "DRAHT: '%s' laesst sich nicht starten (%s) - Python 3 installiert?", python, e2);
        UnmapViewOfFile(w); CloseHandle(g_map); CloseHandle(g_fh); g_map = g_fh = NULL; DeleteFileA(g_win); DeleteFileA(g_wache); g_proc = NULL; return -1; }
    m->dr = w; return 0;
}
void dienst_stop(Machine *m) {
    if (g_proc) { TerminateProcess(g_proc, 0); WaitForSingleObject(g_proc, 3000); CloseHandle(g_proc); g_proc = NULL; }
    if (m->dr != m->draht) { memcpy(m->draht, (const void *)m->dr, 65536); UnmapViewOfFile((LPCVOID)m->dr); m->dr = m->draht; }
    if (g_map) { CloseHandle(g_map); g_map = NULL; }
    if (g_fh) { CloseHandle(g_fh); g_fh = NULL; }
    if (g_win[0]) { DeleteFileA(g_win); g_win[0] = 0; }
    if (g_wache[0]) { DeleteFileA(g_wache); g_wache[0] = 0; }
}
int dienst_lebt(char *msg, size_t n) {
    if (!g_proc) { snprintf(msg, n, "nicht gestartet"); return 0; }
    if (WaitForSingleObject(g_proc, 0) == WAIT_TIMEOUT) return 1;
    DWORD code = 0; GetExitCodeProcess(g_proc, &code); char t[1200]; logtail(t, sizeof t);
    snprintf(msg, n, "Dienst vorzeitig beendet (Code %lu)\n%s", (unsigned long)code, t);
    CloseHandle(g_proc); g_proc = NULL; return 0;
}
#else
static pid_t g_pid;
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
static const char WACHE_UNIX[] =
    "import os,sys,threading,time,importlib.util\n"
    "pp=int(sys.argv[1]); script=os.path.abspath(sys.argv[2]); sys.argv=[script]+sys.argv[3:]\n"
    "def wache():\n"
    "    while True:\n"
    "        time.sleep(1)\n"
    "        try: os.kill(pp,0)\n"
    "        except OSError: os._exit(0)\n"
    "threading.Thread(target=wache,daemon=True).start()\n"
    WACHE_REST;

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
    char *argv[] = { (char *)python, "-u", "-c", (char *)WACHE_UNIX, ppid, (char *)script, "--datei", g_win, "--ordner", (char *)ordner, "--port", pstr, NULL };
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

int dienst_lebt(char *msg, size_t n) {
    if (!g_pid) { snprintf(msg, n, "nicht gestartet"); return 0; }
    int st = 0; pid_t r = waitpid(g_pid, &st, WNOHANG);
    if (r == 0) return 1;
    char t[1200]; logtail(t, sizeof t);
    snprintf(msg, n, "Dienst vorzeitig beendet (%s %d)\n%s", WIFEXITED(st) ? "Code" : "Signal", WIFEXITED(st) ? WEXITSTATUS(st) : WTERMSIG(st), t);
    g_pid = 0; return 0;
}
#endif
