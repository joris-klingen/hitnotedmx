#pragma once

#include <array>
#include <cstddef>

namespace hitnotedmx
{

// Colour palettes.
//
// Palettes start on a C so they line up with the keyboard (C3 = MIDI 60):
//   MIDI 84..107 (C5..B6) → primary palette   (two octaves, all 24 colours).
//   MIDI 108..119 (C7..B7) → secondary palette (one octave, 12 colours).
// Secondary is a single octave because a second would run past G8 (127), the
// top of MIDI. The two palettes are SEPARATE tables: the secondary is a set of
// softer accent hues chosen to complement (not duplicate) the saturated
// primaries, so a soft-velocity accent reads as a tasteful partner colour.

struct PaletteColor { float r, g, b; };

inline constexpr int kPaletteSize           = 24;
inline constexpr int kPrimaryPaletteStart   = 84;
inline constexpr int kSecondaryPaletteStart = 108;
inline constexpr int kSecondaryPaletteSize  = 12;   // secondary is one octave
// One past the last secondary colour — exclusive upper bound of the palette
// range (no blackout note here; a hard kill is the dim knobs / C-2 / Blackout).
inline constexpr int kSecondaryPaletteEnd   = kSecondaryPaletteStart + kSecondaryPaletteSize;  // 120

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

// Secondary accent palette — 12 softer, complementary hues. Spread around the
// wheel so any primary has a pleasing partner, but lighter/less saturated so
// the accent supports rather than fights the primary colour.
inline constexpr std::array<PaletteColor, kSecondaryPaletteSize> kSecondaryPalette {{
    { 1.000f, 0.820f, 0.550f },  //  0  Warm white
    { 1.000f, 0.420f, 0.380f },  //  1  Coral
    { 1.000f, 0.750f, 0.200f },  //  2  Gold
    { 0.620f, 0.900f, 0.220f },  //  3  Chartreuse
    { 0.100f, 0.820f, 0.450f },  //  4  Jade
    { 0.200f, 0.850f, 0.820f },  //  5  Aqua
    { 0.250f, 0.620f, 1.000f },  //  6  Azure
    { 0.480f, 0.500f, 0.950f },  //  7  Periwinkle
    { 0.700f, 0.520f, 0.960f },  //  8  Lavender
    { 0.850f, 0.400f, 0.900f },  //  9  Orchid
    { 1.000f, 0.450f, 0.650f },  // 10  Rose
    { 0.800f, 0.880f, 1.000f },  // 11  Cool white
}};

// Colour for a held palette note: primary notes index the 24-colour table,
// secondary notes index their own 12-colour complementary set. `offset` is
// `pitch - paletteStart`.
inline constexpr PaletteColor paletteColorFor (int paletteStart, int offset) noexcept
{
    return paletteStart == kSecondaryPaletteStart
               ? kSecondaryPalette[static_cast<std::size_t> (offset)]
               : kPalette[static_cast<std::size_t> (offset)];
}

}  // namespace hitnotedmx
