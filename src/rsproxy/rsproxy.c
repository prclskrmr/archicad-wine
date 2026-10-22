#include <windows.h>
#include <wchar.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

typedef HRSRC (WINAPI *pFindResourceW)(HMODULE, LPCWSTR, LPCWSTR);
typedef HRSRC (WINAPI *pFindResourceExW)(HMODULE, LPCWSTR, LPCWSTR, WORD);
typedef DWORD (WINAPI *pSizeofResource)(HMODULE, HRSRC);
typedef HGLOBAL (WINAPI *pLoadResource)(HMODULE, HRSRC);
typedef LPVOID (WINAPI *pLockResource)(HGLOBAL);
typedef BOOL (WINAPI *pFreeResource)(HGLOBAL);
typedef DWORD (WINAPI *pGetModuleFileNameW)(HMODULE, LPWSTR, DWORD);
typedef HMODULE (WINAPI *pLoadLibraryExW)(LPCWSTR, HANDLE, DWORD);

static pFindResourceW rFindResourceW;
static pFindResourceExW rFindResourceExW;
static pSizeofResource rSizeofResource;
static pLoadResource rLoadResource;
static pLockResource rLockResource;
static pFreeResource rFreeResource;
static pGetModuleFileNameW rGetModuleFileNameW;
static pLoadLibraryExW rLoadLibraryExW;

static CRITICAL_SECTION g_cs;

typedef struct { HRSRC hr; HMODULE sat; char type[16]; char name[40]; } MAPENT;
static MAPENT *g_map;
static int g_cap, g_n;

#define MAXCACHE 256
typedef struct { wchar_t path[MAX_PATH]; HMODULE mod; } CACHEENT;
static CACHEENT *g_cache;
static int g_ncache;

static void dbg(const char *fmt, ...)
{
    va_list ap;
    FILE *f = fopen("Z:\\tmp\\rsproxy.log", "a");
    if (!f) return;
    va_start(ap, fmt); vfprintf(f, fmt, ap); va_end(ap);
    fputc('\n', f); fclose(f);
}

/* Resource-lookup diagnostics are extremely chatty (thousands per minute
 * while Archicad repaints).  They are only logged when RSPROXY_VERBOSE=1,
 * otherwise just the first NOISE_MAX lines are kept. */
static int g_verbose;
static int g_noise;
#define NOISE_MAX 20

static void dq(const char *fmt, ...)
{
    va_list ap;
    FILE *f;

    if (!g_verbose && g_noise >= NOISE_MAX) return;
    g_noise++;
    f = fopen("Z:\\tmp\\rsproxy.log", "a");
    if (!f) return;
    va_start(ap, fmt); vfprintf(f, fmt, ap); va_end(ap);
    fputc('\n', f); fclose(f);
}

static void nm2(LPCWSTR p, char *b, int n)
{
    if (!p) { lstrcpynA(b, "(null)", n); return; }
    if ((ULONG_PTR)p >> 16) WideCharToMultiByte(CP_ACP, 0, p, -1, b, n, NULL, NULL);
    else wsprintfA(b, "#%lu", (ULONG)(ULONG_PTR)p);
}

static void init_procs(void)
{
    HMODULE k = GetModuleHandleW(L"kernel32.dll");
    rFindResourceW = (pFindResourceW)GetProcAddress(k, "FindResourceW");
    rFindResourceExW = (pFindResourceExW)GetProcAddress(k, "FindResourceExW");
    rSizeofResource = (pSizeofResource)GetProcAddress(k, "SizeofResource");
    rLoadResource = (pLoadResource)GetProcAddress(k, "LoadResource");
    rLockResource = (pLockResource)GetProcAddress(k, "LockResource");
    rFreeResource = (pFreeResource)GetProcAddress(k, "FreeResource");
    rGetModuleFileNameW = (pGetModuleFileNameW)GetProcAddress(k, "GetModuleFileNameW");
    rLoadLibraryExW = (pLoadLibraryExW)GetProcAddress(k, "LoadLibraryExW");
}

static int get_info(HRSRC h, char *t, char *n, int tn, int nn)
{
    int found = 0;
    EnterCriticalSection(&g_cs);
    for (int i = 0; i < g_n; i++)
        if (g_map[i].hr == h) { lstrcpynA(t, g_map[i].type, tn); lstrcpynA(n, g_map[i].name, nn); found = 1; break; }
    LeaveCriticalSection(&g_cs);
    return found;
}

static HMODULE lookup_sat(HRSRC h)
{
    HMODULE r = NULL;
    EnterCriticalSection(&g_cs);
    for (int i = 0; i < g_n; i++)
        if (g_map[i].hr == h) { r = g_map[i].sat; break; }
    LeaveCriticalSection(&g_cs);
    return r;
}

static void record_sat(HRSRC h, HMODULE sat, const char *type, const char *name)
{
    EnterCriticalSection(&g_cs);
    if (g_n == g_cap) {
        int cap = g_cap ? g_cap * 2 : 1024;
        MAPENT *n = (MAPENT *)HeapAlloc(GetProcessHeap(), 0, cap * sizeof(MAPENT));
        if (n) {
            if (g_map) {
                memcpy(n, g_map, g_n * sizeof(MAPENT));
                HeapFree(GetProcessHeap(), 0, g_map);
            }
            g_map = n; g_cap = cap;
        }
    }
    if (g_n < g_cap) {
        g_map[g_n].hr = h; g_map[g_n].sat = sat;
        lstrcpynA(g_map[g_n].type, type, 16); lstrcpynA(g_map[g_n].name, name, 40);
        g_n++;
    }
    LeaveCriticalSection(&g_cs);
}

static const wchar_t *g_langs[4] = { L"en", L"en-US", L"pt-BR", NULL };

static HMODULE sat_load(const wchar_t *path)
{
    EnterCriticalSection(&g_cs);
    for (int i = 0; i < g_ncache; i++)
        if (!_wcsicmp(g_cache[i].path, path)) { HMODULE m = g_cache[i].mod; LeaveCriticalSection(&g_cs); return m; }
    LeaveCriticalSection(&g_cs);

    HMODULE m = rLoadLibraryExW(path, NULL, LOAD_LIBRARY_AS_DATAFILE);
    if (!m) {
        dq("SATLOADFAIL path=%ls exists=%lu err=%lu", path,
            (unsigned long)(GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES),
            (unsigned long)GetLastError());
        return NULL;
    }

    EnterCriticalSection(&g_cs);
    if (g_ncache < MAXCACHE) {
        wcsncpy(g_cache[g_ncache].path, path, MAX_PATH - 1);
        g_cache[g_ncache].path[MAX_PATH - 1] = 0;
        g_cache[g_ncache].mod = m;
        g_ncache++;
    }
    LeaveCriticalSection(&g_cs);
    return m;
}

static HMODULE try_satellite(const wchar_t *modpath, LPCWSTR lpName, LPCWSTR lpType, HRSRC *out)
{
    wchar_t dir[MAX_PATH], base[MAX_PATH], stem[MAX_PATH];
    wcsncpy(dir, modpath, MAX_PATH - 1); dir[MAX_PATH - 1] = 0;
    wchar_t *s = wcsrchr(dir, L'\\');
    if (!s) return NULL;
    *s = 0;
    wcsncpy(base, s + 1, MAX_PATH - 1); base[MAX_PATH - 1] = 0;
    wcsncpy(stem, base, MAX_PATH - 1); stem[MAX_PATH - 1] = 0;
    wchar_t *dot = wcsrchr(stem, L'.');
    if (dot) *dot = 0;

    for (int i = 0; g_langs[i] && i < 3; i++) {
        wchar_t p1[MAX_PATH], p2[MAX_PATH];
        wsprintfW(p1, L"%s\\%s\\%s.mui", dir, g_langs[i], base);
        wsprintfW(p2, L"%s\\%s\\%s.mui", dir, g_langs[i], stem);
        const wchar_t *cands[2] = { p1, p2 };
        for (int c = 0; c < 2; c++) {
            HMODULE sat = sat_load(cands[c]);
            if (!sat) continue;
            HRSRC h = rFindResourceW(sat, lpName, lpType);
            if (h) { char t2[128], n2[128]; nm2(lpType, t2, sizeof t2); nm2(lpName, n2, sizeof n2); record_sat(h, sat, t2, n2); *out = h; return sat; }
        }
    }
    return NULL;
}

HRSRC WINAPI FindResourceW(HMODULE hModule, LPCWSTR lpName, LPCWSTR lpType)
{
    if (!rFindResourceW) init_procs();
    HRSRC h = rFindResourceW(hModule, lpName, lpType);
    if (h) return h;

    wchar_t path[MAX_PATH];
    if (!rGetModuleFileNameW(hModule, path, MAX_PATH) || !path[0]) return NULL;

    char t[128], na[128];
    nm2(lpType, t, sizeof t);
    nm2(lpName, na, sizeof na);
    HRSRC hs = NULL;
    try_satellite(path, lpName, lpType, &hs);
    if (hs) {
        dq("SAT  mod=%ls type=%s name=%s", path, t, na);
        return hs;
    }
    dq("MISS mod=%ls type=%s name=%s", path, t, na);
    return NULL;
}

HRSRC WINAPI FindResourceExW(HMODULE hModule, LPCWSTR lpType, LPCWSTR lpName, WORD wLang)
{
    if (!rFindResourceExW) init_procs();
    HRSRC h = rFindResourceExW(hModule, lpType, lpName, wLang);
    if (h) return h;
    return FindResourceW(hModule, lpName, lpType);
}

DWORD WINAPI SizeofResource(HMODULE hModule, HRSRC hResInfo)
{
    if (!rSizeofResource) init_procs();
    HMODULE sat = lookup_sat(hResInfo);
    if (sat && !rSizeofResource(sat, hResInfo)) {
        dq("SZFAIL sat=%p", (void*)sat);
    }
    return rSizeofResource(sat ? sat : hModule, hResInfo);
}

HGLOBAL WINAPI LoadResource(HMODULE hModule, HRSRC hResInfo)
{
    if (!rLoadResource) init_procs();
    HMODULE sat = lookup_sat(hResInfo);
    if (sat) {
        char t[16], n[40];
        get_info(hResInfo, t, n, 16, 40);
        DWORD sz = rSizeofResource(sat, hResInfo);
        HGLOBAL g = rLoadResource(sat, hResInfo);
        const unsigned char *q = g ? (const unsigned char *)rLockResource(g) : NULL;
        dq("RESLOAD type=%s name=%s size=%lu ptr=%p first=%02x%02x%02x%02x%s", t, n,
            (unsigned long)sz, (void*)q,
            sz>0&&q?q[0]:0, sz>1&&q?q[1]:0, sz>2&&q?q[2]:0, sz>3&&q?q[3]:0,
            (g && sz < 4) ? "  <<< TINY" : "");
        return g;
    }
    return rLoadResource(hModule, hResInfo);
}

LPVOID WINAPI LockResource(HGLOBAL hResData)
{
    if (!rLockResource) init_procs();
    return rLockResource(hResData);
}

BOOL WINAPI FreeResource(HGLOBAL hResData)
{
    if (!rFreeResource) init_procs();
    return rFreeResource(hResData);
}

/* ---- Wine ucrtbase printf workaround -------------------------------
 * Wine's __stdio_common_vsprintf_s with bufcnt=(size_t)-1 writes the
 * terminating NUL at buf+bufcnt (wild write) whenever the format
 * contains %n/%ln/%lln.  GS formats its log timestamp with
 * "[%hhu:%hhu:%hhu]%lln" and passes SIZE_MAX -> access violation.
 * We replace the CRT function pointer GS keeps in Archicad.exe with a
 * shim that renders into a bounded temp buffer and copies back.
 */
typedef int (__cdecl *pfn_vsprintf_s)(unsigned __int64, char *, size_t, const char *, void *, va_list);
static pfn_vsprintf_s g_crt_real;
static LONG g_crt_calls;
static int g_slot_rw;
/* Rewriting %n to "%.0s" makes Wine's formatter produce the real text, but
   with Archicad's "[%hhu:%hhu:%hhu]%lln" the caller then faults later, so it
   stays opt-in (RSPROXY_SANITIZE=1). */
static int g_sanitize;
static LONG g_crt_entered;
static int g_buf_rw;
static int g_buf_rw_fail;

/* Wine's ucrtbase has no %n support at all: a format such as
 * "[%hhu:%hhu:%hhu]%lln" makes __stdio_common_vsprintf_s return -1 and
 * drop the whole formatted result.  Rewrite each %n conversion to "%.0s",
 * which consumes the same pointer argument, prints nothing, and lets the
 * rest of the format be produced normally. */
static int sanitize_fmt(const char *fmt, char *out, size_t outsz)
{
    size_t o = 0;
    const char *p = fmt;
    int changed = 0;

    while (*p) {
        const char *s;
        size_t n;

        if (*p != '%') {
            if (o + 1 < outsz) out[o++] = *p;
            p++;
            continue;
        }
        s = p + 1;
        if (*s == '%') {
            if (o + 2 < outsz) { out[o++] = '%'; out[o++] = '%'; }
            p += 2;
            continue;
        }
        while (*s && strchr("-+ #0", *s)) s++;
        if (*s == '*') s++;
        while (*s >= '0' && *s <= '9') s++;
        if (*s == '.') {
            s++;
            if (*s == '*') s++;
            while (*s >= '0' && *s <= '9') s++;
        }
        while (*s && strchr("hljztL", *s)) s++;
        if (!*s) break;
        if (*s == 'n') {
            changed = 1;
            if (o + 5 < outsz) { memcpy(out + o, "%.0s", 4); o += 4; }
        } else {
            n = (size_t)(s + 1 - p);
            if (o + n < outsz) { memcpy(out + o, p, n); o += n; }
        }
        p = s + 1;
    }
    out[o < outsz ? o : outsz - 1] = 0;
    return changed;
}

static int __cdecl crt_vsprintf_s(unsigned __int64 opts, char *buf, size_t n,
                                  const char *fmt, void *loc, va_list ap)
{
    char tmp[4096];
    char fbuf[512];
    const char *use = fmt;
    int r;

    if (!g_crt_real) return -1;
    /* Archicad formats straight into a global inside GSRoot.dll.  Like the
       CRT pointer slot, that page is mapped read-only (Wibu-protected
       section), so the formatter's terminating NUL store faults there.
       Make the destination page writable, as on Windows. */
    if (buf) {
        DWORD old;
        if (!g_buf_rw && VirtualProtect(buf, 0x1000, PAGE_READWRITE, &old)) g_buf_rw++;
        else if (!g_buf_rw && !g_buf_rw_fail) {
            g_buf_rw_fail = 1;
            dbg("crtfix: VirtualProtect(buf=%p) failed %lu", (void *)buf, GetLastError());
        }
    }
    if (InterlockedIncrement(&g_crt_entered) <= 10)
        dbg("crtfix: ENTER opts=%#llx buf=%p n=%#llx fmt=[%.100s]",
            (unsigned long long)opts, (void *)buf, (unsigned long long)n, fmt ? fmt : "(null)");
    if (!buf || !fmt || n <= 0x1000000)
        return g_crt_real(opts, buf, n, fmt, loc, ap);

    if (g_sanitize && sanitize_fmt(fmt, fbuf, sizeof(fbuf))) use = fbuf;
    r = g_crt_real(opts, tmp, sizeof(tmp) - 1, use, loc, ap);
    tmp[sizeof(tmp) - 1] = 0;
    if (r < 0) r = (int)strlen(tmp);
    else if ((size_t)r > sizeof(tmp) - 1) r = (int)sizeof(tmp) - 1;
    memcpy(buf, tmp, (size_t)r + 1);
    if (InterlockedIncrement(&g_crt_calls) <= 5)
        dbg("crtfix: fmt=[%s] -> r=%d buf=[%s]", use, r, buf);
    return r;
}

static DWORD WINAPI crtfix_thread(LPVOID unused)
{
    char name[MAX_PATH];
    HMODULE exe, ucrt;
    void **slot, *v;

    for (;;) {
        Sleep(50);
        exe = GetModuleHandleW(NULL);
        ucrt = GetModuleHandleW(L"ucrtbase.dll");
        if (!exe || !ucrt) continue;
        name[0] = 0;
        GetModuleFileNameA(exe, name, sizeof(name));
        if (!strstr(name, "Archicad.exe")) continue;
        slot = (void **)((char *)exe + 0x25E1DD0);
        v = *slot;
        if (!v || v == (void *)&crt_vsprintf_s) continue;
        if ((char *)v < (char *)ucrt || (char *)v > (char *)ucrt + 0x400000) continue;
        if (!g_crt_real) {
            g_crt_real = (pfn_vsprintf_s)v;
            dbg("crtfix: real %p, patching slot %p", v, slot);
        }
        {
            DWORD old;
            if (!g_slot_rw) {
                if (!VirtualProtect(slot, sizeof(void *), PAGE_READWRITE, &old)) {
                    dbg("crtfix: VirtualProtect failed %lu", GetLastError());
                    continue;
                }
                g_slot_rw = 1;
                dbg("crtfix: page now RW (was %#lx)", old);
            }
        }
        InterlockedExchangePointer(slot, (void *)&crt_vsprintf_s);
    }
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID r)
{
    if (reason == DLL_PROCESS_ATTACH) {
        wchar_t ev[8];
        InitializeCriticalSection(&g_cs);
        if (GetEnvironmentVariableW(L"RSPROXY_VERBOSE", ev, 8)) g_verbose = 1;
        if (GetEnvironmentVariableW(L"RSPROXY_SANITIZE", ev, 8)) g_sanitize = 1;
        g_cache = (CACHEENT *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, MAXCACHE * sizeof(CACHEENT));
        init_procs();
        CreateThread(NULL, 0, crtfix_thread, NULL, 0, NULL);
    }
    return TRUE;
}
