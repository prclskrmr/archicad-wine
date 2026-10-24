/* dlgpaint.c: stellt Archicads Dialogaufbau nach (trace:win Wand-Grundeinstellung).
 * Ablauf wie im Log:
 *   1. Elternfenster (Dialog) unsichtbar erzeugen
 *   2. Kindfenster alle bei (0,0) erzeugen, sichtbar schalten (SW_SHOWNA), RedrawWindow(INVALIDATE|ERASE|UPDATENOW)
 *   3. Kinder per Begin/Defer/EndDeferWindowPos an ihre Plaetze schieben, flags NOSIZE|NOZORDER|NOACTIVATE (ohne NOCOPYBITS)
 *   4. Dialog zeigen (SW_SHOW), je Kind RedrawWindow(RDW_ALLCHILDREN|RDW_UPDATENOW)
 *   5. Nachrichten abarbeiten
 * Gemessen je Kind: WM_PAINT vor/nach dem Zeigen, Update-Region direkt nach dem Zeigen, letztes rcPaint.
 * Erwartung Windows (Vermutung): nach dem Zeigen bekommt jedes Kind ein WM_PAINT ueber die ganze Flaeche.
 * Nur lesend, aendert nichts am Prefix. Bauen: x86_64-w64-mingw32-gcc -O1 -o tools/dlgpaint.exe tools/dlgpaint.c -luser32 -lgdi32
 * Aufruf: WINEPREFIX=~/.wine-archicad26 WINEDEBUG=-all ~/wine-build-11.19/install/bin/wine tools/dlgpaint.exe [warten_ms] [clip]   (clip: Dialog mit WS_CLIPCHILDREN; Standard ohne, wie Archicad style 94cc0080) */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>

#define N 6
static HWND kids[N];
static int idx(HWND h) { for (int i = 0; i < N; i++) if (kids[i] == h) return i; return -1; }
static int paints_before[N], paints_after[N], phase;
static RECT last_paint[N];
static int seq; static HWND dlg_g;
static void logev(const char *what, HWND h) { int i = h == dlg_g ? -2 : idx(h); if (phase) printf("  #%02d %-14s %s\n", ++seq, what, i == -2 ? "DIALOG" : (i >= 0 ? (const char[]){(char)('0'+i),0} : "?")); }


static LRESULT CALLBACK kid_proc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    if (m == WM_PAINT)
    {
        PAINTSTRUCT ps; WCHAR t[32]; RECT r;
        HDC dc = BeginPaint(h, &ps);
        int i = idx(h);
        logev("WM_PAINT Kind", h);
        if (i >= 0) { if (phase) paints_after[i]++; else paints_before[i]++; last_paint[i] = ps.rcPaint; }
        GetClientRect(h, &r);
        FillRect(dc, &r, (HBRUSH)(COLOR_BTNFACE + 1));
        GetWindowTextW(h, t, 32);
        DrawTextW(dc, t, -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        EndPaint(h, &ps);
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

static LRESULT CALLBACK dlg_proc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    if (m == WM_ERASEBKGND) logev("WM_ERASEBKGND", h);
    if (m == WM_PAINT)
    {
        PAINTSTRUCT ps; RECT r; HDC dc = BeginPaint(h, &ps);
        logev("WM_PAINT", h);
        GetClientRect(h, &r);
        FillRect(dc, &r, (HBRUSH)(COLOR_BTNFACE + 1)); /* malt ohne WS_CLIPCHILDREN auch ueber die Kinder */
        EndPaint(h, &ps);
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

static void pump(DWORD ms)
{
    DWORD end = GetTickCount() + ms; MSG msg;
    while ((LONG)(end - GetTickCount()) > 0)
    {
        while (PeekMessageW(&msg, 0, 0, 0, PM_REMOVE)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
        Sleep(10);
    }
}

int main(int argc, char **argv)
{
    DWORD wait = argc > 1 ? atoi(argv[1]) : 1500;
    WNDCLASSW wc = {0};
    wc.lpfnWndProc = kid_proc; wc.hInstance = GetModuleHandleW(NULL);
    wc.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW); wc.lpszClassName = L"DlgPaintKid";
    RegisterClassW(&wc);
    wc.lpfnWndProc = dlg_proc; wc.lpszClassName = L"DlgPaintDlg"; wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    RegisterClassW(&wc);

    /* 1. unsichtbarer Dialog */
    HWND dlg = CreateWindowExW(WS_EX_DLGMODALFRAME | WS_EX_CONTROLPARENT, L"DlgPaintDlg", L"dlgpaint",
                               WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_CLIPSIBLINGS | WS_THICKFRAME | ((argc > 2) ? WS_CLIPCHILDREN : 0), 200, 200, 420, 260, NULL, NULL, wc.hInstance, NULL);
    dlg_g = dlg;
    /* 2. Kinder bei (0,0), sichtbar, neu zeichnen – ohne WS_CLIPSIBLINGS wie Archicads Knöpfe */
    const WCHAR *names[N] = { L"Abbrechen", L"OK", L"zu Projektursprung", L"Kalksandstein", L"0,365", L"Positionierung" };
    for (int i = 0; i < N; i++)
    {
        kids[i] = CreateWindowExW(0, L"DlgPaintKid", names[i], WS_CHILD, 0, 0, 150, 24, dlg, NULL, wc.hInstance, NULL);
        RedrawWindow(kids[i], NULL, NULL, RDW_INVALIDATE | RDW_ERASE);
        RedrawWindow(kids[i], NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_UPDATENOW);
        ShowWindow(kids[i], SW_SHOWNA);
        RedrawWindow(kids[i], NULL, NULL, RDW_INVALIDATE | RDW_UPDATENOW);
    }
    pump(200);
    /* 3. an ihre Plaetze schieben */
    HDWP dw = BeginDeferWindowPos(N);
    for (int i = 0; i < N; i++)
        dw = DeferWindowPos(dw, kids[i], NULL, 20 + (i % 2) * 200, 20 + (i / 2) * 60, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    EndDeferWindowPos(dw);
    /* 4. Dialog zeigen */
    phase = 1;
    printf("Reihenfolge ab ShowWindow (Dialog %s WS_CLIPCHILDREN):\n", argc > 2 ? "mit" : "ohne");
    ShowWindow(dlg, SW_SHOW);
    printf("Update-Region direkt nach ShowWindow (vor dem Zeichnen):\n");
    for (int i = 0; i < N; i++)
    {
        RECT u; BOOL has = GetUpdateRect(kids[i], &u, FALSE);
        printf("  %-20ls %s (%ld,%ld)-(%ld,%ld)\n", names[i], has ? "ungueltig" : "GUELTIG  ", u.left, u.top, u.right, u.bottom);
    }
    for (int i = 0; i < N; i++) RedrawWindow(kids[i], NULL, NULL, RDW_ALLCHILDREN | RDW_UPDATENOW);
    /* 5. abarbeiten */
    pump(wait);
    printf("\nWM_PAINT je Kind (vor dem Zeigen / nach dem Zeigen), letztes rcPaint:\n");
    for (int i = 0; i < N; i++)
        printf("  %-20ls %d / %d   (%ld,%ld)-(%ld,%ld)\n", names[i], paints_before[i], paints_after[i],
               last_paint[i].left, last_paint[i].top, last_paint[i].right, last_paint[i].bottom);
    DestroyWindow(dlg);
    return 0;
}
