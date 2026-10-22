/* Load every RT_GROUP_ICON in a PE module with LoadImageW and report how many
 * fail or come back blank (all-transparent).  Used to diagnose missing tool
 * icons in Archicad under Wine. */
#include <windows.h>
#include <stdio.h>

static HMODULE g_mod;
static int g_total, g_loadfail, g_blank, g_ok;
static char g_lastname[128];

static void sample_alpha(HICON ic, const wchar_t *name)
{
    ICONINFO ii;
    if (!GetIconInfo(ic, &ii)) { return; }
    BITMAP bm;
    if (ii.hbmColor && GetObject(ii.hbmColor, sizeof bm, &bm)) {
        /* count non-zero pixels in the colour bitmap */
        BITMAPINFO bi;
        ZeroMemory(&bi, sizeof bi);
        bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bi.bmiHeader.biWidth = bm.bmWidth;
        bi.bmiHeader.biHeight = -bm.bmHeight;   /* top-down */
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        bi.bmiHeader.biCompression = BI_RGB;
        int px = bm.bmWidth * bm.bmHeight;
        unsigned int *buf = HeapAlloc(GetProcessHeap(), 0, px * 4);
        HDC dc = CreateCompatibleDC(NULL);
        if (buf && GetDIBits(dc, ii.hbmColor, 0, bm.bmHeight, buf, &bi, DIB_RGB_COLORS)) {
            long nonzero = 0;
            for (int i = 0; i < px; i++) if (buf[i] & 0xFFFFFF) nonzero++;
            if (nonzero == 0) {
                g_blank++;
                printf("BLANK %ls (%dx%d)\n", name, bm.bmWidth, bm.bmHeight);
            } else {
                g_ok++;
            }
        }
        if (buf) HeapFree(GetProcessHeap(), 0, buf);
        DeleteDC(dc);
    }
    if (ii.hbmColor) DeleteObject(ii.hbmColor);
    if (ii.hbmMask) DeleteObject(ii.hbmMask);
}

static BOOL CALLBACK enum_proc(HMODULE mod, LPCWSTR type, LPWSTR name, LONG_PTR p)
{
    (void)mod; (void)type; (void)p;
    g_total++;
    HICON ic = (HICON)LoadImageW(g_mod, name, IMAGE_ICON, 0, 0, LR_DEFAULTCOLOR);
    if (!ic) {
        g_loadfail++;
        if (g_loadfail <= 15) printf("LOADFAIL %ls err=%lu\n", name, GetLastError());
        return TRUE;
    }
    lstrcpynW((wchar_t*)g_lastname, name, 120);
    sample_alpha(ic, name);
    DestroyIcon(ic);
    return TRUE;
}

int wmain(int argc, wchar_t **argv)
{
    if (argc < 2) { printf("usage: iconprobe <module.dll>\n"); return 2; }
    g_mod = LoadLibraryExW(argv[1], NULL, LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE);
    if (!g_mod) g_mod = LoadLibraryExW(argv[1], NULL, LOAD_LIBRARY_AS_DATAFILE);
    if (!g_mod) { printf("cannot load %ls err=%lu\n", argv[1], GetLastError()); return 1; }
    EnumResourceNamesW(g_mod, RT_GROUP_ICON, enum_proc, 0);
    printf("\n== %ls: group icons=%d loadfail=%d blank=%d ok=%d\n",
           argv[1], g_total, g_loadfail, g_blank, g_ok);
    return 0;
}
