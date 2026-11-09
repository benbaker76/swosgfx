/*
 * dos_sprite_io.h -- the PC game's sprites as sprNNNN.bmp files, and back.
 *
 * A port of the writing half of SwosGfx's DosSprite.cs (SaveSpriteToBmp,
 * SaveAllSpritesToDirectory, InsertSpriteFromBmp, InsertAllSpritesFromDirectory,
 * SaveChangesToSprites) and of DosSpriteCodec.cs's UnchainSprite: the
 * `-dos -sprites-export` and `-dos -sprites-import` modes.
 *
 * A sprite's BMP is 8-bit, width x height, colour 0..15 the sprite's own and
 * 16 the background (a transparent pixel); slot 16 of the palette is the menu
 * palette's colour `bgIndex`, so the background can be made any colour to
 * draw on. Importing maps 16 (and 0) back to transparent and keeps only the
 * low four bits of any other index.
 */
#ifndef SWOSGFX_DOS_SPRITE_IO_H
#define SWOSGFX_DOS_SPRITE_IO_H

#include <stdint.h>

#include "dos_sprite.h"

/* Write sprite `index` of a loaded set as an 8bpp BMP at `path`.
 * 0 written; 1 when the sprite has no pixels (zero width or height: SwosGfx
 * writes no file for it); -1 on failure, `err` (256 bytes) says why. */
int32_t Sg_DosSpriteExportBmp(const SgDosSprites *s, int32_t index, uint8_t bgIndex,
                              const char *path, char *err);

/* -dos -sprites-export: every sprite of the game in `dosDir` as
 * outDir/sprNNNN.bmp (0 <= NNNN < 1334), the folder created. 0 on success. */
int32_t Sg_DosSpritesExport(const char *dosDir, const char *outDir, uint8_t bgIndex, char *err);

/* -dos -sprites-import: read every sprNNNN.bmp in `spritesDir` (any case),
 * re-encode those sprites, and rewrite IN PLACE, in `dosDir`, the containers
 * that hold one of them and SPRITE.DAT. Nothing is written unless every file
 * read was good. 0 on success. */
int32_t Sg_DosSpritesImport(const char *dosDir, const char *spritesDir, char *err);

/* One sprite's pixels (width x height, top row first, 0..15, 0 transparent;
 * only the low four bits are kept) in the game's chained format: *data is
 * malloc'd, *size bytes (height * wquads * 8), *wquads the 16-pixel units a
 * line. 0 on success. */
int32_t Sg_DosSpriteEncode(const uint8_t *px, int32_t width, int32_t height, uint8_t **data,
                           uint32_t *size, int16_t *wquads, char *err);

/* A container (*.DAT) of `count` records, in order: each its 24-byte header
 * (spr_data = its offset + dataOfs + 24; size, dat_file, changed and unk1
 * zeroed) and its data. out->data is malloc'd (Sg_BufFree). 0 on success. */
int32_t Sg_DosDatBuild(const SgDosSprite *const *spr, int32_t count, uint32_t dataOfs, SgBuf *out, char *err);

#endif
