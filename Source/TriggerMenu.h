#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <functional>
#include <vector>

namespace hitnotedmx
{

// A non-scrolling, piano-roll-style reference of the whole MIDI trigger
// vocabulary. Each vocabulary section is one COLUMN (its category name is the
// header); the 12 rows are the chromatic notes of that section's octave with
// C at the BOTTOM and B at the TOP — so the menu lines up vertically with
// Ableton's piano roll. Black-key rows are shaded and a left gutter shows the
// note letters, exactly like the piano roll.
//
// Columns (left → right): Spots & bars, Pixel zones, Chases, Breathes, Wild,
// Multicolor, then the four palette octaves (Primary C4/C5, Secondary C6/C7).
// The palette columns are narrow colour-chip strips grouped under PRIM / SEC
// headers; trigger columns are wider (label left, note name right).
//
// Cells are TOGGLES: click to latch one on, click again to release. Multiple
// latched cells combine; onSelectionChanged fires the full latched set and
// the editor forwards it to the processor's preview injector.
//
// NOTE: labels mirror the vocabulary in Composition.cpp / Recipes.h /
// Palette.h. Pitch ranges come from exported constants; the short human
// labels are maintained by hand — keep them in sync.
class TriggerMenu : public juce::Component
{
public:
    TriggerMenu();

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void resized() override;   // fills its bounds: row height scales to height

    // Fires with the currently-latched pitches whenever the set changes.
    std::function<void (const std::vector<int>&)> onSelectionChanged;

    // Pitches currently sounding over MIDI (held in MidiState). Pushed from
    // the editor's timer; these tiles light up too, so the menu doubles as a
    // live activity display. Repaints only when the set changes.
    void setLiveNotes (const std::vector<int>& heldPitches);

private:
    // One grid cell. `present` is false for notes in the octave that carry no
    // trigger (drawn as empty piano-roll slots). Array index == note offset
    // 0(C)..11(B).
    struct Cell
    {
        int          pitch   { -1 };
        juce::String label;
        juce::Colour colour;
        bool         swatch  { false };
        bool         present { false };
    };

    struct Column
    {
        juce::String          title;             // category name (trigger columns)
        juce::String          group;             // grouped header (PRIM / SEC), else ""
        int                   octave  { 0 };     // octave number shown under the title
        bool                  palette { false };
        std::array<Cell, 12>  cells;
    };

    void buildModel();
    int  pitchAt (juce::Point<int>) const noexcept;  // -1 if none / empty cell
    void toggle (int pitch);
    bool isActive (int pitch) const noexcept;

    std::vector<Column> columns;
    std::vector<int>    colX;       // per-column left edge (set in resized)
    std::vector<int>    colWid;     // per-column width
    std::vector<int>    active;     // latched (clicked) pitches
    std::vector<int>    liveActive; // pitches sounding over MIDI
    int rowH { 22 };             // chromatic-row height, scaled to fill bounds

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TriggerMenu)
};

}  // namespace hitnotedmx
