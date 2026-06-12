#pragma once

#include <juce_core/juce_core.h>

namespace hitnotedmx
{

// Ships the showcase assets with the app. The named trigger rack is embedded
// in the binary (it can't be regenerated at runtime) and installed into the
// Ableton User Library so it shows up under MIDI Effects. The demo MIDI clips
// are a curated set of layered "combos" generated at runtime from the live
// vocabulary constants (so they never drift from the recipes / palette).
namespace Showcase
{
    // Default location for the demo clips: ~/Music/HitNoteDmx Showcase
    juce::File defaultRoot();

    // "Init. names": install the named trigger rack into the Ableton User
    // Library (Presets/MIDI Effects/MIDI Effect Rack), where Live indexes it.
    // Idempotent. Returns the installed rack file.
    juce::File installRack();

    // "Show clips": write the demo combo clips under `root` and return the
    // MIDI Clips folder (for the caller to reveal in Finder). Idempotent.
    juce::File writeClips (const juce::File& root);
}

}  // namespace hitnotedmx
