// options.cpp -- see options.h. Switches are matched without regard to case;
// paths keep theirs.
#include "options.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>

namespace {

std::string Lower(const std::string &s)
{
    std::string r = s;
    for (char &c : r)
        c = (char)std::tolower((unsigned char)c);
    return r;
}

bool StartsWith(const std::string &s, const char *prefix, std::string *rest = nullptr)
{
    const std::string p(prefix);
    if (s.compare(0, p.size(), p) != 0)
        return false;
    if (rest)
        *rest = s.substr(p.size());
    return true;
}

// int.TryParse: optional sign, digits, nothing else.
bool ParseInt(const std::string &s, int32_t &out)
{
    if (s.empty())
        return false;
    char *end = nullptr;
    long v = std::strtol(s.c_str(), &end, 10);
    if (*end != '\0' || v < INT32_MIN || v > INT32_MAX)
        return false;
    out = (int32_t)v;
    return true;
}

const char *const kPitchTypeNames[] = { "frozen", "muddy", "wet", "soft", "normal", "dry", "hard" };

bool ParsePitchType(const std::string &value, PitchType &type)
{
    // Trimmed, any case: the C# matches it against the pitch palettes' names.
    size_t b = value.find_first_not_of(" \t"), e = value.find_last_not_of(" \t");
    const std::string v = b == std::string::npos ? std::string() : Lower(value.substr(b, e - b + 1));
    for (int i = 0; i < 7; i++)
        if (v == kPitchTypeNames[i]) {
            type = (PitchType)i;
            return true;
        }
    return false;
}

const char *const kDosModes = "-picture, -sprites-export, -sprites-import";

bool SetDosMode(Options &o, DosMode mode, const char *list, std::string &error)
{
    if (o.dosMode != DosMode::Pitch) {
        error = std::string("Cannot specify multiple DOS modes (") + list + ").";
        return false;
    }
    o.dosMode = mode;
    return true;
}

bool SetInput(Options &o, InputKind kind, const char *list, std::string &error)
{
    if (o.input != InputKind::Unspecified && o.input != kind) {
        error = std::string("Multiple input kinds specified. Use only one of: ") + list + ".";
        return false;
    }
    o.input = kind;
    return true;
}

}  // namespace

const char *PitchTypeName(PitchType t) { return kPitchTypeNames[(int)t]; }

bool ParseOptions(const std::vector<std::string> &args, Options &o, std::string &error)
{
    o = Options();
    error.clear();
    std::string v;

    for (const std::string &arg : args) {
        if (arg.empty() || arg[0] != '-') {
            o.files.push_back(arg);     // a positional file
            continue;
        }
        const std::string lower = Lower(arg);

        if (lower == "-h" || lower == "-?" || lower == "--help") {
            o.showHelp = true;
            return true;
        } else if (lower == "--version" || lower == "-version") {
            o.showVersion = true;
            return true;
        } else if (lower == "-amiga" || lower == "-dos") {
            Platform p = lower == "-amiga" ? Platform::Amiga : Platform::Dos;
            if (o.platformExplicitlySet && o.platform != p) {
                error = "Cannot specify both -amiga and -dos.";
                return false;
            }
            o.platform = p;
            o.platformExplicitlySet = true;
        } else if (StartsWith(lower, "-amiga-dir=")) {
            o.amigaDirectory = arg.substr(11);
        } else if (StartsWith(lower, "-dos-dir=")) {
            o.dosDirectory = arg.substr(9);
        } else if (lower == "-picture") {
            if (!SetDosMode(o, DosMode::Picture, kDosModes, error))
                return false;
        } else if (lower == "-scale") {
            o.pictureScale = std::make_pair(352, 272);
        } else if (StartsWith(lower, "-scale=", &v)) {
            size_t xi = v.find('x');
            int32_t sw = 0, sh = 0;
            if (xi == std::string::npos || xi == 0 || !ParseInt(v.substr(0, xi), sw) ||
                !ParseInt(v.substr(xi + 1), sh) || sw <= 0 || sh <= 0) {
                error = "-scale expects WxH, e.g. -scale=352x272.";
                return false;
            }
            o.pictureScale = std::make_pair(sw, sh);
        } else if (lower == "-sprites-export") {
            if (!SetDosMode(o, DosMode::SpritesExport, kDosModes, error))
                return false;
        } else if (lower == "-sprites-import") {
            if (!SetDosMode(o, DosMode::SpritesImport, kDosModes, error))
                return false;
        } else if (lower == "-dos2amiga") {
            if (!SetDosMode(o, DosMode::Dos2Amiga,
                            "-picture, -sprites-export, -sprites-import, -dos2amiga", error))
                return false;
        } else if (lower == "-jag2amiga") {
            if (!SetDosMode(o, DosMode::Jag2Amiga,
                            "-picture, -sprites-export, -sprites-import, -dos2amiga, -jag2amiga", error))
                return false;
        } else if (lower == "-jag-template") {
            o.jagTemplate = true;
        } else if (lower == "-jag-dump-sheets") {
            o.jagDumpSheets = true;
        } else if (StartsWith(lower, "-jag-rom=")) {
            o.jagRomPath = arg.substr(9);
        } else if (StartsWith(lower, "-jag-dir=")) {
            o.jagDirectory = arg.substr(9);
        } else if (StartsWith(lower, "-sprite-bg=", &v)) {
            int32_t idx;
            if (!ParseInt(v, idx) || idx < 0 || idx > 255) {
                error = "Invalid sprite background index '" + v + "'. Expected 0-255.";
                return false;
            }
            o.spriteBackgroundIndex = (uint8_t)idx;
        } else if (lower == "-map") {
            if (!SetInput(o, InputKind::Map, "-map, -tmx, -bmp, -raw", error))
                return false;
        } else if (lower == "-tmj" || lower == "-json" || lower == "-tmx") {
            // One input kind: the format is sniffed from the file.
            if (!SetInput(o, InputKind::Tiled, "-map, -tmj, -bmp, -raw", error))
                return false;
        } else if (lower == "-bmp") {
            if (!SetInput(o, InputKind::Bmp, "-map, -tmx, -bmp, -raw", error))
                return false;
        } else if (lower == "-raw") {
            if (!SetInput(o, InputKind::Raw, "-map, -tmx, -bmp, -raw", error))
                return false;
        } else if (StartsWith(lower, "-output=", &v)) {
            if (o.output != OutputKind::Unspecified) {
                error = "Multiple -output options specified.";
                return false;
            }
            if (v == "bmp")
                o.output = OutputKind::Bmp;
            else if (v == "tmj" || v == "json" || v == "tmx")
                o.output = OutputKind::Tiled;
            else if (v == "map")
                o.output = OutputKind::Map;
            else if (v == "raw")
                o.output = OutputKind::Raw;
            else {
                error = "Unknown output type '" + v + "'. Expected bmp, tmj, map, or raw.";
                return false;
            }
        } else if (StartsWith(lower, "-palette=")) {
            if (o.paletteName) {
                error = "Multiple -palette options specified.";
                return false;
            }
            o.paletteName = arg.substr(9);
        } else if (StartsWith(lower, "-bitplanes=", &v)) {
            if (v == "auto") {
                o.autoBitplanes = true;
            } else {
                int32_t planes;
                if (!ParseInt(v, planes)) {
                    error = "Invalid bitplanes value '" + v + "'. Expected integer 1-8 or 'auto'.";
                    return false;
                }
                if (planes < 1 || planes > 8) {
                    error = "Bitplanes must be between 1 and 8 (4=16 colors, 8=256 colors).";
                    return false;
                }
                o.bitplanes = planes;
                o.amigaColorCount = 1 << planes;
            }
        } else if (lower == "-no-rnc") {
            o.noRnc = true;
        } else if (StartsWith(lower, "-pitch=", &v)) {
            int32_t idx;
            if (!ParseInt(v, idx) || idx < 0) {
                error = "Invalid pitch index '" + v + "'. Expected non-negative integer.";
                return false;
            }
            o.pitchIndex = idx;
        } else if (StartsWith(lower, "-type=", &v)) {
            if (!ParsePitchType(v, o.pitchType)) {
                error = "Invalid DOS pitch type '" + v + "'. Valid: frozen, muddy, wet, soft, normal, dry, hard.";
                return false;
            }
        } else if (StartsWith(lower, "-colors=", &v)) {
            int32_t n;
            if (!ParseInt(v, n) || n < 16 || n > 256) {
                error = "Invalid colors '" + v + "'. Expected range 16-256.";
                return false;
            }
            o.amigaColorCount = n;
            o.dosColorCount = n;
        } else if (lower == "-insertblanktile") {
            o.insertBlankTile = true;
        } else if (StartsWith(lower, "-tilesetwidth=", &v)) {
            int32_t w;
            if (!ParseInt(v, w) || w < 16 || w % 16 != 0) {
                error = "Invalid tileset width '" + v + "'. Expected a multiple of 16 (pixels).";
                return false;
            }
            o.tilesetWidth = w;
        } else if (lower == "-palettes") {
            o.paletteMode = true;
        } else if (lower == "-font-extend") {
            o.fontExtendMode = true;
        } else if (StartsWith(lower, "-glyphs=")) {
            o.glyphsPath = arg.substr(8);
        } else if (lower == "-no-boxes") {
            o.noMissingBoxes = true;
        } else if (StartsWith(lower, "-font-base=", &v)) {
            int32_t b = 0;
            if (!ParseInt(v, b) || b < 0 || b > 32767) {
                error = "Invalid -font-base '" + v + "'. Expected a sprite number, 0..32767.";
                return false;
            }
            o.fontBase = b;
        } else if (StartsWith(lower, "-pal-color=", &v)) {
            if (v == "amiga12")
                o.paletteColorFormat = PalColor::Amiga12;
            else if (v == "rgb32")
                o.paletteColorFormat = PalColor::Rgb32;
            else {
                error = "Invalid palette color format '" + v + "'. Expected amiga12 or rgb32.";
                return false;
            }
        } else if (StartsWith(lower, "-pal-file=", &v)) {
            if (v == "asm")
                o.paletteFileFormat = PalFile::Asm;
            else if (v == "c")
                o.paletteFileFormat = PalFile::C;
            else if (v == "palette")
                o.paletteFileFormat = PalFile::Palette;
            else {
                error = "Invalid palette file format '" + v + "'. Expected asm, c, or palette.";
                return false;
            }
        } else if (StartsWith(lower, "-pal-count=", &v)) {
            int32_t n;
            if (!ParseInt(v, n) || (n != 16 && n != 128 && n != 256)) {
                error = "Invalid palette color count '" + v + "'. Expected 16, 128, or 256.";
                return false;
            }
            o.paletteColorCount = n;
        } else if (lower == "-pal-full") {
            o.paletteFull = true;
        } else if (StartsWith(lower, "-pal-format=", &v)) {
            if (v == "act")
                o.paletteFormat = PalFormat::Act;
            else if (v == "mspal")
                o.paletteFormat = PalFormat::MsPal;
            else if (v == "jasc")
                o.paletteFormat = PalFormat::Jasc;
            else if (v == "gimp")
                o.paletteFormat = PalFormat::Gimp;
            else if (v == "paintnet")
                o.paletteFormat = PalFormat::PaintNet;
            else {
                error = "Invalid palette format '" + v + "'. Expected act, mspal, jasc, gimp, or paintnet.";
                return false;
            }
        } else {
            error = "Unknown option '" + arg + "'.";
            return false;
        }
    }

    // -palettes ignores -amiga/-dos/-map/-output and the rest.
    if (o.paletteMode)
        return true;

    if (o.fontExtendMode && o.platform == Platform::Dos) {
        if (o.files.size() > 1) {
            error = "-dos -font-extend expects: [outDir] (default: the current folder).";
            return false;
        }
        return true;
    }
    if (o.fontExtendMode) {
        if (o.files.empty() || o.files.size() > 2) {
            error = "-font-extend expects: in.bmp [out.bmp] (out defaults to in, i.e. in-place).";
            return false;
        }
        return true;
    }

    if (o.autoBitplanes && (o.platform != Platform::Amiga || o.output != OutputKind::Raw)) {
        error = "-bitplanes=auto is only supported with Amiga -output=raw.";
        return false;
    }
    if (o.platform == Platform::Amiga && o.dosMode != DosMode::Pitch) {
        error = "DOS-specific options (-picture, -sprites-export, -sprites-import) can only be used with -dos.";
        return false;
    }

    const size_t n = o.files.size();
    if (o.platform == Platform::Amiga) {
        if (o.input == InputKind::Unspecified) {
            error = "No input type specified. Use one of: -map, -tmx, -bmp, -raw.";
            return false;
        }
        if (o.output == OutputKind::Unspecified) {
            error = "No output type specified. Use -output=bmp|tmx|map|raw.";
            return false;
        }
        if (n == 2)
            return true;
        if (n <= 1) {
            // Batch: input from the Amiga folder, the one argument the output folder.
            bool supported = (o.input == InputKind::Raw && o.output == OutputKind::Bmp) ||
                             (o.input == InputKind::Map &&
                              (o.output == OutputKind::Bmp || o.output == OutputKind::Tiled));
            if (!supported) {
                error = "In Amiga batch mode (0 or 1 positional args), supported conversions are: "
                        "-raw -output=bmp, -map -output=bmp, -map -output=tmx. "
                        "For other conversions, specify explicit input and output paths.";
                return false;
            }
            return true;
        }
        error = "Too many file arguments for Amiga mode. Expected 0/1 (batch) or 2 (in out), got " +
                std::to_string(n) + ".";
        return false;
    }

    // DOS
    auto noInput = [&](const char *mode, const char *why) {
        if (o.input == InputKind::Unspecified)
            return true;
        error = std::string("In -dos ") + mode + " mode, do not specify -map/-tmx/-bmp/-raw; " + why;
        return false;
    };
    auto atMostOneDir = [&](const char *mode) {
        if (n <= 1)
            return true;
        error = std::string("In -dos ") + mode + " mode, expected 0 or 1 argument (output directory), got " +
                std::to_string(n) + ".";
        return false;
    };
    switch (o.dosMode) {
    case DosMode::Pitch:
        if (o.output == OutputKind::Unspecified) {
            error = "In -dos pitch mode, -output=bmp or -output=tmx is required.";
            return false;
        }
        if (o.output != OutputKind::Bmp && o.output != OutputKind::Tiled) {
            error = "In -dos pitch mode, only -output=bmp or -output=tmx is supported.";
            return false;
        }
        if (o.input != InputKind::Unspecified) {
            error = "In -dos pitch mode, do not specify -map/-tmx/-bmp/-raw; input is implicit.";
            return false;
        }
        if (n > 1) {
            error = "In -dos pitch mode, expected 0 or 1 argument (output directory), got " + std::to_string(n) + ".";
            return false;
        }
        break;
    case DosMode::Picture:
        if (o.output == OutputKind::Unspecified) {
            error = "In -dos -picture mode, -output=bmp is required.";
            return false;
        }
        if (o.output != OutputKind::Bmp) {
            error = "In -dos -picture mode, only -output=bmp is supported.";
            return false;
        }
        if (!noInput("-picture", "input is implicit (.256)."))
            return false;
        // The C# also accepts no files here (every .256 of the DOS folder),
        // although its check asks for two; that batch is kept.
        if (n != 0 && n != 2) {
            error = "In -dos -picture mode, expected 2 file arguments (in.256 out.bmp), got " + std::to_string(n) + ".";
            return false;
        }
        break;
    case DosMode::SpritesExport:
        if (!noInput("-sprites-export", "input is implicit from DOS folder."))
            return false;
        if (o.output == OutputKind::Unspecified)
            o.output = OutputKind::Bmp;
        else if (o.output != OutputKind::Bmp) {
            error = "In -dos -sprites-export mode, only -output=bmp is supported.";
            return false;
        }
        if (!atMostOneDir("-sprites-export"))
            return false;
        break;
    case DosMode::SpritesImport:
        if (!noInput("-sprites-import", "input is implicit from sprNNNN.bmp files."))
            return false;
        if (o.output != OutputKind::Unspecified) {
            error = "In -dos -sprites-import mode, do not specify -output=...; changes are applied in-place to DAT/SPRITE.DAT.";
            return false;
        }
        if (n != 1) {
            error = "In -dos -sprites-import mode, expected 1 argument (sprites directory), got " + std::to_string(n) + ".";
            return false;
        }
        break;
    case DosMode::Dos2Amiga:
        if (!noInput("-dos2amiga", "input is implicit from the DOS folder."))
            return false;
        if (!atMostOneDir("-dos2amiga"))
            return false;
        break;
    case DosMode::Jag2Amiga:
        if (!noInput("-jag2amiga", "input is the Jaguar cartridge plus the DOS folder."))
            return false;
        if (!atMostOneDir("-jag2amiga"))
            return false;
        break;
    }
    return true;
}

void PrintUsage()
{
    std::printf(
        "swosgfx v%s - Sensible World of Soccer graphics tool (Amiga, PC, Jaguar)\n"
        "\n"
        "Amiga usage:\n"
        "  swosgfx -amiga -map    -output=bmp  [-palette=<name>] [-bitplanes=N] [-no-rnc] in.map out.bmp\n"
        "  swosgfx -amiga -map    -output=tmj  [-palette=<name>] [-bitplanes=N] [-no-rnc] in.map out.tmj\n"
        "  swosgfx -amiga -tmj    -output=map  [-bitplanes=N]    [-no-rnc]      in.tmj out.map\n"
        "  swosgfx -amiga -bmp    -output=map  [-bitplanes=N]    [-no-rnc]      fullPitch.bmp out.map\n"
        "  swosgfx -amiga -bmp    -output=raw  [-palette=<name>] [-bitplanes=N] [-no-rnc] in.bmp out.raw\n"
        "  swosgfx -amiga -raw    -output=bmp  [-palette=<name>] [-bitplanes=N] [-no-rnc] in.raw out.bmp\n"
        "\n"
        "Amiga batch usage (input from ./Amiga or -amiga-dir=..., output to current dir or optional outDir):\n"
        "  swosgfx -amiga -raw -output=bmp  [-palette=<name>] [-bitplanes=N] [-no-rnc] [outDir]\n"
        "  swosgfx -amiga -map -output=bmp  [-palette=<name>] [-bitplanes=N] [-no-rnc] [outDir]\n"
        "  swosgfx -amiga -map -output=tmj  [-palette=<name>] [-bitplanes=N] [-no-rnc] [outDir]\n"
        "\n"
        "PC pitch usage (patterns from ./DOS):\n"
        "  swosgfx -dos -output=bmp -pitch=N -type=normal  [-colors=N] [outDir]\n"
        "  swosgfx -dos -output=tmj -pitch=N -type=normal  [-colors=N] [-insertblanktile] [-tilesetwidth=N] [outDir]\n"
        "    (pitch defaults: -pitch=0 -type=normal)\n"
        "\n"
        "PC picture usage (.256 -> BMP):\n"
        "  swosgfx -dos -picture -output=bmp [-scale[=WxH]] in.256 out.bmp\n"
        "  swosgfx -dos -picture -output=bmp          (every .256 in the DOS folder)\n"
        "\n"
        "PC sprite usage (SPRITE.DAT + *.DAT from ./DOS):\n"
        "  Export all sprites to BMP:\n"
        "    swosgfx -dos -sprites-export [-sprite-bg=N] [-output=bmp] [outDir]\n"
        "      (writes sprNNNN.bmp, 0 <= N < 1334)\n"
        "  Import sprites from BMP and update DAT/SPRITE.DAT:\n"
        "    swosgfx -dos -sprites-import spritesDir\n"
        "      (reads sprNNNN.bmp, inserts and saves changes)\n"
        "\n"
        "PC -> Amiga bank conversion (rebuild Amiga sprite banks from PC graphics):\n"
        "  swosgfx -dos -dos2amiga [outDir]\n"
        "    Reads SPRITE.DAT + *.DAT from ./DOS (or -dos-dir=...) and writes 8bpp\n"
        "    256-colour BMP banks with the original Amiga sprite layout, sourced\n"
        "    from the PC graphics + PC palette. Produces: CJCGRAFS, CJCTEAM1,\n"
        "    CJCTEAM2, CJCTEAM3, CJCTEAMG, CJCBITS, SOCCER_S, CJCBENCH (gameplay,\n"
        "    Game palette), plus CHARSET, MENUS, MENUS2 (menu, Menu palette).\n"
        "\n"
        "Jaguar -> Amiga bank conversion (rebuild Amiga sprite banks from a cartridge):\n"
        "  swosgfx -dos -jag2amiga [-jag-rom=<path>] [outDir]\n"
        "    Unpacks the Jaguar (Sensible Soccer International Edition) sprite sheets\n"
        "    and lays them into the Amiga bank layout, writing 8bpp BMP banks in the\n"
        "    Jaguar CRY palette (also dumped as JAG_GAME.act). Sprites the Jaguar\n"
        "    release does not have - notably the goal celebration frames - fall back\n"
        "    to the PC graphics, so the folder is a complete set. Needs ./DOS too.\n"
        "      -jag-template     also write <bank>_DOS.bmp (the whole original bank's\n"
        "                        PC art in the Jaguar palette), <bank>_TODO.bmp (only\n"
        "                        the cells the Jaguar sheet has not got - the redraw\n"
        "                        list) and <bank>_placements.csv.\n"
        "      -jag-dump-sheets  also write <bank>_JAGSHEET.bmp, the raw unpacked\n"
        "                        Jaguar sheet before the placement walk.\n"
        "    Hand-drawn art goes back in as <jag-dir>/<bank>_PATCH.bmp, a whole\n"
        "    320x256 bank whose non-zero pixels are laid over the Jaguar sheet;\n"
        "    repaint <bank>_TODO.bmp and save it under that name. The goal\n"
        "    celebrations are <jag-dir>/CJCTEAMn_CELEB.bmp (assets/jag has ours).\n"
        "\n"
        "Font extend (add extended glyphs from glyphs.txt into a CHARSET bmp):\n"
        "  swosgfx -font-extend [-glyphs=glyphs.txt] [-no-boxes] charset.bmp [out.bmp]\n"
        "    Draws the full-ASCII + accented glyphs into the free cells of the\n"
        "    charset sheet (palette preserved; works for AGA or OCS CHARSET), and\n"
        "    fills empty cells with the 'missing glyph' box (rows 0-27; -no-boxes\n"
        "    skips this). If out.bmp is omitted the input is overwritten. Also\n"
        "    writes integration.txt. (assets/glyphs.txt is the default glyph set.)\n"
        "  swosgfx -dos -font-extend [-glyphs=glyphs.txt] [-font-base=N] [outDir]\n"
        "    The same glyphs for the PC game, whose fonts are sprites (CHARSET.DAT\n"
        "    0-56 small, 57-113 big, both full): writes outDir/FONTX.DAT (the glyphs\n"
        "    as records in the PC sprite format, small font then big, numbered\n"
        "    from N, default 1334 -- after the game's last sprite), FONTX.txt\n"
        "    (each character's record, sprite number and conversion code) and\n"
        "    FONTX.bmp (a preview). Loading them is the exe's change.\n"
        "\n"
        "Common options:\n"
        "  -amiga                    Amiga mode (default)\n"
        "  -dos                      PC (DOS) mode\n"
        "  -amiga-dir=<path>         Amiga directory (default: ./Amiga)\n"
        "  -dos-dir=<path>           PC game directory (default: ./DOS)\n"
        "  -map | -tmj | -bmp | -raw Input type (Amiga only; -json and -tmx are aliases)\n"
        "  -output=bmp|tmj|map|raw   Output type (Tiled maps are JSON; json/tmx are aliases)\n"
        "  -palette=<name>           Palette (Amiga): Soft, Muddy, Frozen, Dry, Normal, Hard, Wet\n"
        "  -bitplanes=N              Bitplanes 1-8 (default 4; 4=16 colors, 8=256)\n"
        "  -bitplanes=auto           Auto: 4 planes if image uses only colors 0-15, else 8 (-output=raw only)\n"
        "  -no-rnc                   Disable RNC compression for Amiga MAP/RAW outputs\n"
        "  -pitch=N                  PC pitch index (0..MaxPitch-1), default 0\n"
        "  -type=name                PC pitch type: frozen, muddy, wet, soft, normal, dry, hard\n"
        "  -colors=N                 PC: remap to N colors (16-256)\n"
        "  -insertblanktile          PC pitch -output=tmj: reserve tile id 0 as a blank tile\n"
        "  -tilesetwidth=N           PC pitch -output=tmj: tileset image width in pixels (default 256)\n"
        "  -picture                  PC: operate on a .256 picture file\n"
        "  -scale[=WxH]              PC picture: bilinear-enlarge (default 352x272)\n"
        "  -sprites-export           PC: export all sprites to sprNNNN.bmp files\n"
        "  -sprites-import           PC: import sprNNNN.bmp files into DAT/SPRITE.DAT\n"
        "  -dos2amiga                PC: rebuild Amiga gameplay banks from PC graphics\n"
        "  -jag2amiga                Jaguar: rebuild Amiga gameplay banks from a Jaguar cartridge\n"
        "  -jag-rom=<path>           Jaguar cartridge image (default: first ROM in ./Jag)\n"
        "  -jag-dir=<path>           Jaguar directory (default: ./Jag)\n"
        "  -sprite-bg=N              PC sprites: menu palette index for background color (in BMP slot 16)\n"
        "  -h, -?                    Show this help\n"
        "  --version                 Show the version\n"
        "\n"
        "A relative -amiga-dir, -dos-dir or -jag-dir is looked for in the current\n"
        "folder first, then beside swosgfx.\n"
        "\n"
        "Amiga raw format dimensions:\n"
        "  320x256, 352x272\n"
        "\n"
        "Palette export (writes multiple files):\n"
        "  swosgfx -palettes\n"
        "    [-pal-color=amiga12|rgb32]     (default: amiga12)\n"
        "    [-pal-file=asm|c|palette]      (default: asm)\n"
        "    [-pal-count=16|128|256]        (default: 16)\n"
        "    [-pal-full]                    (default: only pitch-affected colors for pitches)\n"
        "    [-pal-format=act|mspal|jasc|gimp|paintnet]  (for -pal-file=palette, default: act)\n"
        "    [outDir]                       Optional output directory\n"
        "\n",
        SWOSGFX_VERSION);
}
