/*
 * dos_sprite_io.c -- the PC game's sprites as sprNNNN.bmp files, and back.
 *
 * Ported from SwosGfx DosSprite.cs (SaveSpriteToBmp, InsertSpriteFromBmp,
 * InsertAllSpritesFromDirectory, SaveChangesToSprites, BuildDatFile, and its
 * Bitmap8Helper.Read8bppBmpTopDown) and DosSpriteCodec.cs (UnchainSprite,
 * encode_quad, put_bits_step). The rcl chain is emulated bit for bit, carry
 * included, as the decoder in dos_sprite.c does.
 *
 * What an import writes, and why a plain export -> import is not a no-op:
 * SwosGfx rebuilds every container one of whose sprites it re-imported, and
 * a rebuilt record is the one SwosGfx lays out, not the one on the disk --
 * spr_data recomputed, size/dat_file/changed/unk1 zeroed, nlines_div4 set to
 * height / 4, the pixels past the width zeroed, wquads the fewest that hold
 * the width, and bytes after the last record dropped. The PC game's own files
 * differ from that in places, so they come back different, exactly as they do
 * from SwosGfx. SPRITE.DAT is rewritten from the new sizes.
 */
#include "dos_sprite_io.h"
#include "sg_image.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RECORD_SIZE      24
#define LINE_BUFFER_SIZE (256 - 8)   /* the PC tool's temp line: a longer one is refused */

/* ---- export ------------------------------------------------------------ */

int32_t Sg_DosSpriteExportBmp(const SgDosSprites *s, int32_t index, uint8_t bgIndex,
                              const char *path, char *err)
{
    const SgDosSprite *spr;
    SgImage img;
    int32_t width, height, isRS, rc;
    uint32_t pal[256];

    if (index < 0 || index >= SG_DOS_NUM_SPRITES || !s->spr[index]) {
        snprintf(err, 256, "no sprite %d", (int)index);
        return -1;
    }
    spr = s->spr[index];
    width = spr->width;
    height = spr->height;
    if (width <= 0 || height <= 0)
        return 1;

    if (SgImage_Init(&img, width, height) != 0) {
        snprintf(err, 256, "out of memory for sprite %d", (int)index);
        return -1;
    }
    /* Decode gives 0..15 with 0 transparent; the BMP's transparent is 16. */
    if (Sg_DosSpriteDecode(spr, img.px) != 0) {
        snprintf(err, 256, "sprite %d: its %dx%d does not fit its data (wquads %d)",
                 (int)index, (int)width, (int)height, (int)spr->wquads);
        SgImage_Free(&img);
        return -1;
    }
    for (int32_t i = 0; i < width * height; i++)
        if (img.px[i] == 0)
            img.px[i] = 16;

    /* The big R/S sprites are drawn with the game palette's 0x70 row; the
     * rest with the menu palette's first 16. Slot 16 is the background colour
     * and 17..255 the menu palette, only to fill the table. */
    isRS = index > 1208 && index < 1273;
    for (int32_t i = 0; i < 16; i++)
        pal[i] = isRS ? Sg_DosSpritePalGame[i | 0x70] : Sg_DosSpritePalMenu[i];
    pal[16] = Sg_DosSpritePalMenu[bgIndex];
    for (int32_t i = 17; i < 256; i++)
        pal[i] = Sg_DosSpritePalMenu[i];
    SgImage_SetPaletteArgb(&img, pal, 256);

    rc = SgImage_WriteBmp(&img, path, err);
    SgImage_Free(&img);
    return rc;
}

int32_t Sg_DosSpritesExport(const char *dosDir, const char *outDir, uint8_t bgIndex, char *err)
{
    SgDosSprites s;
    char path[1024], name[16];
    int32_t rc = 0;

    if (Sg_DosSpritesLoad(&s, dosDir, err) != 0)
        return -1;
    if (outDir[0] && Sg_MakeDirs(outDir) != 0) {
        snprintf(err, 256, "cannot create the folder %.200s", outDir);
        Sg_DosSpritesFree(&s);
        return -1;
    }
    for (int32_t i = 0; i < SG_DOS_NUM_SPRITES; i++) {
        snprintf(name, sizeof name, "spr%04d.bmp", (int)i);
        Sg_PathJoin(path, sizeof path, outDir, name);
        /* SwosGfx stops at the first sprite it cannot decode (it throws). */
        if (Sg_DosSpriteExportBmp(&s, i, bgIndex, path, err) < 0) {
            rc = -1;
            break;
        }
    }
    Sg_DosSpritesFree(&s);
    return rc;
}

/* ---- UnchainSprite: chunky 4bpp -> the chained layout ------------------ */

static void Rcl8(uint8_t *v, int32_t *cf)
{
    int32_t out = (*v >> 7) & 1;
    *v = (uint8_t)((*v << 1) | (*cf & 1));
    *cf = out;
}

/* rcl al,1; rcl bl,1; rcl al,1; rcl bh,1; rcl al,1; rcl cl,1; rcl al,1; rcl ch,1 */
static void PutBitsStep(uint8_t *al, uint8_t *bl, uint8_t *bh, uint8_t *cl, uint8_t *ch, int32_t *cf)
{
    Rcl8(al, cf); Rcl8(bl, cf);
    Rcl8(al, cf); Rcl8(bh, cf);
    Rcl8(al, cf); Rcl8(cl, cf);
    Rcl8(al, cf); Rcl8(ch, cf);
}

/* 8 chunky bytes (16 pixels) -> 8 bytes of chain data. The carry runs on
 * from the first half into the second, as in the original. */
static void EncodeQuad(const uint8_t *src, uint8_t chain[8])
{
    uint8_t bl = 0, bh = 0, cl = 0, ch = 0, al;
    int32_t cf = 0;

    for (int32_t i = 0; i < 4; i++) {
        al = src[i];
        PutBitsStep(&al, &bl, &bh, &cl, &ch, &cf);
        PutBitsStep(&al, &bl, &bh, &cl, &ch, &cf);
    }
    chain[0] = ch; chain[2] = cl; chain[4] = bh; chain[6] = bl;

    for (int32_t i = 4; i < 8; i++) {
        al = src[i];
        PutBitsStep(&al, &bl, &bh, &cl, &ch, &cf);
        PutBitsStep(&al, &bl, &bh, &cl, &ch, &cf);
    }
    chain[1] = ch; chain[3] = cl; chain[5] = bh; chain[7] = bl;
}

/* In place, line by line; bytesPerLine (wQuads * 8) <= LINE_BUFFER_SIZE. */
static void UnchainSprite(uint8_t *data, int32_t nLines, int32_t wQuads)
{
    int32_t bytesPerLine = wQuads << 3, units4 = wQuads << 1;
    uint8_t line[LINE_BUFFER_SIZE], chain[8];

    for (int32_t y = 0; y < nLines; y++) {
        uint8_t *dst = data + y * bytesPerLine;
        memcpy(line, dst, (size_t)bytesPerLine);
        for (int32_t q = 0; q < wQuads; q++) {
            uint8_t *p = dst + q * 2;
            EncodeQuad(line + q * 8, chain);
            /* Four little-endian words, one per plane, units4 bytes apart. */
            p[0]          = chain[0]; p[1]              = chain[1];
            p[units4]     = chain[2]; p[units4 + 1]     = chain[3];
            p[units4 * 2] = chain[4]; p[units4 * 2 + 1] = chain[5];
            p[units4 * 3] = chain[6]; p[units4 * 3 + 1] = chain[7];
        }
    }
}

/* new_width_aligned = (width + 15) & ~15; size = aligned * height / 2: a zeroed
 * buffer filled two pixels a byte, high nibble first (the odd pixel past the
 * width transparent), then chained -- as SwosGfx's InsertSpriteFromBmp. */
int32_t Sg_DosSpriteEncode(const uint8_t *px, int32_t width, int32_t height, uint8_t **data,
                           uint32_t *size, int16_t *wquads, char *err)
{
    int32_t aligned = (width + 15) & ~15, bytesPerLine;
    uint32_t n;
    uint8_t *d;

    *data = NULL;
    *size = 0;
    if (width <= 0 || height <= 0 || height > 32767 || aligned / 2 > LINE_BUFFER_SIZE) {
        snprintf(err, 256, "a %dx%d sprite cannot be encoded (at most %d wide)", (int)width, (int)height,
                 LINE_BUFFER_SIZE * 2);
        return -1;
    }
    n = (uint32_t)aligned * (uint32_t)height / 2;
    d = calloc(n, 1);
    if (!d) {
        snprintf(err, 256, "out of memory encoding a sprite");
        return -1;
    }
    *wquads = (int16_t)(aligned / 16);
    bytesPerLine = *wquads * 8;
    for (int32_t y = 0; y < height; y++) {
        const uint8_t *src = px + (size_t)y * width;
        uint8_t *dst = d + y * bytesPerLine;
        for (int32_t x = 0; x < width; x += 2) {
            uint8_t c1 = (uint8_t)(src[x] & 0x0F);
            uint8_t c2 = (uint8_t)(x + 1 < width ? src[x + 1] & 0x0F : 0);
            dst[x >> 1] = (uint8_t)((c1 << 4) | c2);
        }
    }
    UnchainSprite(d, height, *wquads);
    *data = d;
    *size = n;
    return 0;
}

/* ---- import ------------------------------------------------------------ */

/* A sprite as the import edits it: the header fields, and the pixels -- the
 * loaded record's, or a new buffer once its BMP was read. */
typedef struct {
    SgDosSprite hdr;
    uint8_t    *owned;       /* NULL: hdr.data is the loaded record's */
} EditSprite;

typedef struct {
    SgDosSprites src;
    EditSprite   spr[SG_DOS_NUM_SPRITES];
    int32_t      datChanged[SG_DOS_NUM_DAT_FILES];
    int32_t      datIndex[SG_DOS_NUM_SPRITES];
} Editor;

static void EditorFree(Editor *e)
{
    for (int32_t i = 0; i < SG_DOS_NUM_SPRITES; i++)
        free(e->spr[i].owned);
    Sg_DosSpritesFree(&e->src);
}

/* Bitmap8Helper.Read8bppBmpTopDown, check for check: 8 bits, no compression,
 * one plane, bottom-up only (a top-down BMP is refused). The palette is not
 * read: an index is a colour. *px is malloc'd, width*height, top row first.
 * A width of 0 reads as an empty picture (the import then skips the sprite). */
static int32_t ReadSpriteBmp(const char *path, uint8_t **px, int32_t *width, int32_t *height, char *err)
{
    SgBuf b;
    const uint8_t *d;
    uint32_t pixelOffset, infoSize;
    int32_t w, h;
    uint16_t planes, bitCount;
    uint32_t compression;
    uint64_t rowSize;

    *px = NULL;
    if (Sg_ReadFile(path, &b, err) != 0)
        return -1;
    d = b.data;
    if (b.size < 2 || d[0] != 'B' || d[1] != 'M') {
        snprintf(err, 256, "%.200s is not a BMP", path);
        goto fail;
    }
    if (b.size < 14 + 40) {
        snprintf(err, 256, "%.200s: the BMP header is cut short", path);
        goto fail;
    }
    pixelOffset = Sg_Le32(d + 10);
    infoSize    = Sg_Le32(d + 14);
    w           = (int32_t)Sg_Le32(d + 18);
    h           = (int32_t)Sg_Le32(d + 22);
    planes      = Sg_Le16(d + 26);
    bitCount    = Sg_Le16(d + 28);
    compression = Sg_Le32(d + 30);
    if (infoSize < 40) {
        snprintf(err, 256, "%.200s: unsupported BMP info header size", path);
        goto fail;
    }
    if (bitCount != 8) {
        snprintf(err, 256, "%.200s: only 8bpp BMPs are supported (it is %u bpp)", path, (unsigned)bitCount);
        goto fail;
    }
    if (compression != 0) {
        snprintf(err, 256, "%.200s: compressed BMPs are not supported", path);
        goto fail;
    }
    if (planes != 1) {
        snprintf(err, 256, "%.200s: invalid planes count in BMP (expected 1)", path);
        goto fail;
    }
    if (h <= 0) {
        snprintf(err, 256, "%.200s: top-down BMPs (negative height) are not supported", path);
        goto fail;
    }
    if (w < 0) {
        snprintf(err, 256, "%.200s: negative BMP width", path);
        goto fail;
    }
    /* Not SwosGfx's checks, but where it would fail later: a line of more
     * than LINE_BUFFER_SIZE bytes (UnchainSprite throws), and a height the
     * 16-bit header field cannot hold (SwosGfx would truncate it). */
    if ((((int64_t)w + 15) & ~15) / 2 > LINE_BUFFER_SIZE) {
        snprintf(err, 256, "%.200s: %d pixels wide, a sprite is at most %d", path, (int)w, LINE_BUFFER_SIZE * 2);
        goto fail;
    }
    if (h > 32767) {
        snprintf(err, 256, "%.200s: %d lines high, a sprite is at most 32767", path, (int)h);
        goto fail;
    }
    rowSize = (((uint64_t)w + 3) / 4) * 4;
    if ((uint64_t)pixelOffset + rowSize * (uint64_t)h > b.size) {
        snprintf(err, 256, "%.200s: unexpected end of BMP file reading pixel data", path);
        goto fail;
    }
    *px = malloc((size_t)w * (size_t)h + 1);
    if (!*px) {
        snprintf(err, 256, "out of memory reading %.200s", path);
        goto fail;
    }
    for (int32_t row = 0; row < h; row++)
        memcpy(*px + (size_t)(h - 1 - row) * (size_t)w, d + pixelOffset + (uint64_t)row * rowSize, (size_t)w);
    *width = w;
    *height = h;
    Sg_BufFree(&b);
    return 0;

fail:
    Sg_BufFree(&b);
    return -1;
}

/* InsertSpriteFromBmp. 0 on success (also when the picture is empty and the
 * sprite is left as it was), -1 on failure. */
static int32_t InsertSpriteFromBmp(Editor *e, int32_t index, const char *path, char *err)
{
    EditSprite *spr = &e->spr[index];
    uint8_t *pixels, *data;
    int32_t width, height;
    int16_t wquads;
    uint32_t newSize;

    if (ReadSpriteBmp(path, &pixels, &width, &height, err) != 0)
        return -1;
    if (width <= 0 || height <= 0) {
        free(pixels);
        return 0;
    }
    /* 16 (transparent) -> 0, any other index keeps its low four bits. */
    for (int32_t i = 0; i < width * height; i++)
        pixels[i] = (uint8_t)(pixels[i] == 16 ? 0 : (pixels[i] & 0x0F));
    if (Sg_DosSpriteEncode(pixels, width, height, &data, &newSize, &wquads, err) != 0) {
        free(pixels);
        return -1;
    }
    free(pixels);

    spr->hdr.wquads     = wquads;
    spr->hdr.width      = (int16_t)width;
    spr->hdr.height     = (int16_t)height;
    spr->hdr.heightDiv4 = (uint8_t)(height / 4);
    free(spr->owned);
    spr->owned = data;
    spr->hdr.data = data;
    spr->hdr.size = newSize;
    e->datChanged[e->datIndex[index]] = 1;
    return 0;
}

static void Put16(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void Put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

/* A container: the records laid out one after the other, spr_data = record
 * offset + dataOfs + 24, the run-time fields zeroed (SwosGfx's BuildDatFile). */
int32_t Sg_DosDatBuild(const SgDosSprite *const *spr, int32_t count, uint32_t dataOfs, SgBuf *out, char *err)
{
    uint32_t total = 0, p = 0;

    for (int32_t j = 0; j < count; j++)
        total += RECORD_SIZE + spr[j]->size;
    out->data = malloc(total ? total : 1);
    out->size = total;
    if (!out->data) {
        snprintf(err, 256, "out of memory building a container");
        return -1;
    }
    for (int32_t j = 0; j < count; j++) {
        const SgDosSprite *s = spr[j];
        uint8_t *h = out->data + p;
        Put32(h + 0, p + dataOfs + RECORD_SIZE);
        Put16(h + 4, 0);                 /* size */
        Put16(h + 6, 0);                 /* dat_file */
        h[8] = 0;                        /* changed */
        h[9] = 0;                        /* unk1 */
        Put16(h + 10, (uint16_t)s->width);
        Put16(h + 12, (uint16_t)s->height);
        Put16(h + 14, (uint16_t)s->wquads);
        Put16(h + 16, (uint16_t)s->centerX);
        Put16(h + 18, (uint16_t)s->centerY);
        h[20] = s->unk4;
        h[21] = s->heightDiv4;
        Put16(h + 22, (uint16_t)s->ordinal);
        if (s->size)
            memcpy(h + RECORD_SIZE, s->data, s->size);
        p += RECORD_SIZE + s->size;
    }
    return 0;
}

/* BuildDatFile for a changed container. */
static int32_t BuildDatFile(const Editor *e, int32_t di, uint32_t dataOfs, SgBuf *out, char *err)
{
    const SgDosDatSpec *spec = &Sg_DosDatSpecs[di];
    const SgDosSprite *spr[SG_DOS_NUM_SPRITES];

    for (int32_t j = 0; j < spec->entries; j++)
        spr[j] = &e->spr[spec->start + j].hdr;
    if (Sg_DosDatBuild(spr, spec->entries, dataOfs, out, err) != 0) {
        snprintf(err, 256, "out of memory building %s", spec->name);
        return -1;
    }
    return 0;
}

/* SaveChangesToSprites. */
static int32_t SaveChanges(Editor *e, const char *dosDir, char *err)
{
    SgBuf built[SG_DOS_NUM_DAT_FILES];
    uint8_t index[SG_DOS_SPRITE_DAT_SIZE];
    char path[1024];
    uint32_t ofs, running = 0;
    int32_t rc = -1;

    memset(built, 0, sizeof built);

    /* SPRITE.DAT: each sprite's offset within its stream -- CHARSET.DAT on
     * its own, then every other container as one run (GOAL1.DAT's two copies
     * included) -- and a terminating 0. */
    ofs = 0;
    for (int32_t i = 0; i < Sg_DosDatSpecs[0].entries; i++) {
        Put32(index + i * 4, ofs);
        ofs += e->spr[i].hdr.size + RECORD_SIZE;
    }
    ofs = 0;
    for (int32_t di = 1; di < SG_DOS_NUM_DAT_FILES; di++) {
        const SgDosDatSpec *spec = &Sg_DosDatSpecs[di];
        for (int32_t j = 0; j < spec->entries; j++) {
            Put32(index + (spec->start + j) * 4, ofs);
            ofs += e->spr[spec->start + j].hdr.size + RECORD_SIZE;
        }
    }
    Put32(index + SG_DOS_NUM_SPRITES * 4, 0);

    /* CHARSET.DAT and containers 1..5 are built with a data offset of 0;
     * BENCH.DAT, the last, with the sum of 1..5's sizes -- as built, or as
     * read from the disk (trailing bytes and all) for one left unchanged. */
    for (int32_t di = 0; di < SG_DOS_NUM_DAT_FILES; di++) {
        uint32_t dataOfs = di == SG_DOS_NUM_DAT_FILES - 1 ? running : 0;
        uint32_t size;
        if (e->datChanged[di]) {
            if (BuildDatFile(e, di, dataOfs, &built[di], err) != 0)
                goto done;
            size = built[di].size;
        } else {
            size = e->src.dat[di].buf.size;
        }
        if (di >= 1 && di < SG_DOS_NUM_DAT_FILES - 1)
            running += size;
    }

    /* In order, so of GOAL1.DAT's two copies the second one written wins. */
    for (int32_t di = 0; di < SG_DOS_NUM_DAT_FILES; di++) {
        if (!e->datChanged[di])
            continue;
        if (Sg_FindNoCase(dosDir, Sg_DosDatSpecs[di].name, path, sizeof path) != 0) {
            snprintf(err, 256, "%s not found in %.200s", Sg_DosDatSpecs[di].name, dosDir);
            goto done;
        }
        if (Sg_WriteFile(path, built[di].data, built[di].size, err) != 0)
            goto done;
    }
    if (Sg_FindNoCase(dosDir, "SPRITE.DAT", path, sizeof path) != 0) {
        snprintf(err, 256, "SPRITE.DAT not found in %.200s", dosDir);
        goto done;
    }
    if (Sg_WriteFile(path, index, sizeof index, err) != 0)
        goto done;
    rc = 0;

done:
    for (int32_t di = 0; di < SG_DOS_NUM_DAT_FILES; di++)
        Sg_BufFree(&built[di]);
    return rc;
}

int32_t Sg_DosSpritesImport(const char *dosDir, const char *spritesDir, char *err)
{
    Editor *e = calloc(1, sizeof *e);
    char path[1024], name[16];
    int32_t found = 0, rc = -1;

    if (!e) {
        snprintf(err, 256, "out of memory");
        return -1;
    }
    if (Sg_DosSpritesLoad(&e->src, dosDir, err) != 0) {
        free(e);
        return -1;
    }
    for (int32_t di = 0; di < SG_DOS_NUM_DAT_FILES; di++)
        for (int32_t j = 0; j < Sg_DosDatSpecs[di].entries; j++)
            e->datIndex[Sg_DosDatSpecs[di].start + j] = di;
    for (int32_t i = 0; i < SG_DOS_NUM_SPRITES; i++)
        e->spr[i].hdr = *e->src.spr[i];

    /* SwosGfx lists spr????.bmp (Windows: any case) and takes the names whose
     * four characters are digits and number a sprite; asking for each
     * sprNNNN.bmp by name finds the same set. */
    for (int32_t i = 0; i < SG_DOS_NUM_SPRITES; i++) {
        snprintf(name, sizeof name, "spr%04d.bmp", (int)i);
        if (Sg_FindNoCase(spritesDir, name, path, sizeof path) != 0)
            continue;
        found++;
        if (InsertSpriteFromBmp(e, i, path, err) != 0)
            goto done;
    }
    /* SwosGfx returns quietly here, writing nothing; say why nothing was. */
    if (!found) {
        snprintf(err, 256, "no sprNNNN.bmp files in %.200s", spritesDir);
        goto done;
    }
    {
        int32_t any = 0;
        for (int32_t di = 0; di < SG_DOS_NUM_DAT_FILES; di++)
            any |= e->datChanged[di];
        rc = any ? SaveChanges(e, dosDir, err) : 0;
    }

done:
    EditorFree(e);
    free(e);
    return rc;
}
