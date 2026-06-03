#pragma once

#include <array>
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
    explicit DmxVisualizer (const DmxValues& valuesRef);

    void paint (juce::Graphics&) override;
    void resized() override;

    // Build the rig fingerprint; if it differs from the cached image,
    // re-rasterise the image and schedule a repaint. Called from the
    // editor's GUI timer.
    void repaintIfChanged();

    // Mirror the driver's strobe shutter on screen. While active, a
    // dedicated timer flashes the preview to black on alternate ticks at
    // the strobe frequency; otherwise the timer is stopped (zero cost).
    // The preview is NOT phase-locked to the real DMX output (different
    // clocks) — it just conveys that a strobe is running.
    void setStrobeActive (bool active);

private:
    void timerCallback() override;
    void rebuildCache();

    static constexpr int kFingerprintSize =
        kNumBars * kPixelsPerBar * 3 + kNumSpots * 6;  // 228 bytes

    const DmxValues& values;

    juce::Image cachedImage;  // ARGB; fully covers the component bounds
    std::array<std::uint8_t, kFingerprintSize> lastFingerprint {};

    bool strobeActive { false };  // is the strobe-flash timer running?
    bool strobeDark   { false };  // current flash phase (true = blanked)

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DmxVisualizer)
};

}  // namespace hitnotedmx
