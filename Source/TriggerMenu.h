#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <vector>

namespace hitnotedmx
{

// A non-scrolling, multi-column reference of the whole MIDI trigger
// vocabulary — spots, bar selectors, pixel zones, dynamic recipes, the
// two palette swatch grids, and blackout. Each cell is labelled with its
// MIDI note name (C3 = note 60, matching Ableton + the MIDI log).
//
// Cells are TOGGLES: click to latch one on, click again to release.
// Multiple latched cells combine, so you can audition e.g. "All bars" +
// a colour + a chase at once. onSelectionChanged fires the full latched
// set; the editor forwards it to the processor's preview injector.
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

    int preferredHeight() const noexcept { return totalHeight; }
    void layoutForWidth (int width);  // recompute block geometry + height

    // Fires with the currently-latched pitches whenever the set changes.
    std::function<void (const std::vector<int>&)> onSelectionChanged;

private:
    struct Item { int pitch; juce::String label; juce::Colour colour; bool swatch; };

    struct Block
    {
        juce::String      title;
        std::vector<Item> items;
        int  cols;
        int  cellH;
        bool swatches;
        int  y { 0 };
        int  h { 0 };
    };

    void buildModel();
    int  pitchAt (juce::Point<int>) const noexcept;  // -1 if none
    void toggle (int pitch);
    bool isActive (int pitch) const noexcept;

    std::vector<Block> blocks;
    std::vector<int>   active;   // latched pitches
    int totalHeight { 0 };
    int laidOutWidth { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TriggerMenu)
};

}  // namespace hitnotedmx
