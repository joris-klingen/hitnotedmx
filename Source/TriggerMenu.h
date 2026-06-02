#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <vector>

namespace hitnotedmx
{

// A scrollable reference list of the whole MIDI trigger vocabulary —
// spots, bar selectors, pixel-zone statics, dynamic recipes, the two
// palette swatch grids, and blackout. Press-and-hold a row or swatch
// (or drag across them) to preview it live on the rig visualiser; the
// editor wires onPreviewStart/onPreviewStop to the processor's preview
// injector. Sized taller than its viewport — drop it in a juce::Viewport.
//
// NOTE: the labels here mirror the vocabulary defined in Composition.cpp
// / Recipes.h / Palette.h. Pitch *ranges* come from the exported
// constants; the human labels are maintained by hand, so keep them in
// sync if the vocabulary changes.
class TriggerMenu : public juce::Component
{
public:
    TriggerMenu();

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp   (const juce::MouseEvent&) override;

    // Total content height for the host Viewport to size us to.
    int preferredHeight() const noexcept { return totalHeight; }

    // Called with the MIDI pitch to preview while pressed, and on release.
    std::function<void (int pitch)> onPreviewStart;
    std::function<void()>           onPreviewStop;

private:
    struct Item { int pitch; juce::String label; };

    struct Block
    {
        enum class Kind { Header, Rows, Swatches };
        Kind kind;
        juce::String  title;        // Header / section caption
        std::vector<Item> rows;     // Rows
        int paletteStart { -1 };    // Swatches (24 colours from kPalette)
        int y { 0 };
        int h { 0 };
    };

    void buildModel();
    void layoutBlocks();
    int  pitchAt (juce::Point<int>) const noexcept;  // -1 if none
    void setActive (int pitch);

    std::vector<Block> blocks;
    int totalHeight { 0 };
    int activePitch { -1 };   // currently previewing / highlighted

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TriggerMenu)
};

}  // namespace hitnotedmx
