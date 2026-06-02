#include "TriggerMenu.h"

#include "Palette.h"
#include "Recipes.h"

namespace hitnotedmx
{

namespace
{
constexpr int kHeaderH    = 22;
constexpr int kRowH       = 22;
constexpr int kSwatchCols = 6;
constexpr int kSwatchH    = 26;

// Vocabulary starts that aren't exported from Composition.cpp. These
// mirror the constants there (spots 0..3, bar selectors 4..11, pixel
// statics 12..23); keep in sync if the vocabulary changes.
constexpr int kSpotStart       = 0;
constexpr int kBarSelStart     = 4;
constexpr int kPixelStaticStart = 12;

juce::Colour paletteColour (int index)
{
    const auto& c = kPalette[index];
    return juce::Colour::fromFloatRGBA (c.r, c.g, c.b, 1.0f);
}
}  // namespace


TriggerMenu::TriggerMenu()
{
    buildModel();
    layoutBlocks();
}

void TriggerMenu::buildModel()
{
    blocks.clear();

    auto header = [this] (juce::String t)
    {
        blocks.push_back ({ Block::Kind::Header, std::move (t), {}, -1, 0, 0 });
    };
    auto rows = [this] (std::vector<Item> items)
    {
        blocks.push_back ({ Block::Kind::Rows, {}, std::move (items), -1, 0, 0 });
    };
    auto swatches = [this] (int paletteStart)
    {
        blocks.push_back ({ Block::Kind::Swatches, {}, {}, paletteStart, 0, 0 });
    };

    header ("Spots");
    rows ({ { kSpotStart + 0, "Spot L  warm white" },
            { kSpotStart + 1, "Spot L  colour" },
            { kSpotStart + 2, "Spot R  warm white" },
            { kSpotStart + 3, "Spot R  colour" } });

    header ("Bars");
    rows ({ { kBarSelStart + 0, "All bars" },
            { kBarSelStart + 1, "Bar 1" },
            { kBarSelStart + 2, "Bar 2" },
            { kBarSelStart + 3, "Bar 3" },
            { kBarSelStart + 4, "Bar 4" },
            { kBarSelStart + 5, "Bars 1+2" },
            { kBarSelStart + 6, "Bars 3+4" },
            { kBarSelStart + 7, "Bars 1+4" } });

    header ("Pixel zones");
    rows ({ { kPixelStaticStart + 0,  "Zone 1" },
            { kPixelStaticStart + 1,  "Zones 2-3" },
            { kPixelStaticStart + 2,  "Zone 4" },
            { kPixelStaticStart + 3,  "Zones 5-6" },
            { kPixelStaticStart + 4,  "Zones 7-8" },
            { kPixelStaticStart + 5,  "Zone 9" },
            { kPixelStaticStart + 6,  "Zones 1-3" },
            { kPixelStaticStart + 7,  "Zones 4-6" },
            { kPixelStaticStart + 8,  "Zones 7-9" },
            { kPixelStaticStart + 9,  "Ends (1 & 9)" },
            { kPixelStaticStart + 10, "Odd zones" },
            { kPixelStaticStart + 11, "Zones 2,5,8" } });

    header ("Dynamics");
    rows ({ { kDynamicPitchStart + 0,  "Chase up" },
            { kDynamicPitchStart + 1,  "Chase down" },
            { kDynamicPitchStart + 2,  "Ping pong" },
            { kDynamicPitchStart + 3,  "Snake" },
            { kDynamicPitchStart + 4,  "Sine wave" },
            { kDynamicPitchStart + 5,  "Sparkle" },
            { kDynamicPitchStart + 6,  "Breathe" },
            { kDynamicPitchStart + 7,  "Sweep up" },
            { kDynamicPitchStart + 8,  "Sweep down" },
            { kDynamicPitchStart + 9,  "Strobe" },
            { kDynamicPitchStart + 10, "Kick pulse" },
            { kDynamicPitchStart + 11, "Alt swap" } });

    header ("Primary colours");
    swatches (kPrimaryPaletteStart);

    header ("Secondary colours");
    swatches (kSecondaryPaletteStart);

    header ("Blackout");
    rows ({ { kBlackoutNote, "Blackout (all off)" } });
}

void TriggerMenu::layoutBlocks()
{
    int y = 4;
    for (auto& b : blocks)
    {
        b.y = y;
        switch (b.kind)
        {
            case Block::Kind::Header:   b.h = kHeaderH; break;
            case Block::Kind::Rows:     b.h = static_cast<int> (b.rows.size()) * kRowH; break;
            case Block::Kind::Swatches:
            {
                const int gridRows = (kPaletteSize + kSwatchCols - 1) / kSwatchCols;
                b.h = gridRows * kSwatchH;
                break;
            }
        }
        y += b.h + 2;
    }
    totalHeight = y + 4;
}

int TriggerMenu::pitchAt (juce::Point<int> p) const noexcept
{
    for (const auto& b : blocks)
    {
        if (p.y < b.y || p.y >= b.y + b.h)
            continue;

        if (b.kind == Block::Kind::Rows)
        {
            const int idx = (p.y - b.y) / kRowH;
            if (idx >= 0 && idx < static_cast<int> (b.rows.size()))
                return b.rows[static_cast<size_t> (idx)].pitch;
            return -1;
        }
        if (b.kind == Block::Kind::Swatches)
        {
            const int cellW = juce::jmax (1, getWidth() / kSwatchCols);
            const int col = juce::jlimit (0, kSwatchCols - 1, p.x / cellW);
            const int row = (p.y - b.y) / kSwatchH;
            const int idx = row * kSwatchCols + col;
            if (idx >= 0 && idx < kPaletteSize)
                return b.paletteStart + idx;
            return -1;
        }
        return -1;  // header
    }
    return -1;
}

void TriggerMenu::setActive (int pitch)
{
    if (pitch == activePitch)
        return;
    activePitch = pitch;
    repaint();

    if (pitch >= 0) { if (onPreviewStart) onPreviewStart (pitch); }
    else            { if (onPreviewStop)  onPreviewStop(); }
}

void TriggerMenu::mouseDown (const juce::MouseEvent& e) { setActive (pitchAt (e.getPosition())); }
void TriggerMenu::mouseDrag (const juce::MouseEvent& e) { setActive (pitchAt (e.getPosition())); }
void TriggerMenu::mouseUp   (const juce::MouseEvent&)   { setActive (-1); }

void TriggerMenu::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff262626));
    const int w = getWidth();

    for (const auto& b : blocks)
    {
        if (b.kind == Block::Kind::Header)
        {
            g.setColour (juce::Colour (0xff9a9a9a));
            g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
            g.drawText (b.title.toUpperCase(), 8, b.y, w - 16, b.h,
                        juce::Justification::centredLeft);
        }
        else if (b.kind == Block::Kind::Rows)
        {
            g.setFont (juce::FontOptions (12.0f));
            for (size_t i = 0; i < b.rows.size(); ++i)
            {
                const auto& it = b.rows[i];
                const int ry = b.y + static_cast<int> (i) * kRowH;
                const auto rb = juce::Rectangle<int> (4, ry, w - 8, kRowH - 2);
                const bool on = (it.pitch == activePitch);

                g.setColour (on ? juce::Colour (0xff3a6ea5) : juce::Colour (0xff303030));
                g.fillRoundedRectangle (rb.toFloat(), 3.0f);
                g.setColour (on ? juce::Colours::white : juce::Colours::lightgrey);
                g.drawText (it.label, rb.getX() + 8, rb.getY(), rb.getWidth() - 56, rb.getHeight(),
                            juce::Justification::centredLeft);
                g.setColour (juce::Colour (0xff707070));
                g.setFont (juce::FontOptions (10.0f));
                g.drawText (juce::String (it.pitch), rb.getRight() - 40, rb.getY(), 34, rb.getHeight(),
                            juce::Justification::centredRight);
                g.setFont (juce::FontOptions (12.0f));
            }
        }
        else  // Swatches
        {
            const int cellW = juce::jmax (1, w / kSwatchCols);
            for (int idx = 0; idx < kPaletteSize; ++idx)
            {
                const int col = idx % kSwatchCols;
                const int row = idx / kSwatchCols;
                const auto cell = juce::Rectangle<int> (col * cellW, b.y + row * kSwatchH,
                                                        cellW - 3, kSwatchH - 3);
                g.setColour (paletteColour (idx));
                g.fillRect (cell);

                const bool on = (b.paletteStart + idx == activePitch);
                g.setColour (on ? juce::Colours::white : juce::Colour (0x40ffffff));
                g.drawRect (cell, on ? 2 : 1);
            }
        }
    }
}

}  // namespace hitnotedmx
