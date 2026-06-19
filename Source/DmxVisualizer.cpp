#include "DmxVisualizer.h"

#include <cmath>

#include "Rig.h"

namespace hitnotedmx
{

namespace
{
// The fixtures' own dimming curve makes low DMX values already fairly bright,
// so a straight value→pixel map reads far too dark on screen — 30% looks
// near-black while the real lights are clearly on. Lift the lows with a gamma
// (~0.45, an sRGB-ish encode) so the preview tracks PERCEIVED output, not the
// raw DMX byte. Applied only at display; the DMX values themselves are linear.
constexpr float kVizGamma = 0.45f;

float displayCurve (float v) noexcept
{
    return v <= 0.0f ? 0.0f : std::pow (v, kVizGamma);
}

juce::uint8 toByte (float v) noexcept
{
    const int n = juce::jlimit (0, 255, static_cast<int> (v * 255.0f + 0.5f));
    return static_cast<juce::uint8> (n);
}

// Brightness-curved screen byte for a single linear channel value.
juce::uint8 toScreenByte (float v) noexcept
{
    return toByte (displayCurve (v));
}

// Composite an RGBW + dimmer cell down to a screen RGB.
// White is added with a warm tilt so it visually reads like the
// `white=1, r=0.4, g=0.15` warm-white tint the recipe layer uses.
juce::Colour rgbwToScreen (float r, float g, float b, float w, float dim) noexcept
{
    const float dr = juce::jlimit (0.0f, 1.0f, (r + w * 1.0f)  * dim);
    const float dg = juce::jlimit (0.0f, 1.0f, (g + w * 0.85f) * dim);
    const float db = juce::jlimit (0.0f, 1.0f, (b + w * 0.70f) * dim);
    return juce::Colour::fromRGB (toScreenByte (dr), toScreenByte (dg), toScreenByte (db));
}

// Small "dNNN" start-address caption, centred under a fixture of width
// `fixtureW` whose bottom edge sits at `topY`. Drawn on the dark panel
// background, so a fixed dim grey reads cleanly.
void drawAddress (juce::Graphics& g, int address, int x, int topY, int fixtureW) noexcept
{
    g.setColour (juce::Colour (0xff808080));
    g.setFont (juce::FontOptions (10.5f));
    g.drawText ("d" + juce::String (address).paddedLeft ('0', 3),
                x, topY + 2, fixtureW, 12, juce::Justification::centred, false);
}
}

DmxVisualizer::DmxVisualizer (const DmxValues& valuesRef, const SelectionMask& selectionRef)
    : values (valuesRef), selection (selectionRef)
{
    setOpaque (true);
    lastFingerprint.fill (0);
}

void DmxVisualizer::resized()
{
    // Allocate the image once per size change. rebuildCache() reuses
    // this buffer — the previous version allocated a fresh ~1 MB Image
    // on every state change, which made the GUI visibly glitchy under
    // a running chase after a few loop iterations as the allocator
    // started fragmenting.
    const int w = getWidth();
    const int h = getHeight();
    if (w > 0 && h > 0)
    {
        // clearImage=false — rebuildCache fills every pixel itself.
        cachedImage = juce::Image (juce::Image::RGB, w, h, false);
    }
    else
    {
        cachedImage = juce::Image();
    }
    rebuildCache();
}

void DmxVisualizer::paint (juce::Graphics& g)
{
    if (strobePeriod > 0 && strobeDark)
        g.fillAll (juce::Colour (0xff141414));   // strobe blank: rig dark, panel bg unchanged
    else if (cachedImage.isValid())
        g.drawImageAt (cachedImage, 0, 0);
    else
        g.fillAll (juce::Colour (0xff141414));
}

void DmxVisualizer::setStrobe (float hz)
{
    // One lit frame per period, the rest black — same shape as the driver
    // shutter (EnttecProDmx::sendDmxFrame). The timer runs at the send rate
    // so the lit flash is a single tick; the period sets the black gap.
    constexpr double kSendRateHz = 40.0;   // matches EnttecProDmx::kSendRateHz
    const int period = hz > 0.0f
                     ? juce::jmax (2, juce::roundToInt (kSendRateHz / (double) hz))
                     : 0;

    if (period == strobePeriod)
        return;

    strobePeriod = period;
    if (period > 0)
    {
        strobeFrame = 0;
        strobeDark  = false;
        startTimerHz (juce::roundToInt (kSendRateHz));
    }
    else
    {
        stopTimer();
        strobeDark = false;
        repaint();   // restore the steady lit frame
    }
}

void DmxVisualizer::timerCallback()
{
    ++strobeFrame;
    const bool dark = strobePeriod > 0
                   && (strobeFrame % static_cast<std::uint64_t> (strobePeriod)) != 0ull;
    if (dark != strobeDark)
    {
        strobeDark = dark;
        repaint();
    }
}

void DmxVisualizer::rebuildCache()
{
    if (! cachedImage.isValid())
        return;

    const int w = cachedImage.getWidth();
    const int h = cachedImage.getHeight();
    juce::Graphics g (cachedImage);

    g.fillAll (juce::Colour (0xff141414));

    const int barW       = 32;   // ~2:1 cells (was 64); spots no longer stack on top
    const int barSpacing = 10;
    const int spotSize   = 52;
    const int spotGap    = 26;   // breathing room between a spot and the grid
    const int topPad     = 6;    // inset above the rig (≈ the cards' top inset)
    const int bottomPad  = 2;    // inset below the captions
    const int labelH     = 14;   // reserved under each fixture for the "dNNN" caption

    // The bar grid fills the FULL pane height: top flush with the cards' top,
    // captions flush with the cards' bottom. Row edges are interpolated across
    // the available height so the rounding remainder is spread one pixel at a
    // time over the rows (no pooled margin at either end) — `rowY(r)` is the
    // top of screen-row r, and the grid bottom (rowY(nPix)) carries the
    // captions.
    const int originY = topPad;
    const int gridH   = juce::jmax (kPixelsPerBar * 4, h - topPad - labelH - bottomPad);
    auto rowY = [originY, gridH] (int screenRow)
    {
        return originY + (gridH * screenRow) / kPixelsPerBar;
    };

    const int barsTotalW = kNumBars * barW + (kNumBars - 1) * barSpacing;

    // Whole rig laid out horizontally: spot_l | bar grid | spot_r, centred.
    const int rigW    = spotSize + spotGap + barsTotalW + spotGap + spotSize;
    const int rigX    = (w - rigW) / 2;
    const int barsX   = rigX + spotSize + spotGap;  // left edge of bar 1

    // ---- Spots flanking the grid, top-aligned with the bars -------------
    const int spotY = originY;
    const int spotX[kNumSpots] = { rigX, barsX + barsTotalW + spotGap };

    for (int s = 0; s < kNumSpots; ++s)
    {
        const auto& spot = kSpots[s];
        const float dim = values.get (spot.dimmer());
        const float r   = values.get (spot.red());
        const float gv  = values.get (spot.green());
        const float b   = values.get (spot.blue());
        const float ww  = values.get (spot.white());

        const int x = spotX[s];
        g.setColour (rgbwToScreen (r, gv, b, ww, dim));
        g.fillEllipse (static_cast<float> (x), static_cast<float> (spotY),
                       static_cast<float> (spotSize), static_cast<float> (spotSize));

        // Start DMX address, small, under the fixture.
        drawAddress (g, spot.dimmer(), x, spotY + spotSize, spotSize);
    }

    // ---- Bars (unlabelled) ----------------------------------------------
    for (int barIdx = 0; barIdx < kNumBars; ++barIdx)
    {
        const auto& bar = kBars[barIdx];
        const int x = barsX + barIdx * (barW + barSpacing);

        for (int pixel = 1; pixel <= bar.pixels; ++pixel)
        {
            const auto ch  = bar.channelsFor (pixel);
            const float r  = values.get (ch[0]);
            const float gv = values.get (ch[1]);
            const float b  = values.get (ch[2]);

            const int row = bar.pixels - pixel;  // pixel 1 at the bottom
            const int y0  = rowY (row);
            const int y1  = rowY (row + 1);
            const int cellH = juce::jmax (1, y1 - y0 - 2);
            g.setColour (juce::Colour::fromRGB (toScreenByte (r), toScreenByte (gv), toScreenByte (b)));
            g.fillRect (x, y0, barW, cellH);

            // "Armed but unlit": a held selector covers this cell but nothing
            // is lighting it. Show a grey outline so the operator sees the
            // selection even though no DMX is sent for it.
            if (selection.cell[static_cast<size_t> (barIdx)][static_cast<size_t> (pixel)])
            {
                g.setColour (juce::Colour (0xff6a6a6a));
                g.drawRect (x, y0, barW, cellH, 1);
            }
        }

        // Start DMX address, small, under the bar (flush with the pane bottom).
        drawAddress (g, bar.dmxStart, x, rowY (bar.pixels), barW);
    }
}

void DmxVisualizer::repaintIfChanged()
{
    // 8-bit fingerprint of every rig channel — quantising matches the
    // bytes the renderer ultimately uses, so sub-byte float jitter
    // never triggers an unnecessary re-rasterisation.
    std::array<std::uint8_t, kFingerprintSize> current {};
    int i = 0;
    for (int b = 0; b < kNumBars; ++b)
    {
        const auto& bar = kBars[b];
        for (int p = 1; p <= bar.pixels; ++p)
        {
            const auto ch = bar.channelsFor (p);
            current[i++] = toByte (values.get (ch[0]));
            current[i++] = toByte (values.get (ch[1]));
            current[i++] = toByte (values.get (ch[2]));
        }
    }
    for (int s = 0; s < kNumSpots; ++s)
    {
        const auto& spot = kSpots[s];
        current[i++] = toByte (values.get (spot.dimmer()));
        current[i++] = toByte (values.get (spot.red()));
        current[i++] = toByte (values.get (spot.green()));
        current[i++] = toByte (values.get (spot.blue()));
        current[i++] = toByte (values.get (spot.white()));
        current[i++] = toByte (values.get (spot.strobe()));
    }
    // Selection outlines: the rig is black in the armed-but-unlit state, so the
    // DMX bytes above don't capture a selection change — fingerprint it too.
    for (int b = 0; b < kNumBars; ++b)
        for (int p = 1; p <= kBars[b].pixels; ++p)
            current[i++] = selection.cell[static_cast<size_t> (b)][static_cast<size_t> (p)] ? 1 : 0;

    if (current == lastFingerprint)
        return;
    lastFingerprint = current;
    rebuildCache();
    repaint();
}

}  // namespace hitnotedmx
