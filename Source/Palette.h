#pragma once

#include <array>

namespace hitnotedmx
{

// 24-entry colour palette.
//
// Octaves 4-5 (MIDI 36..59) → primary palette.
// Octaves 6-7 (MIDI 60..83) → secondary palette.
// Both palettes use the same 24 colors; the index is `pitch - palette_start`.

struct PaletteColor { float r, g, b; };

inline constexpr int kPaletteSize          = 24;
inline constexpr int kPrimaryPaletteStart  = 36;
inline constexpr int kSecondaryPaletteStart = 60;
inline constexpr int kBlackoutNote         = 84;

inline constexpr std::array<PaletteColor, kPaletteSize> kPalette {{
    { 0.000f, 0.000f, 0.000f },  //  0  Black
    { 1.000f, 0.000f, 0.000f },  //  1  Red
    { 1.000f, 0.235f, 0.000f },  //  2  Orange-red
    { 1.000f, 0.471f, 0.000f },  //  3  Orange
    { 1.000f, 0.706f, 0.000f },  //  4  Amber
    { 1.000f, 0.902f, 0.000f },  //  5  Yellow
    { 0.706f, 1.000f, 0.000f },  //  6  Lime
    { 0.000f, 1.000f, 0.000f },  //  7  Green
    { 0.000f, 1.000f, 0.471f },  //  8  Mint
    { 0.000f, 0.784f, 0.706f },  //  9  Teal
    { 0.000f, 0.863f, 1.000f },  // 10  Cyan
    { 0.000f, 0.549f, 1.000f },  // 11  Sky
    { 0.000f, 0.000f, 1.000f },  // 12  Blue
    { 0.235f, 0.000f, 0.902f },  // 13  Royal
    { 0.392f, 0.000f, 0.784f },  // 14  Indigo
    { 0.627f, 0.000f, 0.863f },  // 15  Violet
    { 0.745f, 0.000f, 0.745f },  // 16  Purple
    { 1.000f, 0.000f, 0.784f },  // 17  Magenta
    { 1.000f, 0.392f, 0.706f },  // 18  Pink
    { 1.000f, 0.157f, 0.471f },  // 19  Hot pink
    { 0.706f, 0.000f, 0.157f },  // 20  Crimson
    { 1.000f, 0.706f, 0.431f },  // 21  Warm white (palette)
    { 0.863f, 0.902f, 1.000f },  // 22  Cool white
    { 0.784f, 0.706f, 1.000f },  // 23  Lavender
}};

}  // namespace hitnotedmx
