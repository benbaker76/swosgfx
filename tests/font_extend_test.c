/*
 * font_extend_test.c -- -font-extend (Sg_FontExtendRun) against the C#
 * SwosGfx's output for the same CHARSET and glyphs.
 *
 *   font_extend_test                     the PC output (-dos -font-extend) of the
 *       shipped assets/glyphs.txt: every FONTX.DAT record read back by the PC
 *       sprite reader, its header as the stock font's and its pixels the same
 *       glyph the Amiga path draws into a sheet (needs no game data)
 *   font_extend_test WORKDIR [REFDIR] [noboxes] [inplace]
 *       WORKDIR holds CHARSET.bmp and glyphs.txt. Writes WORKDIR/OUT.bmp (or,
 *       with "inplace", over CHARSET.bmp) and WORKDIR/integration.txt. With
 *       REFDIR (the same folder after SwosGfx -font-extend
 *       -glyphs=REFDIR/glyphs.txt [-no-boxes] REFDIR/CHARSET.bmp [REFDIR/OUT.bmp])
 *       every file must match.
 */
#include "conv_compare.h"
#include "dos_sprite.h"
#include "font_extend.h"

#include <stdlib.h>

#ifndef SWOSGFX_SOURCE_DIR
#define SWOSGFX_SOURCE_DIR "."
#endif

static void DosCheck(void)
{
    char err[256] = "", glyphs[1024], dir[1024] = "font_extend_test.tmp";
    SgImage sheet;
    SgFontLayout l;
    SgDosDat d;

    Sg_PathJoin(glyphs, sizeof glyphs, SWOSGFX_SOURCE_DIR, "assets/glyphs.txt");
    CHECK(Sg_FontExtendDos(glyphs, dir, 1334, PrintLine, NULL, err) == 0, "Sg_FontExtendDos: %s", err);
    /* the Amiga path's cells: the same glyphs, small then big, in the same order */
    CHECK(SgImage_Init(&sheet, 320, 256) == 0, "image");
    CHECK(Sg_FontExtendEx(&sheet, glyphs, 0, &l, err) == 0, "Sg_FontExtendEx: %s", err);
    CHECK(Sg_DosDatLoad(&d, dir, "FONTX.DAT", l.count, err) == 0, "FONTX.DAT: %s", err);
    CHECK(d.buf.size > 0, "FONTX.DAT is empty");
    {
        uint32_t used = 0;
        for (int32_t k = 0; k < d.count; k++)
            used += 24 + d.spr[k].size;
        CHECK(used == d.buf.size, "FONTX.DAT: %u bytes, its %d records take %u", (unsigned)d.buf.size,
              (int)d.count, (unsigned)used);
    }
    for (int32_t k = 0; k < l.count && k < d.count; k++) {
        const SgFontEntry *e = &l.entries[k];
        const SgDosSprite *s = &d.spr[k];
        const int32_t h = e->big ? 8 : 6;
        uint8_t px[8 * 64];
        CHECK(s->width == e->w && s->height == h && s->wquads == (e->w + 15) / 16 && s->centerX == 0 &&
              s->centerY == 0 && s->unk4 == ((e->big ? 0 : 0x80) | (s->wquads * 2)) && s->heightDiv4 == h / 4 &&
              s->ordinal == 1334 + k,
              "record %d ('%c' %s): header %dx%d wq %d unk4 %d div4 %d ordinal %d", (int)k, (char)e->ch,
              e->big ? "big" : "small", s->width, s->height, s->wquads, s->unk4, s->heightDiv4, s->ordinal);
        CHECK(Sg_DosSpriteDecode(s, px) == 0, "record %d does not decode", (int)k);
        for (int32_t y = 0; y < h; y++)
            for (int32_t x = 0; x < e->w; x++) {
                const uint8_t want = y < e->h ? sheet.px[(e->y + y) * sheet.w + e->x + x] : 0;
                CHECK(px[y * e->w + x] == want, "record %d ('%c' %s) pixel %d,%d: %d, the sheet's %d", (int)k,
                      (char)e->ch, e->big ? "big" : "small", (int)x, (int)y, px[y * e->w + x], want);
            }
    }
    CHECK(Sg_FontExtendDos("no/such/glyphs.txt", dir, 1334, NULL, NULL, err) != 0 &&
          !strcmp(err, "glyphs file not found: no/such/glyphs.txt"), "missing glyphs: %s", err);
    printf("font_extend_test: the PC output, %d records\n", (int)d.count);
    Sg_DosDatFree(&d);
    Sg_FontLayoutFree(&l);
    SgImage_Free(&sheet);
}

int main(int argc, char **argv)
{
    char err[256] = "", in[1024], out[1024], glyphs[1024];
    int32_t boxes = 1, inplace = 0;

    if (argc < 2) {
        DosCheck();
        printf("font_extend_test: %d checks, %d failures (give WORKDIR [REFDIR] [noboxes] [inplace] for the "
               "Amiga sheet)\n", (int)gChecks, (int)gFailures);
        return gFailures ? 1 : 0;
    }
    for (int32_t i = 3; i < argc; i++) {
        if (!strcmp(argv[i], "noboxes"))
            boxes = 0;
        else if (!strcmp(argv[i], "inplace"))
            inplace = 1;
    }
    Sg_PathJoin(in, sizeof in, argv[1], "CHARSET.bmp");
    Sg_PathJoin(out, sizeof out, argv[1], "OUT.bmp");
    Sg_PathJoin(glyphs, sizeof glyphs, argv[1], "glyphs.txt");
    CHECK(Sg_FontExtendRun(in, inplace ? NULL : out, glyphs, boxes, 1, PrintLine, NULL, err) == 0,
          "Sg_FontExtendRun: %s", err);
    CHECK(Sg_FontExtendRun(out, NULL, "no/such/glyphs.txt", 1, 1, NULL, NULL, err) != 0 &&
          !strcmp(err, "glyphs file not found: no/such/glyphs.txt"), "missing glyphs: %s", err);
    if (argc > 2 && argv[2][0])
        CompareFolders(argv[2], argv[1]);
    printf("font_extend_test: %d checks, %d failures\n", (int)gChecks, (int)gFailures);
    return gFailures ? 1 : 0;
}
