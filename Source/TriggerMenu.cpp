#include "TriggerMenu.h"

#include "Palette.h"
#include "Recipes.h"

#include <algorithm>

namespace hitnotedmx
{

namespace
{
constexpr int kHeaderH   = 16;
constexpr int kRowH      = 22;
constexpr int kRowCols   = 4;
constexpr int kSwatchH   = 26;
constexpr int kSwatchCols = 12;

// Vocabulary starts not exported from Composition.cpp — mirror them here.
constexpr int kSpotStart        = 0;
constexpr int kBarSelStart      = 4;
constexpr int kPixelStaticStart = 12;

// Note name, C3 = note 60 (Ableton convention, matches the MIDI log).
juce::String noteName (int pitch)
{
    static const char* names[] = { "C", "C#", "D", "D#", "E", "F",
                                   "F#", "G", "G#", "A", "A#", "B" };
    const int octave = pitch / 12 - 2;  // 60 -> C3, 0 -> C-2
    return juce::String (names[pitch % 12]) + juce::String (octave);
}

juce::Colour paletteColour (int index)
{
    const auto& c = kPalette[index];
    return juce::Colour::fromFloatRGBA (c.r, c.g, c.b, 1.0f);
}
}  // namespace


TriggerMenu::TriggerMenu()
{
    buildModel();
}

void TriggerMenu::buildModel()
{
    blocks.clear();

    auto rowsBlock = [this] (juce::String title, std::vector<Item> items)
    {
        blocks.push_back ({ std::move (title), std::move (items), kRowCols, kRowH, false, 0, 0 });
    };
    auto swatchBlock = [this] (juce::String title, int paletteStart)
    {
        std::vector<Item> items;
        for (int i = 0; i < kPaletteSize; ++i)
            items.push_back ({ paletteStart + i, noteName (paletteStart + i),
                               paletteColour (i), true });
        blocks.push_back ({ std::move (title), std::move (items), kSwatchCols, kSwatchH, true, 0, 0 });
    };
    auto row = [] (int pitch, juce::String label) -> Item
    {
        return { pitch, std::move (label), {}, false };
    };

    rowsBlock ("Spots", {
        row (kSpotStart + 0, "Spot L WW"), row (kSpotStart + 1, "Spot L col"),
        row (kSpotStart + 2, "Spot R WW"), row (kSpotStart + 3, "Spot R col") });

    rowsBlock ("Bars", {
        row (kBarSelStart + 0, "All bars"),
        row (kBarSelStart + 1, "Bar 1"), row (kBarSelStart + 2, "Bar 2"),
        row (kBarSelStart + 3, "Bar 3"), row (kBarSelStart + 4, "Bar 4") });

    rowsBlock ("Pixel zones", {
        row (kPixelStaticStart + 0, "Zone 1"), row (kPixelStaticStart + 1, "Zone 2"),
        row (kPixelStaticStart + 2, "Zone 3"), row (kPixelStaticStart + 3, "Zone 4"),
        row (kPixelStaticStart + 4, "Zone 5"), row (kPixelStaticStart + 5, "Zone 6"),
        row (kPixelStaticStart + 6, "Zone 7"), row (kPixelStaticStart + 7, "Zone 8"),
        row (kPixelStaticStart + 8, "Zone 9") });

    // Dynamics, grouped by feel. Pitches are unchanged from the flat
    // vocabulary (24..35) — only the menu presentation is grouped. A future
    // "Multicolor" block lands here with the self-coloured recipes (TODO #5).
    rowsBlock ("Chases", {
        row (kDynamicPitchStart + 0,  "Chase up"),  row (kDynamicPitchStart + 1,  "Chase dn"),
        row (kDynamicPitchStart + 2,  "Ping-pong"), row (kDynamicPitchStart + 3,  "Snake"),
        row (kDynamicPitchStart + 7,  "Sweep up"),  row (kDynamicPitchStart + 8,  "Sweep dn") });

    rowsBlock ("Breathes", {
        row (kDynamicPitchStart + 4,  "Sine"),      row (kDynamicPitchStart + 6,  "Breathe"),
        row (kDynamicPitchStart + 10, "Kick") });

    rowsBlock ("Wild", {
        row (kDynamicPitchStart + 5,  "Sparkle"),   row (kDynamicPitchStart + 9,  "Strobe"),
        row (kDynamicPitchStart + 11, "Alt swap") });

    swatchBlock ("Primary colours",   kPrimaryPaletteStart);
    swatchBlock ("Secondary colours", kSecondaryPaletteStart);

    rowsBlock ("Blackout", { row (kBlackoutNote, "Blackout") });
}

void TriggerMenu::layoutForWidth (int width)
{
    laidOutWidth = width;
    int y = 4;
    for (auto& b : blocks)
    {
        b.y = y;
        const int gridRows = (static_cast<int> (b.items.size()) + b.cols - 1) / b.cols;
        b.h = kHeaderH + gridRows * b.cellH;
        y += b.h + 6;
    }
    totalHeight = y + 4;
    setSize (width, totalHeight);
}

int TriggerMenu::pitchAt (juce::Point<int> p) const noexcept
{
    const int w = getWidth();
    for (const auto& b : blocks)
    {
        if (p.y < b.y || p.y >= b.y + b.h)
            continue;
        const int gy = p.y - (b.y + kHeaderH);
        if (gy < 0)
            return -1;  // on the header

        const int cellW = juce::jmax (1, w / b.cols);
        const int col = juce::jlimit (0, b.cols - 1, p.x / cellW);
        const int rowIdx = gy / b.cellH;
        const int idx = rowIdx * b.cols + col;
        if (idx >= 0 && idx < static_cast<int> (b.items.size()))
            return b.items[static_cast<size_t> (idx)].pitch;
        return -1;
    }
    return -1;
}

bool TriggerMenu::isActive (int pitch) const noexcept
{
    return std::find (active.begin(), active.end(), pitch) != active.end();
}

void TriggerMenu::toggle (int pitch)
{
    if (pitch < 0)
        return;
    auto it = std::find (active.begin(), active.end(), pitch);
    if (it != active.end()) active.erase (it);
    else                    active.push_back (pitch);

    repaint();
    if (onSelectionChanged)
        onSelectionChanged (active);
}

void TriggerMenu::mouseDown (const juce::MouseEvent& e)
{
    toggle (pitchAt (e.getPosition()));
}

void TriggerMenu::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff262626));
    const int w = getWidth();

    for (const auto& b : blocks)
    {
        // Section header.
        g.setColour (juce::Colour (0xff9a9a9a));
        g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
        g.drawText (b.title.toUpperCase(), 6, b.y, w - 12, kHeaderH,
                    juce::Justification::centredLeft);

        const int cellW = juce::jmax (1, w / b.cols);
        const int gridTop = b.y + kHeaderH;

        for (size_t i = 0; i < b.items.size(); ++i)
        {
            const auto& it = b.items[i];
            const int col = static_cast<int> (i) % b.cols;
            const int rowIdx = static_cast<int> (i) / b.cols;
            const auto cell = juce::Rectangle<int> (col * cellW, gridTop + rowIdx * b.cellH,
                                                    cellW, b.cellH).reduced (2, 1);
            const bool on = isActive (it.pitch);

            if (b.swatches)
            {
                g.setColour (it.colour);
                g.fillRect (cell);
                g.setColour (on ? juce::Colours::white : juce::Colour (0x40ffffff));
                g.drawRect (cell, on ? 2 : 1);

                // Tiny note name, readable on both light and dark swatches.
                const bool light = it.colour.getPerceivedBrightness() > 0.5f;
                g.setColour (light ? juce::Colours::black.withAlpha (0.7f)
                                   : juce::Colours::white.withAlpha (0.8f));
                g.setFont (juce::FontOptions (8.5f));
                g.drawText (it.label, cell, juce::Justification::centredBottom);
            }
            else
            {
                g.setColour (on ? juce::Colour (0xff3a6ea5) : juce::Colour (0xff303030));
                g.fillRoundedRectangle (cell.toFloat(), 3.0f);

                auto text = cell.reduced (5, 0);
                const auto noteStr = noteName (it.pitch);
                // Reserve right side for note name, give rest to label.
                constexpr int kNoteW = 28;
                auto noteArea  = text.removeFromRight (kNoteW);
                g.setColour (on ? juce::Colours::white : juce::Colour (0xff8fb6dd));
                g.setFont (juce::FontOptions (9.5f, juce::Font::bold));
                g.drawText (it.label, text, juce::Justification::centredLeft, true);
                g.setColour (on ? juce::Colours::white.withAlpha (0.6f) : juce::Colour (0xff666666));
                g.setFont (juce::FontOptions (8.5f));
                g.drawText (noteStr, noteArea, juce::Justification::centredRight);
            }
        }
    }
}

}  // namespace hitnotedmx
