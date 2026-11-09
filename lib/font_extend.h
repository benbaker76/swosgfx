/*
 * font_extend.h -- draw the extended glyphs (full ASCII punctuation, accented
 * capitals, $, the euro, OE) into the free cells of a CHARSET bank.
 *
 * A port of the C# SwosGfx's -font-extend (SwosFont.cs).
 * The glyph shapes come from glyphs.txt (assets/glyphs.txt: blocks
 * of "@<char> small|big" followed by 8 rows of '*' = ink, '-' = blank); the
 * 1-pixel down-right shadow is added here. Glyphs take the free 16x8 cells of
 * the sheet in a fixed order -- the small font in rows 0-13, the big font in
 * rows 14-31 -- which is the placement the intro script's font-extension part
 * (amiga_scripts.c) cuts them from (slot 1220 on). Every cell of rows 0-27 left empty then gets the
 * stock "missing glyph" box, as the Amiga charset has.
 *
 * Works on any CHARSET bank (the PC-sourced one from Sg_DosToAmigaBanks, or the
 * Amiga's own); the palette is not touched: the font draws in index 2 (ink)
 * and 8 (shadow) in both.
 */
#ifndef SWOSGFX_FONT_EXTEND_H
#define SWOSGFX_FONT_EXTEND_H

#include <stdint.h>

#include "sg_image.h"
#include "sg_log.h"

/* Extend `charset` in place from the glyphs file at `glyphsTxtPath`, with the
 * missing-glyph boxes. 0 on success; on failure `err` (256 bytes) says why. */
int32_t Sg_FontExtend(SgImage *charset, const char *glyphsTxtPath, char *err);

/* Where each new sprite went: SwosFont's Entry, one per glyph per font, small
 * font first, sprite slots 1220 on. */
typedef struct {
    int32_t  idx;           /* sprite slot */
    int32_t  big;           /* 0 small font, 1 big */
    uint32_t ch;            /* the character (Unicode) */
    uint32_t cp;            /* its conversionTable code (OE 0x8C, euro 0x9D, Y diaeresis 0x9E) */
    int32_t  x, y, w, h;    /* the cut in the sheet */
} SgFontEntry;

typedef struct {
    int32_t      glyphs;    /* characters drawn (each in both fonts) */
    int32_t      count;     /* entries: 2 x glyphs */
    SgFontEntry *entries;
} SgFontLayout;

/* Sg_FontExtend with -no-boxes (fillMissing 0) and the layout: `layout`, if not
 * NULL, is filled in (Sg_FontLayoutFree it, also after a failure). */
int32_t Sg_FontExtendEx(SgImage *charset, const char *glyphsTxtPath, int32_t fillMissing,
                        SgFontLayout *layout, char *err);
void    Sg_FontLayoutFree(SgFontLayout *layout);

/* integration.txt: the image-script lines and conversionTable wiring for a
 * layout, as SwosFont.EmitIntegration writes it (UTF-8, '\n' lines). *text is
 * malloc'd, *size its length. 0 on success. */
int32_t Sg_FontIntegrationText(const SgFontLayout *layout, char **text, uint32_t *size, char *err);

/* SwosGfx -font-extend [-glyphs=G] [-no-boxes] in.bmp [out.bmp]: read the
 * CHARSET bmp `inBmp`, draw the glyphs of `glyphsPath` (NULL: "glyphs.txt"),
 * write `outBmp` (NULL: over inBmp) with its palette kept, and with
 * emitIntegration write integration.txt into the glyphs file's folder (where
 * SwosGfx puts it). SwosGfx's two lines go to `log` (may be NULL):
 *   Font: added N glyphs x2 fonts = M sprites -> OUT.bmp[ (+ missing-glyph boxes)]
 *   Font: wrote <absolute glyphs folder>/integration.txt (image-script + conversionTable wiring)
 * 0 on success; on failure `err` says why. */
int32_t Sg_FontExtendRun(const char *inBmp, const char *outBmp, const char *glyphsPath,
                         int32_t fillMissing, int32_t emitIntegration, SgLogFn log, void *user, char *err);

/* SwosGfx -dos -font-extend [-glyphs=G] [-font-base=N] [outDir]: the same glyphs
 * for the PC game, which keeps its fonts as sprites (CHARSET.DAT 0-56 small,
 * 57-113 big, both full). Writes into outDir (NULL: "."):
 *   FONTX.DAT  the glyphs as records in the PC game's sprite format, the small
 *              font's then the big font's, their ordinals numbered from `base`
 *   FONTX.txt  which record and sprite each character is, its conversion code
 *   FONTX.bmp  a preview: the small font above, the big below
 * Loading them and mapping characters to them is the exe's change (FONTX.txt
 * says how the PC game finds a character's sprite). 0 on success. */
int32_t Sg_FontExtendDos(const char *glyphsPath, const char *outDir, int32_t base, SgLogFn log, void *user,
                         char *err);

#endif
