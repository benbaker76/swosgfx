/*
 * font_extend.c -- the extended glyphs drawn into a CHARSET bank. A port of
 * SwosGfx SwosFont.Extend; see font_extend.h.
 *
 * glyphs.txt, as SwosFont reads it, line by line (UTF-8, BOM allowed):
 *   "@<c> <font>"   starts a block for character c (the first character after
 *                   '@'; <font> is what follows the LAST space: small or big)
 *   blank or "#..." ends the block in hand; outside a block it is a comment
 *   anything else   a row of the block ('*' or '#' = ink, '+' = shadow, else blank)
 * A later block for the same character and font replaces an earlier one.
 */
#include "font_extend.h"
#include "dos2amiga.h"
#include "dos_sprite_io.h"
#include "sg_dir.h"
#include "sg_util.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WHITE          2      /* ink */
#define SHADE          8      /* drop shadow */
#define SPLIT_ROW      14     /* the big font starts at row 14 (y = 112) */
#define FONT_EXT_START 1220   /* first font-extension sprite slot */
#define BOX_FILL_ROWS  28     /* rows 0-27 get the missing-glyph box */
#define BIG_OFFSET     57     /* bigCharsTable spriteIndexOffset */

#define MAX_BLOCKS     512
#define MAX_KEEP_ROWS  8      /* Finalize uses 6 (small) or 8 (big) rows */
#define MAX_COLS       64

enum { FONT_SMALL, FONT_BIG, FONT_OTHER };

/* The stock Amiga charset's "missing glyph" marker (from CHARSET.RAW). */
static const uint8_t kSmallBox[6][6] = {
    { 2, 2, 2, 2, 2, 0 },
    { 2, 0, 2, 0, 2, 8 },
    { 2, 2, 0, 2, 2, 8 },
    { 2, 0, 2, 0, 2, 8 },
    { 2, 2, 2, 2, 2, 8 },
    { 0, 8, 8, 8, 8, 8 },
};
static const uint8_t kBigBox[8][8] = {
    { 2, 2, 2, 2, 2, 2, 2, 0 },
    { 2, 0, 2, 2, 2, 0, 2, 8 },
    { 2, 2, 0, 2, 0, 2, 2, 8 },
    { 2, 2, 2, 0, 2, 2, 2, 8 },
    { 2, 2, 0, 2, 0, 2, 2, 8 },
    { 2, 0, 2, 2, 2, 0, 2, 8 },
    { 2, 2, 2, 2, 2, 2, 2, 8 },
    { 0, 8, 8, 8, 8, 8, 8, 8 },
};

/* The glyph order (swos_font.py's): punctuation, inverted ! and ?, $, euro, OE,
 * then the accented capitals. Glyphs in the file but not here follow, in file order. */
static const uint32_t kOrder[] = {
    '!', '"', '#', '&', '<', '=', '>', '@', '[', '\\', ']', '^', '_', '`', '{', '|', '}', '~',
    0x00A1, 0x00BF, '$', 0x20AC, 0x0152,
    0x00C0, 0x00C1, 0x00C2, 0x00C7, 0x00C9, 0x00C8, 0x00CA, 0x00CB, 0x00CC, 0x00CD, 0x00CE, 0x00CF,
    0x00D1, 0x00D2, 0x00D3, 0x00D4, 0x00D9, 0x00DA, 0x00DB, 0x0178,
};
#define ORDER_COUNT ((int32_t)(sizeof kOrder / sizeof kOrder[0]))

typedef struct {
    uint32_t cp;
    int32_t  font;
    int32_t  nrows;                        /* rows read (only the first 8 kept) */
    int32_t  len[MAX_KEEP_ROWS];           /* characters in each kept row */
    uint8_t  cell[MAX_KEEP_ROWS][MAX_COLS];
} Block;

typedef struct {
    Block    *blocks;
    int32_t   nblocks;
    uint32_t  fileOrder[MAX_BLOCKS];
    int32_t   norder;
} Glyphs;

/* A finished glyph bitmap: h rows of w. */
typedef struct {
    int32_t w, h;
    uint8_t px[8][MAX_COLS];
} Art;

/* ---- glyphs.txt ----------------------------------------------------------- */

/* Decode one UTF-8 character at *p (advancing it); a bad byte reads as itself. */
static uint32_t Utf8Next(const uint8_t **p, const uint8_t *end)
{
    const uint8_t *s = *p;
    uint32_t c = *s++;
    int32_t more = 0;

    if (c >= 0xF0 && c < 0xF8)      { c &= 0x07; more = 3; }
    else if (c >= 0xE0)             { c &= 0x0F; more = 2; }
    else if (c >= 0xC0)             { c &= 0x1F; more = 1; }
    for (; more > 0; more--) {
        if (s >= end || (*s & 0xC0) != 0x80) {
            c = **p;                    /* not valid UTF-8: take the byte */
            s = *p + 1;
            break;
        }
        c = (c << 6) | (*s++ & 0x3F);
    }
    *p = s;
    return c;
}

static int32_t IsSpace(uint32_t c)
{
    return c == ' ' || c == '\t' || c == '\v' || c == '\f' || c == 0xA0 || c == 0x85;
}

static uint8_t CellOf(uint32_t ch)
{
    return (ch == '#' || ch == '*') ? WHITE : (ch == '+' ? SHADE : 0);
}

static Block *FindBlock(Glyphs *g, uint32_t cp, int32_t font)
{
    for (int32_t i = 0; i < g->nblocks; i++)
        if (g->blocks[i].cp == cp && g->blocks[i].font == font)
            return &g->blocks[i];
    return NULL;
}

/* Store the block in hand (replacing an earlier one with the same key). */
static int32_t CommitBlock(Glyphs *g, const Block *b)
{
    Block *old;

    if (b->font == FONT_OTHER)
        return 0;                       /* a font SwosFont never asks for */
    old = FindBlock(g, b->cp, b->font);
    if (old) {
        *old = *b;
        return 0;
    }
    if (g->nblocks == MAX_BLOCKS)
        return -1;
    g->blocks[g->nblocks++] = *b;
    return 0;
}

static int32_t ReadGlyphs(const char *path, Glyphs *g, char *err)
{
    SgBuf f;
    const uint8_t *p, *end;
    Block cur;
    int32_t have = 0;

    if (Sg_ReadFile(path, &f, err) != 0)
        return -1;
    g->blocks = calloc(MAX_BLOCKS, sizeof *g->blocks);
    if (!g->blocks) {
        Sg_BufFree(&f);
        snprintf(err, 256, "out of memory");
        return -1;
    }
    p = f.data;
    end = f.data + f.size;
    if (end - p >= 3 && p[0] == 0xEF && p[1] == 0xBB && p[2] == 0xBF)
        p += 3;

    while (p < end) {
        uint32_t line[256];
        int32_t n = 0, blank = 1;

        /* one line, split on \n, \r\n or \r */
        while (p < end && *p != '\n' && *p != '\r') {
            uint32_t c = Utf8Next(&p, end);
            if (n < (int32_t)(sizeof line / sizeof line[0]))
                line[n++] = c;
        }
        if (p < end && *p == '\r') {
            p++;
            if (p < end && *p == '\n')
                p++;
        } else if (p < end) {
            p++;                        /* '\n' */
        }

        for (int32_t i = 0; i < n; i++)
            if (!IsSpace(line[i])) { blank = 0; break; }

        if (n > 0 && line[0] == '@') {
            int32_t sp = -1;
            if (have && CommitBlock(g, &cur) != 0)
                goto full;
            have = 0;
            /* s = line[1..]; sp = last space in s */
            for (int32_t i = n - 1; i >= 1; i--)
                if (line[i] == ' ') { sp = i - 1; break; }
            if (sp <= 0)
                continue;
            memset(&cur, 0, sizeof cur);
            cur.cp = line[1];
            {
                int32_t fl = n - (sp + 2);
                const uint32_t *fn = line + sp + 2;
                if (fl == 5 && fn[0] == 's' && fn[1] == 'm' && fn[2] == 'a' && fn[3] == 'l' && fn[4] == 'l')
                    cur.font = FONT_SMALL;
                else if (fl == 3 && fn[0] == 'b' && fn[1] == 'i' && fn[2] == 'g')
                    cur.font = FONT_BIG;
                else
                    cur.font = FONT_OTHER;
            }
            have = 1;
            {
                int32_t known = 0;
                for (int32_t i = 0; i < g->norder; i++)
                    if (g->fileOrder[i] == cur.cp) { known = 1; break; }
                if (!known && g->norder < MAX_BLOCKS)
                    g->fileOrder[g->norder++] = cur.cp;
            }
        } else if (blank || line[0] == '#') {
            if (have && CommitBlock(g, &cur) != 0)
                goto full;
            have = 0;
        } else if (have) {
            if (cur.nrows < MAX_KEEP_ROWS) {
                int32_t r = cur.nrows;
                if (n > MAX_COLS) {
                    snprintf(err, 256, "%s: a glyph row is longer than %d", path, MAX_COLS);
                    Sg_BufFree(&f);
                    return -1;
                }
                cur.len[r] = n;
                for (int32_t i = 0; i < n; i++)
                    cur.cell[r][i] = CellOf(line[i]);
            }
            cur.nrows++;
        }
    }
    if (have && CommitBlock(g, &cur) != 0)
        goto full;
    Sg_BufFree(&f);
    return 0;

full:
    snprintf(err, 256, "%s: more than %d glyph blocks", path, MAX_BLOCKS);
    Sg_BufFree(&f);
    return -1;
}

/* Finalize: keep the font's height (6 small, 8 big), add the shadow, trim to
 * the glyph's natural width (at least 1, never wider than its rows). */
static void Finalize(const Block *b, int32_t font, Art *a)
{
    int32_t hRows = font == FONT_SMALL ? 6 : 8;
    int32_t h = b->nrows < hRows ? b->nrows : hRows;
    int32_t w = 0, width = 1;
    uint8_t src[8][MAX_COLS];

    memset(a, 0, sizeof *a);
    memset(src, 0, sizeof src);
    if (h == 0)
        h = 1;                          /* an empty row */
    for (int32_t r = 0; r < h && r < b->nrows; r++) {
        if (b->len[r] > w)
            w = b->len[r];
        memcpy(src[r], b->cell[r], (size_t)b->len[r]);
    }
    /* FillShadow: 1px down-right under each ink pixel, inside the art */
    memcpy(a->px, src, sizeof src);
    for (int32_t r = 0; r < h; r++)
        for (int32_t c = 0; c < w; c++)
            if (src[r][c] == WHITE && r + 1 < h && c + 1 < w && a->px[r + 1][c + 1] == 0)
                a->px[r + 1][c + 1] = SHADE;
    for (int32_t r = 0; r < h; r++)
        for (int32_t c = 0; c < w; c++)
            if (a->px[r][c] != 0 && c + 1 > width)
                width = c + 1;
    a->h = h;
    a->w = width < w ? width : w;
}

/* ---- the sheet ------------------------------------------------------------ */

/* Cells (16x8) of the stock charset: the intro script's glyph cuts between its
 * leading bank load and the first font-extension slot. occ[row][col]. */
static void CoreCells(uint8_t occ[32][16])
{
    int32_t n, i = 0;
    const uint16_t *s = Sg_ScriptWords(SG_SCRIPT_INTRO, &n);
    uint8_t seen[FONT_EXT_START];
    int32_t cx[FONT_EXT_START], cy[FONT_EXT_START];

    memset(occ, 0, 32 * 16);
    memset(seen, 0, sizeof seen);
    while (i < n && ((s[i] & 0xF000) == 0x7000 || (s[i] & 0xF000) == 0x8000))
        i += 3;
    while (i + 2 < n && s[i] != 0xFFFF) {
        int32_t op = s[i], spr = op & 0x0FFF;
        if ((op & 0xF000) != 0xC000 || spr >= FONT_EXT_START)
            break;
        seen[spr] = 1;                  /* a repeated slot: the last cut wins */
        cx[spr] = s[i + 1] >> 8;
        cy[spr] = s[i + 1] & 0xFF;
        i += 3;
    }
    for (int32_t k = 0; k < FONT_EXT_START; k++) {
        if (!seen[k])
            continue;
        if (cy[k] / 8 < 32 && cx[k] / 16 < 16)
            occ[cy[k] / 8][cx[k] / 16] = 1;
    }
}

static int32_t FreeRows(uint8_t occ[32][16], int32_t lo, int32_t hi, int32_t *rows)
{
    int32_t n = 0;
    for (int32_t r = lo; r < hi; r++) {
        int32_t free_ = 1;
        for (int32_t c = 0; c < 16; c++)
            if (occ[r][c]) { free_ = 0; break; }
        if (free_)
            rows[n++] = r;
    }
    return n;
}

static void SetPx(SgImage *s, int32_t x, int32_t y, uint8_t v)
{
    if (x >= 0 && y >= 0 && x < s->w && y < s->h)
        s->px[y * s->w + x] = v;
}

/* Code points past 0xFF go to spare conversionTable bytes (SwosFont.CpOverride). */
static uint32_t CpOf(uint32_t ch)
{
    switch (ch) {
    case 0x0152: return 0x8C;           /* OE ligature */
    case 0x0178: return 0x9E;           /* Y diaeresis */
    case 0x20AC: return 0x9D;           /* euro */
    default:     return ch;
    }
}

static int32_t Lay(SgImage *sheet, const Art *arts, const uint32_t *chars, int32_t count,
                   const int32_t *rows, int32_t nrows, int32_t big, SgFontLayout *layout, char *err)
{
    int32_t ri = 0, col = 0;
    const char *font = big ? "big" : "small";

    for (int32_t g = 0; g < count; g++) {
        const Art *a = &arts[g];
        int32_t x, y;

        if (col >= 16) { col = 0; ri++; }
        if (ri >= nrows) {
            snprintf(err, 256, "not enough free %s rows in the charset for %d glyphs", font, (int)count);
            return -1;
        }
        x = col * 16;
        y = rows[ri] * 8;
        for (int32_t cc = 0; cc < 16; cc++)        /* clear the cell, then draw */
            for (int32_t rr = 0; rr < 8; rr++)
                SetPx(sheet, x + cc, y + rr, 0);
        for (int32_t rr = 0; rr < a->h; rr++)
            for (int32_t cc = 0; cc < a->w; cc++)
                if (a->px[rr][cc] != 0)
                    SetPx(sheet, x + cc, y + rr, a->px[rr][cc]);
        if (layout) {
            SgFontEntry *e = &layout->entries[layout->count];
            e->idx = FONT_EXT_START + layout->count;
            e->big = big;
            e->ch = chars[g];
            e->cp = CpOf(chars[g]);
            e->x = x;
            e->y = y;
            e->w = a->w;
            e->h = a->h;
            layout->count++;
        }
        col++;
    }
    return 0;
}

static void FillMissingBoxes(SgImage *sheet)
{
    for (int32_t row = 0; row < BOX_FILL_ROWS; row++) {
        int32_t big = row >= SPLIT_ROW, bs = big ? 8 : 6;
        for (int32_t col = 0; col < 16; col++) {
            int32_t x = col * 16, y = row * 8, empty = 1;
            if (x + 16 > sheet->w || y + 8 > sheet->h)
                continue;
            for (int32_t rr = 0; rr < 8 && empty; rr++)
                for (int32_t cc = 0; cc < 16; cc++)
                    if (sheet->px[(y + rr) * sheet->w + x + cc] != 0) { empty = 0; break; }
            if (!empty)
                continue;
            for (int32_t rr = 0; rr < bs; rr++)
                for (int32_t cc = 0; cc < bs; cc++) {
                    uint8_t v = big ? kBigBox[rr][cc] : kSmallBox[rr][cc];
                    if (v)
                        sheet->px[(y + rr) * sheet->w + x + cc] = v;
                }
        }
    }
}

int32_t Sg_FontExtend(SgImage *charset, const char *glyphsTxtPath, char *err)
{
    return Sg_FontExtendEx(charset, glyphsTxtPath, 1, NULL, err);
}

void Sg_FontLayoutFree(SgFontLayout *layout)
{
    free(layout->entries);
    memset(layout, 0, sizeof *layout);
}

/* The glyphs file read and shaped: seq[nseq] the characters in their order (the
 * fixed order, then the file's others; each one has both fonts), small[] and
 * big[] their finished art. *small / *big malloc'd (free both, also on failure). */
static int32_t BuildArts(const char *glyphsTxtPath, uint32_t *seq, int32_t *nseqOut, Art **smallOut,
                         Art **bigOut, char *err)
{
    Glyphs g;
    int32_t nseq = 0, rc = -1;
    Art *small = NULL, *big = NULL;

    memset(&g, 0, sizeof g);
    *smallOut = *bigOut = NULL;
    *nseqOut = 0;
    if (ReadGlyphs(glyphsTxtPath, &g, err) != 0)
        goto out;
    for (int32_t i = 0; i < ORDER_COUNT; i++)
        if (FindBlock(&g, kOrder[i], FONT_SMALL) && FindBlock(&g, kOrder[i], FONT_BIG))
            seq[nseq++] = kOrder[i];
    for (int32_t i = 0; i < g.norder; i++) {
        uint32_t ch = g.fileOrder[i];
        int32_t skip = 0;
        for (int32_t k = 0; k < ORDER_COUNT && !skip; k++)
            if (kOrder[k] == ch) skip = 1;
        for (int32_t k = 0; k < nseq && !skip; k++)
            if (seq[k] == ch) skip = 1;
        if (!skip && FindBlock(&g, ch, FONT_SMALL) && FindBlock(&g, ch, FONT_BIG))
            seq[nseq++] = ch;
    }
    small = calloc((size_t)(nseq ? nseq : 1), sizeof *small);
    big = calloc((size_t)(nseq ? nseq : 1), sizeof *big);
    if (!small || !big) {
        snprintf(err, 256, "out of memory");
        goto out;
    }
    for (int32_t i = 0; i < nseq; i++) {
        Finalize(FindBlock(&g, seq[i], FONT_SMALL), FONT_SMALL, &small[i]);
        Finalize(FindBlock(&g, seq[i], FONT_BIG), FONT_BIG, &big[i]);
    }
    *nseqOut = nseq;
    rc = 0;
out:
    free(g.blocks);
    *smallOut = small;
    *bigOut = big;
    return rc;
}

int32_t Sg_FontExtendEx(SgImage *charset, const char *glyphsTxtPath, int32_t fillMissing,
                        SgFontLayout *layout, char *err)
{
    uint32_t seq[MAX_BLOCKS];
    int32_t nseq = 0, rc = -1;
    Art *small = NULL, *big = NULL;
    uint8_t occ[32][16];
    int32_t smallRows[32], bigRows[32], nSmall, nBig;

    if (layout)
        memset(layout, 0, sizeof *layout);
    if (BuildArts(glyphsTxtPath, seq, &nseq, &small, &big, err) != 0)
        goto out;

    CoreCells(occ);
    nSmall = FreeRows(occ, 0, SPLIT_ROW, smallRows);
    nBig = FreeRows(occ, SPLIT_ROW, 32, bigRows);

    if (layout) {
        layout->entries = calloc((size_t)(nseq ? nseq * 2 : 1), sizeof *layout->entries);
        if (!layout->entries) {
            snprintf(err, 256, "out of memory");
            goto out;
        }
        layout->glyphs = nseq;
    }
    if (Lay(charset, small, seq, nseq, smallRows, nSmall, 0, layout, err) != 0 ||
        Lay(charset, big, seq, nseq, bigRows, nBig, 1, layout, err) != 0)
        goto out;
    if (fillMissing)
        FillMissingBoxes(charset);
    rc = 0;

out:
    if (rc != 0 && layout)
        Sg_FontLayoutFree(layout);
    free(small);
    free(big);
    return rc;
}

/* ---- integration.txt -------------------------------------------------------- */

typedef struct {
    char    *s;
    uint32_t len, cap;
    int32_t  oom;
} Out;

static void Put(Out *o, const char *fmt, ...)
{
    va_list ap;
    int32_t n;

    if (o->oom)
        return;
    for (;;) {
        uint32_t room = o->cap - o->len;
        va_start(ap, fmt);
        n = (int32_t)vsnprintf(o->s ? o->s + o->len : NULL, o->s ? room : 0, fmt, ap);
        va_end(ap);
        if (n < 0) {
            o->oom = 1;
            return;
        }
        if (o->s && (uint32_t)n < room) {
            o->len += (uint32_t)n;
            return;
        }
        {
            uint32_t cap = o->cap ? o->cap * 2 : 4096;
            char *p;
            while (cap < o->len + (uint32_t)n + 1)
                cap *= 2;
            p = realloc(o->s, cap);
            if (!p) {
                o->oom = 1;
                return;
            }
            o->s = p;
            o->cap = cap;
        }
    }
}

/* A character as UTF-8 (File.WriteAllText's encoding), 1..4 bytes + NUL. */
static const char *Utf8(uint32_t c, char buf[5])
{
    if (c < 0x80) {
        buf[0] = (char)c;
        buf[1] = 0;
    } else if (c < 0x800) {
        buf[0] = (char)(0xC0 | (c >> 6));
        buf[1] = (char)(0x80 | (c & 0x3F));
        buf[2] = 0;
    } else if (c < 0x10000) {
        buf[0] = (char)(0xE0 | (c >> 12));
        buf[1] = (char)(0x80 | ((c >> 6) & 0x3F));
        buf[2] = (char)(0x80 | (c & 0x3F));
        buf[3] = 0;
    } else {
        buf[0] = (char)(0xF0 | (c >> 18));
        buf[1] = (char)(0x80 | ((c >> 12) & 0x3F));
        buf[2] = (char)(0x80 | ((c >> 6) & 0x3F));
        buf[3] = (char)(0x80 | (c & 0x3F));
        buf[4] = 0;
    }
    return buf;
}

/* The sprite the last entry of a font with code point cp has (the dictionary
 * SwosFont fills in entry order), or -1. */
static int32_t FindCp(const SgFontLayout *l, int32_t big, uint32_t cp)
{
    int32_t found = -1;
    for (int32_t i = 0; i < l->count; i++)
        if (l->entries[i].big == big && l->entries[i].cp == cp)
            found = l->entries[i].idx;
    return found;
}

/* The stock sheet's accented capitals (A, O, U umlaut): sprite in both fonts. */
static int32_t ExistingOf(uint32_t cp)
{
    switch (cp) {
    case 0xC4: return 53;
    case 0xD6: return 55;
    case 0xDC: return 54;
    default:   return -1;
    }
}

int32_t Sg_FontIntegrationText(const SgFontLayout *l, char **text, uint32_t *size, char *err)
{
    static const uint32_t kExisting[] = { 0xC4, 0xD6, 0xDC };
    Out o;
    uint32_t *cps;
    int32_t ncps = 0;
    char u[5];

    memset(&o, 0, sizeof o);
    *text = NULL;
    *size = 0;
    cps = malloc(((size_t)l->count + 3) * sizeof *cps);
    if (!cps) {
        snprintf(err, 256, "out of memory");
        return -1;
    }

    Put(&o, "# image-script entries to append (before 0xFFFF):\n");
    for (int32_t i = 0; i < l->count; i++) {
        const SgFontEntry *e = &l->entries[i];
        Put(&o, "0x%04X, 0x%04X, 0x%04X,   ", (unsigned)(0xC000 | e->idx), (unsigned)((e->x << 8) | e->y),
            (unsigned)((e->w << 8) | e->h));
        Put(&o, "# '%s' %s  sprite %d @ (%d,%d) %dx%d\n", Utf8(e->ch, u), e->big ? "big" : "small",
            (int)e->idx, (int)e->x, (int)e->y, (int)e->w, (int)e->h);
    }

    /* The small font's code points and the stock three, in order, once each. */
    for (int32_t i = 0; i < l->count + 3; i++) {
        uint32_t cp = i < l->count ? l->entries[i].cp : kExisting[i - l->count];
        int32_t have = 0, at;
        if (i < l->count && l->entries[i].big)
            continue;
        for (int32_t k = 0; k < ncps && !have; k++)
            have = cps[k] == cp;
        if (have)
            continue;
        for (at = ncps; at > 0 && cps[at - 1] > cp; at--)
            cps[at] = cps[at - 1];
        cps[at] = cp;
        ncps++;
    }
    Put(&o, "\n# conversionTable updates (idx = codepoint-0x20):\n");
    for (int32_t i = 0; i < ncps; i++) {
        uint32_t cp = cps[i];
        int32_t sm = FindCp(l, 0, cp), bg = FindCp(l, 1, cp);
        char bigConv[16];
        if (sm < 0)
            sm = ExistingOf(cp);
        if (bg >= 0)
            snprintf(bigConv, sizeof bigConv, "%d", (int)(bg - BIG_OFFSET));
        else if (ExistingOf(cp) >= 0)
            snprintf(bigConv, sizeof bigConv, "%d", (int)ExistingOf(cp));
        else
            snprintf(bigConv, sizeof bigConv, "None");
        Put(&o, "small.conv[0x%02X]=%-4d big.conv[0x%02X]=%s   # U+%04X '%s'\n", (unsigned)(cp - 0x20), (int)sm,
            (unsigned)(cp - 0x20), bigConv, (unsigned)cp, Utf8(cp, u));
    }

    Put(&o, "\n# lowercase a-z -> uppercase glyph (both fonts, conv only):\n");
    for (int32_t ch = 'a'; ch <= 'z'; ch++)
        Put(&o, "conv[0x%02X]=%-4d # '%c' -> '%c'\n", (unsigned)(ch - 0x20), (int)(18 + (ch - 'a')),
            (char)ch, (char)(ch - 'a' + 'A'));
    free(cps);
    if (o.oom) {
        free(o.s);
        snprintf(err, 256, "out of memory");
        return -1;
    }
    *text = o.s;
    *size = o.len;
    return 0;
}

/* ---- -font-extend ------------------------------------------------------------ */

int32_t Sg_FontExtendRun(const char *inBmp, const char *outBmp, const char *glyphsPath,
                         int32_t fillMissing, int32_t emitIntegration, SgLogFn log, void *user, char *err)
{
    SgImage sheet;
    SgFontLayout layout;
    char line[1400], dir[1024], full[1024], path[1100];
    char *text = NULL;
    uint32_t size = 0;
    int32_t rc = -1;

    if (!outBmp || !outBmp[0])
        outBmp = inBmp;
    if (!glyphsPath || !glyphsPath[0])
        glyphsPath = "glyphs.txt";
    memset(&sheet, 0, sizeof sheet);
    memset(&layout, 0, sizeof layout);
    if (!Sg_IsFile(inBmp)) {
        snprintf(err, 256, "CHARSET bmp not found.");
        return -1;
    }
    if (!Sg_IsFile(glyphsPath)) {
        snprintf(err, 256, "glyphs file not found: %.200s", glyphsPath);
        return -1;
    }
    if (SgImage_ReadBmp(&sheet, inBmp, err) != 0)
        return -1;
    if (Sg_FontExtendEx(&sheet, glyphsPath, fillMissing, &layout, err) != 0)
        goto out;
    if (SgImage_WriteBmp(&sheet, outBmp, err) != 0)
        goto out;
    if (log) {
        snprintf(line, sizeof line, "Font: added %d glyphs x2 fonts = %d sprites -> %s%s", (int)layout.glyphs,
                 (int)layout.count, Sg_PathFileName(outBmp), fillMissing ? " (+ missing-glyph boxes)" : "");
        log(user, line);
    }
    if (emitIntegration) {
        /* next to the glyphs file, which is where SwosFont puts it */
        const char *base = Sg_PathFileName(glyphsPath);
        if (base == glyphsPath)
            snprintf(dir, sizeof dir, ".");
        else
            snprintf(dir, sizeof dir, "%.*s", (int)(base - glyphsPath - 1 > 0 ? base - glyphsPath - 1 : 1),
                     glyphsPath);
        Sg_PathFull(full, sizeof full, dir);
        Sg_PathJoin(path, sizeof path, full, "integration.txt");
        if (Sg_FontIntegrationText(&layout, &text, &size, err) != 0 || Sg_WriteFile(path, text, size, err) != 0)
            goto out;
        if (log) {
            snprintf(line, sizeof line, "Font: wrote %s (image-script + conversionTable wiring)", path);
            log(user, line);
        }
    }
    rc = 0;
out:
    free(text);
    Sg_FontLayoutFree(&layout);
    SgImage_Free(&sheet);
    return rc;
}

/* ---- -dos -font-extend ------------------------------------------------------- */

/* The PC game's font sprites as its CHARSET.DAT has them (sprites 0-56 the small
 * font, 6 lines; 57-112 the big one, 8): width the glyph's (its shadow column
 * included), centre 0,0, unk4 wquads * 2 with bit 7 set in the small font,
 * nlines_div4 height / 4, ordinal the sprite's number. */
static int32_t DosRecord(const Art *a, int32_t big, int32_t ordinal, SgDosSprite *s, uint8_t **owned, char *err)
{
    const int32_t h = big ? 8 : 6;      /* (the font's height, whatever rows the glyph drew) */
    uint8_t px[8 * MAX_COLS];
    uint32_t size;
    int16_t wq;

    memset(px, 0, sizeof px);
    for (int32_t r = 0; r < a->h && r < h; r++)
        for (int32_t c = 0; c < a->w; c++)
            px[r * a->w + c] = a->px[r][c];
    if (Sg_DosSpriteEncode(px, a->w, h, owned, &size, &wq, err) != 0)
        return -1;
    memset(s, 0, sizeof *s);
    s->data = *owned;
    s->size = size;
    s->width = (int16_t)a->w;
    s->height = (int16_t)h;
    s->wquads = wq;
    s->unk4 = (uint8_t)((big ? 0 : 0x80) | (wq * 2));
    s->heightDiv4 = (uint8_t)(h / 4);
    s->ordinal = (int16_t)ordinal;
    return 0;
}

/* A look at the result: the small font on the first row, the big one under it,
 * each glyph in a 10x10 cell, on the menus' blue. */
static int32_t DosPreview(const Art *small, const Art *big, int32_t n, const char *path, char *err)
{
    enum { CELL = 10, PER_ROW = 32 };
    const int32_t rows = (n + PER_ROW - 1) / PER_ROW, w = PER_ROW * CELL + 4, h = rows * 2 * CELL + 4;
    uint32_t pal[16] = { 0x000060, 0, 0xFFFFFF, 0, 0, 0, 0, 0, 0x202020 };
    SgImage img;
    int32_t rc;

    if (SgImage_Init(&img, w, h > 4 ? h : 8) != 0) {
        snprintf(err, 256, "out of memory");
        return -1;
    }
    SgImage_SetPaletteArgb(&img, pal, 16);
    for (int32_t i = 0; i < n; i++) {
        const int32_t x0 = 2 + (i % PER_ROW) * CELL, y0 = 2 + (i / PER_ROW) * 2 * CELL;
        for (int32_t f = 0; f < 2; f++) {
            const Art *a = f ? &big[i] : &small[i];
            for (int32_t r = 0; r < a->h; r++)
                for (int32_t c = 0; c < a->w && c < CELL; c++)
                    if (a->px[r][c])
                        img.px[(y0 + f * CELL + r) * img.w + x0 + c] = a->px[r][c];
        }
    }
    rc = SgImage_WriteBmp(&img, path, err);
    SgImage_Free(&img);
    return rc;
}

int32_t Sg_FontExtendDos(const char *glyphsPath, const char *outDir, int32_t base, SgLogFn log, void *user,
                         char *err)
{
    uint32_t seq[MAX_BLOCKS];
    int32_t nseq = 0, rc = -1;
    Art *small = NULL, *big = NULL;
    SgDosSprite *rec = NULL;
    const SgDosSprite **ptr = NULL;
    uint8_t **owned = NULL;
    SgBuf dat = { 0 };
    Out o;
    char path[1100], line[1400], u[5];

    memset(&o, 0, sizeof o);
    if (!glyphsPath || !glyphsPath[0])
        glyphsPath = "glyphs.txt";
    if (!outDir || !outDir[0])
        outDir = ".";
    if (base < 0 || base > 32767 - 2 * MAX_BLOCKS) {
        snprintf(err, 256, "-font-base=%d: a sprite number is 0..32767", (int)base);
        return -1;
    }
    if (!Sg_IsFile(glyphsPath)) {
        snprintf(err, 256, "glyphs file not found: %.200s", glyphsPath);
        return -1;
    }
    if (BuildArts(glyphsPath, seq, &nseq, &small, &big, err) != 0)
        goto out;
    rec = calloc((size_t)(nseq ? 2 * nseq : 1), sizeof *rec);
    ptr = calloc((size_t)(nseq ? 2 * nseq : 1), sizeof *ptr);
    owned = calloc((size_t)(nseq ? 2 * nseq : 1), sizeof *owned);
    if (!rec || !ptr || !owned) {
        snprintf(err, 256, "out of memory");
        goto out;
    }
    /* the small font's glyphs, then the big font's, in one order */
    for (int32_t k = 0; k < 2 * nseq; k++) {
        const int32_t big_ = k >= nseq, i = big_ ? k - nseq : k;
        if (DosRecord(big_ ? &big[i] : &small[i], big_, base + k, &rec[k], &owned[k], err) != 0)
            goto out;
        ptr[k] = &rec[k];
    }
    if (Sg_MakeDirs(outDir) != 0) {
        snprintf(err, 256, "cannot create %.200s", outDir);
        goto out;
    }
    if (Sg_DosDatBuild(ptr, 2 * nseq, 0, &dat, err) != 0)
        goto out;
    Sg_PathJoin(path, sizeof path, outDir, "FONTX.DAT");
    if (Sg_WriteFile(path, dat.data, dat.size, err) != 0)
        goto out;

    Put(&o, "# FONTX.DAT: %d records in the PC game's sprite format (as CHARSET.DAT's), from %s.\n",
        (int)(2 * nseq), Sg_PathFileName(glyphsPath));
    Put(&o, "# Records 0..%d are the small font (6 lines), %d..%d the big font (8 lines), in the same\n",
        (int)nseq - 1, (int)nseq, (int)(2 * nseq) - 1);
    Put(&o, "# order. Colours as the stock font's: 2 ink, 8 shadow, 0 transparent.\n");
    Put(&o, "#\n");
    Put(&o, "# The PC game draws a character c as sprite spriteIndexOffset + conversionTable[c - 0x20]:\n");
    Put(&o, "# smallCharsTable offset 0, bigCharsTable 57, so sprites 0-56 and 57-113 -- both full, and\n");
    Put(&o, "# CHARSET.DAT's 227 sprites are followed by SCORE.DAT's at 227. These records are numbered\n");
    Put(&o, "# (their ordinal) from %d: an exe that loads them as sprites %d.. and maps a character to them\n",
        (int)base, (int)base);
    Put(&o, "# needs a wider sprite table and a conversion past a byte -- the exe's own change.\n");
    Put(&o, "#\n");
    Put(&o, "# code  char  conv  small: record sprite w   big: record sprite w\n");
    for (int32_t i = 0; i < nseq; i++)
        Put(&o, "U+%04X  %-4s  0x%02X         %4d  %5d  %d          %4d  %5d  %d\n", (unsigned)seq[i],
            Utf8(seq[i], u), (unsigned)CpOf(seq[i]), (int)i, (int)(base + i), (int)small[i].w, (int)(nseq + i),
            (int)(base + nseq + i), (int)big[i].w);
    if (o.oom) {
        snprintf(err, 256, "out of memory");
        goto out;
    }
    Sg_PathJoin(path, sizeof path, outDir, "FONTX.txt");
    if (Sg_WriteFile(path, o.s, o.len, err) != 0)
        goto out;
    Sg_PathJoin(path, sizeof path, outDir, "FONTX.bmp");
    if (DosPreview(small, big, nseq, path, err) != 0)
        goto out;
    if (log) {
        snprintf(line, sizeof line, "Font (PC): %d glyphs x2 fonts = %d sprites -> FONTX.DAT, FONTX.txt, FONTX.bmp "
                 "in %.1000s", (int)nseq, (int)(2 * nseq), outDir);
        log(user, line);
    }
    rc = 0;
out:
    for (int32_t k = 0; owned && k < 2 * nseq; k++)
        free(owned[k]);
    free(owned);
    free(ptr);
    free(rec);
    free(o.s);
    Sg_BufFree(&dat);
    free(small);
    free(big);
    return rc;
}
