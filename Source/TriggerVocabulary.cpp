#include "TriggerVocabulary.h"

#include "Palette.h"
#include "Recipes.h"

namespace hitnotedmx::vocab
{

namespace
{
constexpr int kSpotBarOctave = 0;    // blackout / spots / bars
constexpr int kZonesStart    = 12;   // pixel zones + combs
constexpr int kMasterStart   = 120;  // master / global hits (top of keyboard, octave 8)

// A trigger column: labels occupy note offsets 0,1,2,… from `octaveStart`.
Column trig (juce::String title, int octaveStart, std::vector<juce::String> labels)
{
    return { std::move (title), octaveStart, std::move (labels), false, {}, 0 };
}

// A palette column: one swatch octave. `paletteBase` is the colour-table
// offset at this octave's C (0 for a low octave, 12 for the high primary).
Column pal (juce::String group, int octaveStart, int paletteBase)
{
    return { {}, octaveStart, {}, true, std::move (group), paletteBase };
}

std::vector<Column> build()
{
    std::vector<Column> c;

    // Octave -2: total blackout (C-2) at the bottom, spots + bars above.
    c.push_back (trig ("Spots & bars", kSpotBarOctave,
        { "Blackout", "Spot L WW", "Spot L col", "Spot R WW", "Spot R col",
          "Bar 1", "Bar 2", "Bar 3", "Bar 4" }));

    c.push_back (trig ("Zones", kZonesStart,
        { "Zone 1", "Zone 2", "Zone 3", "Zone 4", "Zone 5", "Zone 6",
          "Zone 7", "Zone 8", "Zone 9", "Even", "Odd", "Thirds" }));

    c.push_back (trig ("Chases", kChasesStart,
        { "Chase up", "Chase dn", "Ping-pong", "Diag up", "Diag dn", "Snake",
          "Theater", "Spiral", "Waves", "Expand", "Contract", "Pong" }));

    c.push_back (trig ("Breathes", kBreathesStart,
        { "Tide", "Sine", "Ripple", "Ripple H", "Bloom", "Halo",
          "Moon rise", "Soft ball", "Drift", "Aurora", "Shimmer", "Smooth shimmer" }));

    c.push_back (trig ("Wild", kWildStart,
        { "Strobe", "Sparkle", "Sparkle few", "Lightning", "Glitch", "Static",
          "Rain", "Stutter", "Bounce", "Fast ball", "Zigzag", "Converge" }));

    // Multicolor spans two octaves (two columns of self-coloured recipes).
    c.push_back (trig ("Multicolor", kColorDynStart,
        { "Rainbow", "Comet", "VU meter", "VU smooth", "Fire", "Embers",
          "Magma", "Lava", "Heatmap", "Ocean", "Forest", "Desert" }));

    c.push_back (trig ("Multicolor", kColorDynStart + 12,
        { "Sunset", "Twilight", "Borealis", "Night sky", "Galaxy", "Nebula",
          "Storm", "Plasma", "Police", "Disco", "Velvet", "Rouge" }));

    // Palettes: Primary keeps two octaves, Secondary is one octave.
    c.push_back (pal ("Prim", kPrimaryPaletteStart,      0));
    c.push_back (pal ("Prim", kPrimaryPaletteStart + 12, 12));
    c.push_back (pal ("Sec",  kSecondaryPaletteStart,    0));

    // Master / global controls above the palette. Not per-fixture triggers;
    // computeDmx handles them as whole-rig overrides. The bumps flash the whole
    // frame (velocity = brightness); "To black" / "From black" glide the rig to
    // black and back; "Release" velocity sets how fast the bump tails decay and
    // the to/from-black fades glide (127 = instant, 0 = one bar); Freeze holds
    // the current frame while held; "Speed" (G8) velocity is the global recipe-
    // speed multiplier. F#8 (126, empty label) is left free between them.
    c.push_back (trig ("Master", kMasterStart,
        { "Bump white", "Bump color", "To black", "From black", "Freeze", "Release", "", "Speed" }));

    return c;
}

// The 2-char chain-name prefix for a trigger tile, by column + label. The
// Spots & bars column mixes three kinds, so it keys off the label.
juce::String prefixFor (const juce::String& title, const juce::String& label)
{
    if (title == "Spots & bars")
    {
        if (label == "Blackout")      return "bk";
        if (label.startsWith ("Spot")) return "sp";
        if (label.startsWith ("Bar"))  return "ba";
        return {};
    }
    if (title == "Zones")      return "pz";
    if (title == "Chases")     return "ch";
    if (title == "Breathes")   return "br";
    if (title == "Wild")       return "wd";
    if (title == "Multicolor") return "mc";
    if (title == "Master")     return "ms";
    return {};
}
}  // namespace

const std::vector<Column>& columns()
{
    static const std::vector<Column> cols = build();
    return cols;
}

juce::String chainName (int note)
{
    // Palette ranges resolve to colour names (cp / cs prefix).
    if (note >= kPrimaryPaletteStart && note < kPrimaryPaletteStart + kPaletteSize)
        return "cp " + juce::String (kPaletteNames[static_cast<size_t> (note - kPrimaryPaletteStart)]);
    if (note >= kSecondaryPaletteStart && note < kSecondaryPaletteEnd)
        return "cs " + juce::String (kSecondaryPaletteNames[static_cast<size_t> (note - kSecondaryPaletteStart)]);

    // Trigger columns: the tile label with its group prefix.
    for (const auto& col : columns())
    {
        if (col.palette)
            continue;
        const int idx = note - col.octaveStart;
        if (idx >= 0 && idx < static_cast<int> (col.labels.size()))
        {
            const auto& label = col.labels[static_cast<size_t> (idx)];
            if (label.isEmpty())
                return "-";   // explicit gap within a column (e.g. F#8 in Master)
            const auto pre = prefixFor (col.title, label);
            return pre.isEmpty() ? label : pre + " " + label;
        }
    }

    return "-";   // unused note
}

}  // namespace hitnotedmx::vocab
