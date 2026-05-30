#pragma once

#include <array>
#include <juce_gui_basics/juce_gui_basics.h>

#include "Composition.h"  // DmxValues
#include "Rig.h"

namespace hitnotedmx
{

// On-screen DMX output preview. Lays the rig out as it actually sits
// on stage:
//
//   4 vertical bars side-by-side (each 9 cells tall, pixel 1 at the
//   bottom matching the rig's bottom-up orientation) + 2 RGBW spots
//   below the bars.
//
// Reads DmxValues directly from the audio thread's shared buffer.
// Tearing on individual cells is acceptable at the timer's repaint
// rate — it's a visualiser, not the source of truth.
//
// `repaintIfChanged()` is called from the editor's timer instead of
// invoking repaint() unconditionally; the visualiser quantises the
// rig footprint into a small fingerprint and skips repaint when it
// matches the last drawn frame. Live's plug-in window compositor was
// chewing GPU on every 30 Hz repaint of 36 cells + 2 spots even when
// nothing on stage was actually changing.
class DmxVisualizer : public juce::Component
{
public:
    explicit DmxVisualizer (const DmxValues& valuesRef)
        : values (valuesRef)
    {
        setOpaque (true);  // we fill the entire bounds in paint()
        lastFingerprint.fill (0);
    }

    void paint (juce::Graphics&) override;

    // Compare current DmxValues to the previously-painted frame; only
    // schedule a repaint if anything in the rig footprint moved.
    void repaintIfChanged();

private:
    static constexpr int kFingerprintSize =
        kNumBars * kPixelsPerBar * 3 + kNumSpots * 6;  // 120

    const DmxValues& values;
    std::array<std::uint8_t, kFingerprintSize> lastFingerprint;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DmxVisualizer)
};

}  // namespace hitnotedmx
