#include "DmxVisualizer.h"

#include "Recipes.h"  // kStrobeHz
#include "Rig.h"

namespace hitnotedmx
{

namespace
{
juce::uint8 toByte (float v) noexcept
{
    const int n = juce::jlimit (0, 255, static_cast<int> (v * 255.0f + 0.5f));
    return static_cast<juce::uint8> (n);
}

// Composite an RGBW + dimmer cell down to a screen RGB.
// White is added with a warm tilt so it visually reads like the
// `white=1, r=0.4, g=0.15` warm-white tint the recipe layer uses.
juce::Colour rgbwToScreen (float r, float g, float b, float w, float dim) noexcept
{
    const float dr = juce::jlimit (0.0f, 1.0f, (r + w * 1.0f)  * dim);
    const float dg = juce::jlimit (0.0f, 1.0f, (g + w * 0.85f) * dim);
    const float db = juce::jlimit (0.0f, 1.0f, (b + w * 0.70f) * dim);
    return juce::Colour::fromRGB (toByte (dr), toByte (dg), toByte (db));
}
}

DmxVisualizer::DmxVisualizer (const DmxValues& valuesRef)
    : values (valuesRef)
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
    if (strobeActive && strobeDark)
        g.fillAll (juce::Colour (0xff141414));   // strobe blank: rig dark, panel bg unchanged
    else if (cachedImage.isValid())
        g.drawImageAt (cachedImage, 0, 0);
    else
        g.fillAll (juce::Colour (0xff141414));
}

void DmxVisualizer::setStrobeActive (bool active)
{
    if (active == strobeActive)
        return;

    strobeActive = active;
    if (active)
    {
        // Toggle twice per visible cycle, so the on/off rate matches the
        // strobe frequency. 20 Hz max → 40 Hz timer, well under the display
        // refresh; the callback is a single blit so cost is negligible.
        strobeDark = false;
        startTimerHz (juce::jmax (1, juce::roundToInt (2.0 * kStrobeHz)));
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
    strobeDark = ! strobeDark;
    repaint();
}

void DmxVisualizer::rebuildCache()
{
    if (! cachedImage.isValid())
        return;

    const int w = cachedImage.getWidth();
    juce::Graphics g (cachedImage);

    g.fillAll (juce::Colour (0xff141414));

    const int barW       = 32;   // ~2:1 cells (was 64); spots no longer stack on top
    const int barSpacing = 10;
    const int cellH      = 16;
    const int spotSize   = 52;
    const int spotGap    = 26;   // breathing room between a spot and the grid

    const int barsTotalW = kNumBars * barW + (kNumBars - 1) * barSpacing;

    // Whole rig laid out horizontally: spot_l | bar grid | spot_r, centred.
    const int rigW    = spotSize + spotGap + barsTotalW + spotGap + spotSize;
    const int rigX    = (w - rigW) / 2;
    const int originY = 8;                          // top of the bars
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
            g.setColour (juce::Colour::fromRGB (toByte (r), toByte (gv), toByte (b)));
            g.fillRect (x, originY + row * cellH, barW, cellH - 2);
        }
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

    if (current == lastFingerprint)
        return;
    lastFingerprint = current;
    rebuildCache();
    repaint();
}

}  // namespace hitnotedmx
