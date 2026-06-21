#pragma once

#include <juce_core/juce_core.h>
#include <vector>

namespace hitnotedmx::vocab
{

// Bump whenever any note's MEANING changes (a trigger moves, a colour is
// recoloured, a label is repurposed). Each version has a frozen snapshot in
// `mappings/v<N>.tsv` (note → chainName); the migration tool diffs two
// snapshots to convert clips across versions. Freeze procedure: mappings/README.md.
inline constexpr int kMappingVersion = 5;

// The trigger vocabulary — the SINGLE source of truth for what each MIDI note
// does and what it's called. Both the on-screen trigger menu (TriggerMenu) and
// the Ableton trigger-rack namer (Showcase::installRack) read it from here, so
// the rack chain names can never drift from the menu.
//
// A column is either a trigger column (a list of named tiles) or a palette
// column (a swatch octave); palette colours/names come from Palette.h.
struct Column
{
    juce::String title;                 // category header ("Zones", "Wild", …)
    int          octaveStart = 0;       // MIDI note of the column's bottom row (C)
    std::vector<juce::String> labels;   // trigger labels (empty for palette cols)
    bool         palette     = false;
    juce::String group;                 // "Prim" / "Sec" header (palette cols)
    int          paletteBase = 0;       // colour-table offset (palette cols)
};

// The ordered columns, built once and cached.
const std::vector<Column>& columns();

// Chain name for a MIDI note: the menu label with a 2-char group prefix
// (bk/sp/ba/pz/ch/br/wd/mc) or palette prefix (cp/cs), or "-" if the note has
// no trigger (Ableton renders an empty chain name as its default "Chain").
juce::String chainName (int note);

}  // namespace hitnotedmx::vocab
