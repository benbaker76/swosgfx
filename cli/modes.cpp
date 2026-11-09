// modes.cpp -- one function per mode, each a call into the library.
#include "modes.h"

#include <cstdio>
#include <string>
#include <vector>

#include "paths.h"

extern "C" {
#include "amiga_palette.h"
#include "amiga_tools.h"
#include "dos2amiga.h"
#include "dos_picture.h"
#include "dos_pitch_render.h"
#include "dos_sprite_io.h"
#include "font_extend.h"
#include "jag2amiga.h"
#include "pal_file.h"
}

namespace {

// A library call's result as the exit status: 0, or 2 with its message.
int Done(int32_t rc, const char *err)
{
    if (rc == 0)
        return 0;
    std::fprintf(stderr, "Error: %s\n", err);
    return 2;
}

// The library's progress lines, to stdout as the C# printed them.
void PrintLine(void *user, const char *line)
{
    (void)user;
    std::printf("%s\n", line);
}

// Warnings, to stderr as the C# wrote them.
void PrintWarning(void *user, const char *line)
{
    (void)user;
    std::fprintf(stderr, "%s\n", line);
}

// The output folder of a mode that takes 0 or 1 positional arguments.
std::string OutDir(const Options &o)
{
    return o.files.empty() || o.files[0].empty() ? CurrentDir() : o.files[0];
}

int RunPalettes(const Options &o)
{
    char err[256] = "";
    const SgPalColor color = o.paletteColorFormat == PalColor::Rgb32 ? SG_PAL_COLOR_RGB32 : SG_PAL_COLOR_AMIGA12;
    const SgPalFile file = o.paletteFileFormat == PalFile::C        ? SG_PAL_FILE_C
                           : o.paletteFileFormat == PalFile::Palette ? SG_PAL_FILE_PALETTE
                                                                     : SG_PAL_FILE_ASM;
    SgPalFormat format = SG_PAL_FORMAT_ACT;
    switch (o.paletteFormat) {
    case PalFormat::Act: format = SG_PAL_FORMAT_ACT; break;
    case PalFormat::MsPal: format = SG_PAL_FORMAT_MSPAL; break;
    case PalFormat::Jasc: format = SG_PAL_FORMAT_JASC; break;
    case PalFormat::Gimp: format = SG_PAL_FORMAT_GIMP; break;
    case PalFormat::PaintNet: format = SG_PAL_FORMAT_PAINTNET; break;
    }
    const char *outDir = o.files.empty() ? nullptr : o.files[0].c_str();
    return Done(Sg_PalettesExport(outDir, color, file, o.paletteColorCount, o.paletteFull, format, err), err);
}

// -font-extend: the glyphs from -glyphs=, else glyphs.txt here, else the
// assets/glyphs.txt that ships beside swosgfx.
int RunFontExtend(const Options &o, const char *argv0)
{
    std::string glyphs = o.glyphsPath ? *o.glyphsPath : "glyphs.txt";
    if (!o.glyphsPath && !IsFile(glyphs)) {
        std::string shipped = JoinPath(ExeDir(argv0), "assets/glyphs.txt");
        if (IsFile(shipped))
            glyphs = shipped;
    }
    char err[256] = "";
    if (o.platform == Platform::Dos)
        return Done(Sg_FontExtendDos(glyphs.c_str(), o.files.empty() ? nullptr : o.files[0].c_str(), o.fontBase,
                                     PrintLine, nullptr, err),
                    err);
    const char *out = o.files.size() > 1 ? o.files[1].c_str() : nullptr;
    return Done(Sg_FontExtendRun(o.files[0].c_str(), out, glyphs.c_str(), !o.noMissingBoxes, 1,
                                 PrintLine, nullptr, err),
                err);
}

// -jag2amiga: the cartridge from -jag-rom= (as given, else beside swosgfx),
// else the first .jag/.j64/.rom/.bin in the Jaguar folder.
int RunJag2Amiga(const Options &o, const std::string &dosDir, const char *argv0)
{
    const std::string jagDir = ResolveDataDir(o.jagDirectory, argv0);
    std::string rom;
    if (o.jagRomPath && !o.jagRomPath->empty()) {
        rom = *o.jagRomPath;
        if (!IsFile(rom)) {
            std::string beside = JoinPath(ExeDir(argv0), rom);
            if (!IsFile(beside)) {
                std::fprintf(stderr, "Error: Jaguar ROM not found: %s\n", rom.c_str());
                return 2;
            }
            rom = beside;
        }
    } else {
        char found[1024];
        if (Sg_JagFindRom(jagDir.c_str(), found, sizeof found) != 0) {
            std::fprintf(stderr,
                         "Error: No Jaguar cartridge found in '%s'. Put the ROM there, or pass -jag-rom=<path>.\n",
                         jagDir.c_str());
            return 2;
        }
        rom = found;
    }
    char err[256] = "";
    return Done(Sg_Jag2AmigaRun(dosDir.c_str(), rom.c_str(), jagDir.c_str(), OutDir(o).c_str(), o.jagTemplate,
                                o.jagDumpSheets, PrintLine, nullptr, err),
                err);
}

SgAmigaKind AmigaKind(InputKind k)
{
    switch (k) {
    case InputKind::Map: return SG_AMIGA_KIND_MAP;
    case InputKind::Tiled: return SG_AMIGA_KIND_TILED;
    case InputKind::Bmp: return SG_AMIGA_KIND_BMP;
    case InputKind::Raw: return SG_AMIGA_KIND_RAW;
    default: return SG_AMIGA_KIND_NONE;
    }
}

SgAmigaKind AmigaKind(OutputKind k)
{
    switch (k) {
    case OutputKind::Map: return SG_AMIGA_KIND_MAP;
    case OutputKind::Tiled: return SG_AMIGA_KIND_TILED;
    case OutputKind::Bmp: return SG_AMIGA_KIND_BMP;
    case OutputKind::Raw: return SG_AMIGA_KIND_RAW;
    default: return SG_AMIGA_KIND_NONE;
    }
}

int RunAmiga(const Options &o, const std::string &amigaDir)
{
    std::vector<const char *> files;
    for (const std::string &f : o.files)
        files.push_back(f.c_str());
    SgAmigaOptions a;
    Sg_AmigaOptionsInit(&a);
    a.input = AmigaKind(o.input);
    a.output = AmigaKind(o.output);
    a.paletteName = o.paletteName ? o.paletteName->c_str() : nullptr;
    a.bitplanes = o.bitplanes;
    a.autoBitplanes = o.autoBitplanes;
    a.colorCount = o.amigaColorCount;   // the last of -bitplanes=N and -colors=N, as the C#
    a.noRnc = o.noRnc;
    a.amigaDir = amigaDir.c_str();
    a.files = files.data();
    a.fileCount = (int32_t)files.size();
    a.log = PrintLine;
    a.warn = PrintWarning;
    char err[256] = "";
    return Done(Sg_AmigaRun(&a, err), err);
}

int RunDos(const Options &o, const std::string &dosDir, const char *argv0)
{
    switch (o.dosMode) {
    case DosMode::Pitch: {
        // All six pitches, whatever -pitch says (as the C#).
        char err[256] = "";
        return Done(Sg_DosPitchRun(dosDir.c_str(), OutDir(o).c_str(), (int32_t)o.pitchType, o.dosColorCount,
                                   o.output == OutputKind::Tiled, o.insertBlankTile, o.tilesetWidth,
                                   PrintWarning, nullptr, err),
                    err);
    }
    case DosMode::Picture: {
        char err[256] = "";
        const bool one = o.files.size() > 1;
        const int32_t sw = o.pictureScale ? o.pictureScale->first : 0;
        const int32_t sh = o.pictureScale ? o.pictureScale->second : 0;
        return Done(Sg_DosPictureRun(dosDir.c_str(), one ? o.files[0].c_str() : nullptr,
                                     one ? o.files[1].c_str() : nullptr, sw, sh, PrintLine, nullptr, err),
                    err);
    }
    case DosMode::SpritesExport: {
        char err[256] = "";
        return Done(Sg_DosSpritesExport(dosDir.c_str(), OutDir(o).c_str(), o.spriteBackgroundIndex, err), err);
    }
    case DosMode::SpritesImport: {
        char err[256] = "";
        return Done(Sg_DosSpritesImport(dosDir.c_str(), o.files[0].c_str(), err), err);
    }
    case DosMode::Dos2Amiga: {
        char err[256] = "";
        return Done(Sg_Dos2AmigaRun(dosDir.c_str(), OutDir(o).c_str(), PrintLine, nullptr, err), err);
    }
    case DosMode::Jag2Amiga: return RunJag2Amiga(o, dosDir, argv0);
    }
    return 2;
}

}  // namespace

int RunMode(const Options &o, const char *argv0)
{
    if (o.paletteMode)
        return RunPalettes(o);
    if (o.fontExtendMode)
        return RunFontExtend(o, argv0);
    if (o.platform == Platform::Amiga)
        return RunAmiga(o, ResolveDataDir(o.amigaDirectory, argv0));
    return RunDos(o, ResolveDataDir(o.dosDirectory, argv0), argv0);
}
