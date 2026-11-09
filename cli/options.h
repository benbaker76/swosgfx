// options.h -- the command line, parsed and checked. A port of the C# SwosGfx's
// CliOptions and TryParseOptions: the same switches, the same defaults and the
// same error messages, so scripts written for it keep working.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

enum class Platform { Amiga, Dos };
enum class InputKind { Unspecified, Map, Tiled, Bmp, Raw };
enum class OutputKind { Unspecified, Bmp, Tiled, Map, Raw };
enum class DosMode { Pitch, Picture, SpritesExport, SpritesImport, Dos2Amiga, Jag2Amiga };

// The PC pitch types, in the C# PitchType's order.
enum class PitchType { Frozen, Muddy, Wet, Soft, Normal, Dry, Hard };

enum class PalColor { Amiga12, Rgb32 };
enum class PalFile { Asm, C, Palette };
enum class PalFormat { Act, MsPal, Jasc, Gimp, PaintNet };

struct Options {
    bool showHelp = false;
    bool showVersion = false;
    Platform platform = Platform::Amiga;
    bool platformExplicitlySet = false;

    // Amiga-style I/O
    InputKind input = InputKind::Unspecified;
    OutputKind output = OutputKind::Unspecified;
    std::optional<std::string> paletteName;
    int32_t bitplanes = 4;          // 4 planes -> 16 colours
    bool autoBitplanes = false;     // -bitplanes=auto: 4 or 8 per image (-output=raw)
    bool noRnc = false;
    int32_t amigaColorCount = 16;

    std::string amigaDirectory = "Amiga";
    std::string dosDirectory = "DOS";

    // Jaguar cartridge (-jag2amiga)
    std::string jagDirectory = "Jag";
    std::optional<std::string> jagRomPath;
    bool jagTemplate = false;
    bool jagDumpSheets = false;

    DosMode dosMode = DosMode::Pitch;

    // PC pitch rendering
    int32_t pitchIndex = 0;
    PitchType pitchType = PitchType::Normal;
    int32_t dosColorCount = 256;

    // PC pitch -> Tiled: names and defaults as Tiled2Bin's "-slice" switches
    bool insertBlankTile = false;
    int32_t tilesetWidth = 256;

    // -picture -scale[=WxH]: bilinear enlargement (default the Amiga 352x272)
    std::optional<std::pair<int32_t, int32_t>> pictureScale;

    // PC sprites
    uint8_t spriteBackgroundIndex = 0;

    std::vector<std::string> files;

    // -palettes
    bool paletteMode = false;
    PalColor paletteColorFormat = PalColor::Amiga12;
    PalFile paletteFileFormat = PalFile::Asm;
    int32_t paletteColorCount = 16;
    bool paletteFull = false;
    PalFormat paletteFormat = PalFormat::Act;

    // -font-extend
    bool fontExtendMode = false;
    std::optional<std::string> glyphsPath;
    bool noMissingBoxes = false;
    int32_t fontBase = 1334;                 // -dos -font-extend: the records' first sprite number
};

// Parse argv (without the program name). On failure returns false and sets
// `error` (empty when only the usage should be shown).
bool ParseOptions(const std::vector<std::string> &args, Options &opts, std::string &error);

const char *PitchTypeName(PitchType t);     // "normal", ...
void PrintUsage();
