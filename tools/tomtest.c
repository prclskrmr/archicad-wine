/* tomtest.c: stellt Archicads Text-Eingabefeld nach (trace:richedit):
 * RichEdit20W, ENM_REQUESTRESIZE, TOM: Range(0,0).MoveEnd(tomCharFormat) -> Font SetSize(15.6)/SetName(Arial),
 * Auswahl-Font SetSize(9)/SetBold/SetName(Arial), Text tippen (EM_REPLACESEL), EM_REQUESTRESIZE.
 * Ausgabe: CHARFORMAT des Textes und der Auswahl (Name, yHeight in Twips, fett), TOM-Rueckgabewerte, EN_REQUESTRESIZE-Rechteck,
 * zum Vergleich die Zeilenhoehe von Arial 15,6 pt per GetTextMetrics. Nur lesend.
 * Bauen: x86_64-w64-mingw32-gcc -O1 -o tools/tomtest.exe tools/tomtest.c -luser32 -lgdi32 -lole32 -loleaut32 -luuid
 * Aufruf: WINEPREFIX=~/.wine-archicad26 WINEDEBUG=-all ~/wine-build-11.19/install/bin/wine tools/tomtest.exe */
#define COBJMACROS
#include <windows.h>
#include <richedit.h>
#include <richole.h>
#include <tom.h>
#include <stdio.h>

static const GUID my_IID_ITextDocument = {0x8cc497c0,0xa1df,0x11ce,{0x80,0x98,0x00,0xaa,0x00,0x47,0xbe,0x5d}};
static RECT resize_rc; static int resize_count;

static LRESULT CALLBACK parent_proc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    if (m == WM_NOTIFY && ((NMHDR *)l)->code == EN_REQUESTRESIZE)
    {
        resize_rc = ((REQRESIZE *)l)->rc; resize_count++;
    }
    return DefWindowProcW(h, m, w, l);
}

static void dump_fmt(HWND re, const char *what, WPARAM scf)
{
    CHARFORMAT2W cf = { sizeof(cf) };
    SendMessageW(re, EM_GETCHARFORMAT, scf, (LPARAM)&cf);
    printf("%-22s Name=%-12ls yHeight=%ld Twips (%.2f pt) fett=%s mask=%08lx\n", what, cf.szFaceName, cf.yHeight,
           cf.yHeight / 20.0, (cf.dwEffects & CFE_BOLD) ? "ja" : "nein", cf.dwMask);
}

static void probe(ITextDocument *doc, const char *what)
{
    ITextRange *r; ITextFont *f; BSTR name = NULL; float size = 0; LONG bold = 0;
    ITextDocument_Range(doc, 0, 1, &r); ITextRange_GetFont(r, &f);
    ITextFont_GetName(f, &name); ITextFont_GetSize(f, &size); ITextFont_GetBold(f, &bold);
    printf("  [%-28s] Zeichen 0..1: Name=%ls Size=%.2f Bold=%ld\n", what, name ? name : L"(null)", size, bold);
    SysFreeString(name); ITextFont_Release(f); ITextRange_Release(r);
}

int main(void)
{
    LoadLibraryA("riched20.dll");
    WNDCLASSW wc = {0}; wc.lpfnWndProc = parent_proc; wc.lpszClassName = L"TomTestParent"; RegisterClassW(&wc);
    HWND parent = CreateWindowExW(0, L"TomTestParent", L"tomtest", WS_OVERLAPPEDWINDOW, 100, 100, 400, 200, 0, 0, 0, 0);
    HWND re = CreateWindowExW(0, L"RichEdit20W", L"", WS_CHILD | WS_VISIBLE | ES_MULTILINE, 10, 10, 300, 6, parent, (HMENU)1, 0, 0);
    SendMessageW(re, EM_SETTEXTMODE, TM_RICHTEXT | TM_MULTILEVELUNDO | TM_MULTICODEPAGE, 0);
    SendMessageW(re, EM_SETEVENTMASK, 0, ENM_REQUESTRESIZE | ENM_CHANGE | ENM_SELCHANGE);

    IRichEditOle *ole = NULL; ITextDocument *doc = NULL;
    SendMessageW(re, EM_GETOLEINTERFACE, 0, (LPARAM)&ole);
    if (!ole || FAILED(ole->lpVtbl->QueryInterface(ole, &my_IID_ITextDocument, (void **)&doc))) { printf("kein ITextDocument\n"); return 1; }

    ITextRange *rng; ITextFont *font; LONG delta = 0; HRESULT hr;
    probe(doc, "Start");
    ITextDocument_Range(doc, 0, 0, &rng);
    hr = ITextRange_MoveEnd(rng, tomCharFormat, 1, &delta);
    LONG s, e; ITextRange_GetStart(rng, &s); ITextRange_GetEnd(rng, &e);
    printf("MoveEnd(tomCharFormat,1): hr=%08lx delta=%ld Bereich=%ld..%ld\n", hr, delta, s, e);
    ITextRange_GetFont(rng, &font);
    printf("Range-Font: SetSize(15.6) hr=%08lx  ", ITextFont_SetSize(font, 15.6f));
    BSTR arial = SysAllocString(L"Arial");
    printf("SetName(Arial) hr=%08lx\n", ITextFont_SetName(font, arial));
    probe(doc, "nach Range-Font");

    ITextSelection *sel; ITextFont *sfont;
    ITextDocument_GetSelection(doc, &sel);
    ITextSelection_GetFont(sel, &sfont);
    printf("Auswahl-Font: SetSize(9) hr=%08lx SetBold hr=%08lx SetName hr=%08lx\n", ITextFont_SetSize(sfont, 9.0f),
           ITextFont_SetBold(sfont, tomTrue), ITextFont_SetName(sfont, arial));

    probe(doc, "nach Auswahl-Font");
    dump_fmt(re, "vor Tippen, Auswahl:", SCF_SELECTION);
    dump_fmt(re, "Standard:", SCF_DEFAULT);
    SendMessageW(re, EM_REPLACESEL, TRUE, (LPARAM)L"uksljfuuk");
    SendMessageW(re, EM_SETSEL, 0, 9);
    dump_fmt(re, "getippter Text:", SCF_SELECTION);
    SendMessageW(re, EM_SETSEL, 9, 9);
    SendMessageW(re, EM_REQUESTRESIZE, 0, 0);
    printf("EN_REQUESTRESIZE (%d x): rc=(%ld,%ld)-(%ld,%ld) Hoehe=%ld px\n", resize_count, resize_rc.left, resize_rc.top,
           resize_rc.right, resize_rc.bottom, resize_rc.bottom - resize_rc.top);

    HDC dc = GetDC(re); TEXTMETRICW tm;
    HFONT f = CreateFontW(-MulDiv(156, GetDeviceCaps(dc, LOGPIXELSY), 720), 0, 0, 0, FW_BOLD, 0, 0, 0, 0, 0, 0, 0, 0, L"Arial");
    HGDIOBJ old = SelectObject(dc, f); GetTextMetricsW(dc, &tm); SelectObject(dc, old);
    printf("Vergleich Arial 15,6 pt fett: tmHeight=%ld px (LOGPIXELSY=%d)\n", tm.tmHeight, GetDeviceCaps(dc, LOGPIXELSY));
    ReleaseDC(re, dc);
    return 0;
}
