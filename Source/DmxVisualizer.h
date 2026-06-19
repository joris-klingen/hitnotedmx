#pragma once

#include <array>
#include <cstdint>
#include <juce_gui_basics/juce_gui_basics.h>

#include "Composition.h"  // DmxValues
#include "Rig.h"

namespace hitnotedmx
{

// On-screen DMX output preview.
//
// Architecture: the visualiser keeps a cached juce::Image that mirrors
// what it would draw if it were repainted right now. paint() is a
// single g.drawImageAt() blit. repaintIfChanged() builds an 8-bit
// fingerprint of the rig footprint and, only when it differs from
// what's currently in the cache, redraws the affected cells into the
// cached image and calls repaint(). Live's compositor still has work
// to do on each repaint, but the rasterisation cost (72 fillRects +
// text + spot ellipses) is amortised across long static stretches.
//
// Layout:
//   2 RGBW spots across the top, then 4 vertical bars below them (each
//   18 cells tall, pixel 1 at the bottom matching the rig's bottom-up
//   orientation).
class DmxVisualizer : public juce::Component,
                      private juce::Timer
{
public:
    DmxVisualizer (const DmxValues& valuesRef, const SelectionMask& selectionRef);

    void paint (juce::Graphics&) override;
    void resized() override;

    // Build the rig fingerprint; if it differs from the cached image,
    // re-rasterise the image and schedule a repaint. Called from the
    // editor's GUI timer.
    void repaintIfChanged();

    // Mirror the driver's strobe shutter on screen. `hz` is the repeat rate
    // (0 = off). A dedicated timer at the send rate keeps the preview lit for
    // a single tick per period and black for the rest, matching the driver's
    // one-frame flash; the gap grows as hz drops. The preview is NOT
    // phase-locked to the real DMX output — it just conveys the strobe.
    void setStrobe (float hz);

private:
    void timerCallback() override;
    void rebuildCache();

    static constexpr int kFingerprintSize =
        kNumBars * kPixelsPerBar * 3 + kNumSpots * 6   // rig DMX bytes
      + kNumBars * kPixelsPerBar;                       // + 1 selection byte/cell

    const DmxValues&     values;
    const SelectionMask& selection;

    juce::Image cachedImage;  // ARGB; fully covers the component bounds
    std::array<std::uint8_t, kFingerprintSize> lastFingerprint {};

    int           strobePeriod { 0 };  // frames per flash period (0 = strobe off)
    std::uint64_t strobeFrame  { 0 };  // preview send-frame counter
    bool          strobeDark   { false };  // current flash phase (true = blanked)

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DmxVisualizer)
};

}  // namespace hitnotedmx
