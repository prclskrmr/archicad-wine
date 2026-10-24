/* Listet die Toplevel-Fenster eines Prozesses (Standard: ARCHICAD.exe) mit Stil, Ex-Stil, Besitzer, Sichtbarkeit.
 * Nur lesend. Bauen: x86_64-w64-mingw32-gcc -O2 -o tools/winlist.exe tools/winlist.c
 * Aufruf (im Prefix, waehrend Archicad laeuft): wine tools/winlist.exe [-c] [prozessname]
 *   -c: statt der Toplevel-Fenster den Baum der Unterfenster des Hauptfensters (DGFrameWindow), offener Dialoge (DGModalDlgClass) und Paletten (DGTopPaletteClass) ausgeben
 *   -l: Listen (DGChildListViewClass) als ListView abfragen: echte Klasse, Ansicht, Anzahl Eintraege, Thread
 */
#include <windows.h>
#include <tlhelp32.h>
#include <stdio.h>
#include <wchar.h>

static DWORD target_pid;

static void flags(DWORD s, DWORD x, char *out)
{
    out[0] = 0;
    if ((s & WS_CAPTION) == WS_CAPTION) strcat(out, "CAPTION ");
    if (s & WS_POPUP) strcat(out, "POPUP ");
    if (s & WS_CHILD) strcat(out, "CHILD ");
    if (s & WS_THICKFRAME) strcat(out, "THICKFRAME ");
    if (s & WS_SYSMENU) strcat(out, "SYSMENU ");
    if (x & WS_EX_TOOLWINDOW) strcat(out, "TOOLWINDOW ");
    if (x & WS_EX_APPWINDOW) strcat(out, "APPWINDOW ");
    if (x & WS_EX_LAYERED) strcat(out, "LAYERED ");
    if (x & WS_EX_TOPMOST) strcat(out, "TOPMOST ");
    if (x & WS_EX_NOACTIVATE) strcat(out, "NOACTIVATE ");
}

static BOOL CALLBACK cb(HWND h, LPARAM lp)
{
    DWORD pid; WCHAR title[128], cls[64]; RECT r; char f[256];
    GetWindowThreadProcessId(h, &pid);
    if (pid != target_pid) return TRUE;
    GetWindowTextW(h, title, 128); GetClassNameW(h, cls, 64); GetWindowRect(h, &r);
    DWORD s = GetWindowLongW(h, GWL_STYLE), x = GetWindowLongW(h, GWL_EXSTYLE);
    if (!title[0] && !IsWindowVisible(h)) return TRUE;   /* unsichtbare Fenster ohne Titel ueberspringen */
    flags(s, x, f);
    wprintf(L"%p %-4ls owner=%p style=%08lx ex=%08lx %5ld,%5ld %4ldx%-4ld cls=%-24ls \"%ls\"\n",
            h, IsWindowVisible(h) ? L"VIS" : L"-", GetWindow(h, GW_OWNER), s, x,
            r.left, r.top, r.right - r.left, r.bottom - r.top, cls, title);
    printf("        %s\n", f);
    return TRUE;
}

/* Unterfenster rekursiv (Option -c): Klasse, Titel, Rechteck (Bildschirm), sichtbar, Stil */
static void children(HWND parent, int depth)
{
    for (HWND h = GetWindow(parent, GW_CHILD); h; h = GetWindow(h, GW_HWNDNEXT))
    {
        WCHAR title[96], cls[64]; RECT r;
        GetWindowTextW(h, title, 96); GetClassNameW(h, cls, 64); GetWindowRect(h, &r);
        wprintf(L"%*ls%p %-3ls %5ld,%5ld %4ldx%-4ld style=%08lx ex=%08lx cls=%ls \"%ls\"\n", depth * 2, L"", h,
                (GetWindowLongW(h, GWL_STYLE) & WS_VISIBLE) ? L"V" : L"-", r.left, r.top, r.right - r.left,
                r.bottom - r.top, GetWindowLongW(h, GWL_STYLE), GetWindowLongW(h, GWL_EXSTYLE), cls, title);
        if (depth < 6) children(h, depth + 1);
    }
}

/* Option -l: Fenster der Klasse DGChildListViewClass als ListView abfragen (nur Nachrichten ohne Zeiger) */
static LRESULT ask(HWND h, UINT msg)
{
    DWORD_PTR res = (DWORD_PTR)-99;
    if (!SendMessageTimeoutW(h, msg, 0, 0, SMTO_ABORTIFHUNG, 1000, &res)) return -99;
    return (LRESULT)res;
}

static BOOL CALLBACK cb_lv(HWND h, LPARAM lp)
{
    WCHAR cls[64], real[64], ptitle[96] = L"";
    GetClassNameW(h, cls, 64);
    if (lstrcmpW(cls, L"DGChildListViewClass")) return TRUE;
    RealGetWindowClassW(h, real, 64);
    HWND pal = h;   /* naechste Palette (Titel) suchen */
    while ((pal = GetParent(pal))) { WCHAR c[64]; GetClassNameW(pal, c, 64); if (wcsstr(c, L"PaletteClass")) { GetWindowTextW(pal, ptitle, 96); break; } }
    RECT r; GetWindowRect(h, &r);
    DWORD s = GetWindowLongW(h, GWL_STYLE);
    wprintf(L"%p tid=%04lx real=%-16ls view=%Id items=%Id lvex=%08Ix style=%08lx%ls %4ldx%-5ld Palette=\"%ls\"\n",
            h, GetWindowThreadProcessId(h, NULL), real,
            ask(h, 0x108F /* LVM_GETVIEW */), ask(h, 0x1004 /* LVM_GETITEMCOUNT */),
            ask(h, 0x1037 /* LVM_GETEXTENDEDLISTVIEWSTYLE */), s,
            (s & 0x400) ? L" OWNERDRAWFIXED" : L"", r.right - r.left, r.bottom - r.top, ptitle);
    return TRUE;
}

static BOOL CALLBACK cb_frame(HWND h, LPARAM lp)
{
    DWORD pid; WCHAR cls[64];
    GetWindowThreadProcessId(h, &pid);
    if (pid != target_pid) return TRUE;
    GetClassNameW(h, cls, 64);
    if (!lstrcmpW(cls, L"DGFrameWindow") || !lstrcmpW(cls, L"DGModalDlgClass") || !lstrcmpW(cls, L"DGTopPaletteClass"))
    {
        wprintf(L"%ls %p\n", !lstrcmpW(cls, L"DGFrameWindow") ? L"Hauptfenster" :
                !lstrcmpW(cls, L"DGTopPaletteClass") ? L"Palette" : L"Dialog", h);
        if (lp) EnumChildWindows(h, cb_lv, 0); else children(h, 1);
    }
    return TRUE;
}

int main(int argc, char **argv)
{
    int child_mode = argc > 1 && !strcmp(argv[1], "-c");
    int lv_mode = argc > 1 && !strcmp(argv[1], "-l");
    if (child_mode || lv_mode) { argc--; argv++; }
    const char *name = argc > 1 ? argv[1] : "ARCHICAD.exe";
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    PROCESSENTRY32 pe = { sizeof(pe) };
    for (BOOL ok = Process32First(snap, &pe); ok; ok = Process32Next(snap, &pe))
        if (!lstrcmpiA(pe.szExeFile, name)) target_pid = pe.th32ProcessID;
    CloseHandle(snap);
    if (!target_pid) { printf("Prozess %s nicht gefunden\n", name); return 1; }
    printf("pid %04lx\n", target_pid);
    if (lv_mode) EnumWindows(cb_frame, 1);
    else EnumWindows(child_mode ? cb_frame : cb, 0);
    return 0;
}
