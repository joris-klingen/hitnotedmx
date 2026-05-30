#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "Composition.h"  // DmxValues

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
// Tearing on individual cells is acceptable at 30 Hz repaint rate —
// it's a visualiser, not the source of truth.
class DmxVisualizer : public juce::Component
{
public:
    explicit DmxVisualizer (const DmxValues& valuesRef) : values (valuesRef) {}

    void paint (juce::Graphics&) override;

private:
    const DmxValues& values;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DmxVisualizer)
};

}  // namespace hitnotedmx
