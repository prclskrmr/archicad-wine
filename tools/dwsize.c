/* dwsize.c: prueft Wines DirectWrite bei gebrochenen Schriftgroessen (Stempeltexte).
 * Vergleicht GetAlphaTextureBounds einer Glyphe "9" in Arial:
 *   a) emSize = 0.27 mit Matrix 83.54 (wie Archicad)  -> effektiv 22.56 px
 *   b) emSize = 22.56 ohne Matrix                       -> Referenz
 *   c) emSize = 2.74 mit Matrix 8.235                  -> effektiv 22.56 px
 * Unter Windows sind a, b, c gleich gross. Nur lesend, kein Zugriff auf den Prefix ausser der Schriftliste.
 * Bauen: x86_64-w64-mingw32-gcc -O1 -o tools/dwsize.exe tools/dwsize.c -ldwrite
 * Aufruf: source ./env-wine-custom.sh; WINEPREFIX=~/.wine-archicad26 WINEDEBUG=-all "$WINE_BIN" tools/dwsize.exe */
#define COBJMACROS
#include <windows.h>
#include <dwrite_3.h>
#include <stdio.h>

/* mingw liefert die IID nicht in libuuid */
static const IID IID_DWF = {0xb859ee5a,0xd838,0x4b5b,{0xa2,0xe8,0x1a,0xdc,0x7d,0x93,0xdb,0x48}};

static void test(IDWriteFactory *f, IDWriteFontFace *face, UINT16 glyph, float em, float scale)
{
    DWRITE_GLYPH_RUN run = {0};
    DWRITE_MATRIX m = { scale, 0, 0, scale, 0, 0 };
    IDWriteGlyphRunAnalysis *a;
    FLOAT adv = 0;
    RECT r;
    HRESULT hr;

    run.fontFace = face;
    run.fontEmSize = em;
    run.glyphCount = 1;
    run.glyphIndices = &glyph;
    run.glyphAdvances = &adv;
    hr = IDWriteFactory_CreateGlyphRunAnalysis(f, &run, 1.0f, scale == 1.0f ? NULL : &m,
            DWRITE_RENDERING_MODE_NATURAL, DWRITE_MEASURING_MODE_NATURAL, 0, 0, &a);
    if (FAILED(hr)) { printf("emSize %6.2f x %6.3f: CreateGlyphRunAnalysis hr=%08lx\n", em, scale, hr); return; }
    hr = IDWriteGlyphRunAnalysis_GetAlphaTextureBounds(a, DWRITE_TEXTURE_CLEARTYPE_3x1, &r);
    printf("emSize %6.2f x Matrix %6.3f = %6.2f px: Bounds %ld x %ld (hr=%08lx)\n", em, scale, em * scale,
           r.right - r.left, r.bottom - r.top, hr);
    IDWriteGlyphRunAnalysis_Release(a);
}

int main(void)
{
    IDWriteFactory *f;
    IDWriteFontCollection *coll;
    IDWriteFontFamily *fam;
    IDWriteFont *font;
    IDWriteFontFace *face;
    UINT32 idx, cp = '9';
    BOOL exists;
    UINT16 glyph;

    if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, &IID_DWF, (IUnknown **)&f))) return 1;
    IDWriteFactory_GetSystemFontCollection(f, &coll, FALSE);
    IDWriteFontCollection_FindFamilyName(coll, L"Arial", &idx, &exists);
    if (!exists) { printf("Arial fehlt\n"); return 1; }
    IDWriteFontCollection_GetFontFamily(coll, idx, &fam);
    IDWriteFontFamily_GetFirstMatchingFont(fam, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STRETCH_NORMAL, DWRITE_FONT_STYLE_NORMAL, &font);
    IDWriteFont_CreateFontFace(font, &face);
    IDWriteFontFace_GetGlyphIndices(face, &cp, 1, &glyph);
    printf("Arial, Glyphe '9' = Index %u\n", glyph);
    test(f, face, glyph, 22.56f, 1.0f);
    test(f, face, glyph, 0.27f, 83.54f);
    test(f, face, glyph, 2.74f, 8.235f);
    test(f, face, glyph, 1.00f, 22.56f);
    return 0;
}
