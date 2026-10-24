/* rgndump.c: gibt die Fensterregion (SetWindowRgn) aller Fenster einer Klasse als Pixelbild aus (nur lesend).
 * Zweck: Form von Archicads Eck-Symbol (DGGrowBoxClass) pruefen.
 * Bauen:  x86_64-w64-mingw32-gcc -O1 -o tools/rgndump.exe tools/rgndump.c -luser32 -lgdi32
 * Aufruf (waehrend Archicad mit offenem Dialog laeuft):
 *   WINEPREFIX=~/.wine-archicad26 WINEDEBUG=-all ~/wine-build-11.19/install/bin/wine tools/rgndump.exe [Klasse]
 * Ausgabe je Fenster: Rechteck, Regionstyp, Rechteckliste der Region und ein Pixelbild ('#' = in der Region). */
#include <windows.h>
#include <stdio.h>

static WCHAR cls_wanted[64] = L"DGGrowBoxClass";
static int found;

static void dump(HWND h)
{
    RECT wr; GetWindowRect(h, &wr);
    int w = wr.right - wr.left, ht = wr.bottom - wr.top;
    HRGN rgn = CreateRectRgn(0, 0, 0, 0);
    int type = GetWindowRgn(h, rgn);
    printf("Fenster %p  Bildschirm (%ld,%ld)-(%ld,%ld) %dx%d  Regionstyp %d (%s)\n", h, wr.left, wr.top, wr.right, wr.bottom,
           w, ht, type, type == ERROR ? "keine Region" : type == NULLREGION ? "leer" : type == SIMPLEREGION ? "Rechteck" : "komplex");
    if (type == SIMPLEREGION || type == COMPLEXREGION)
    {
        DWORD sz = GetRegionData(rgn, 0, NULL);
        RGNDATA *d = HeapAlloc(GetProcessHeap(), 0, sz);
        GetRegionData(rgn, sz, d);
        RECT *r = (RECT *)d->Buffer;
        printf("  %lu Rechtecke:", d->rdh.nCount);
        for (DWORD i = 0; i < d->rdh.nCount; i++) printf(" (%ld,%ld)-(%ld,%ld)", r[i].left, r[i].top, r[i].right, r[i].bottom);
        printf("\n");
        HeapFree(GetProcessHeap(), 0, d);
        for (int y = 0; y < ht; y++)
        {
            printf("  %2d ", y);
            for (int x = 0; x < w; x++) putchar(PtInRegion(rgn, x, y) ? '#' : '.');
            putchar('\n');
        }
    }
    DeleteObject(rgn);
    found++;
}

static BOOL CALLBACK cb_child(HWND h, LPARAM lp)
{
    WCHAR c[64]; GetClassNameW(h, c, 64);
    if (!lstrcmpW(c, cls_wanted)) dump(h);
    return TRUE;
}

static BOOL CALLBACK cb_top(HWND h, LPARAM lp)
{
    cb_child(h, 0);
    EnumChildWindows(h, cb_child, 0);
    return TRUE;
}

int main(int argc, char **argv)
{
    if (argc > 1) MultiByteToWideChar(CP_ACP, 0, argv[1], -1, cls_wanted, 64);
    EnumWindows(cb_top, 0);
    printf("%d Fenster der Klasse %ls gefunden\n", found, cls_wanted);
    return 0;
}
