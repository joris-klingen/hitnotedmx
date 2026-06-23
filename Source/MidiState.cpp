#include "MidiState.h"

namespace hitnotedmx
{

void MidiState::noteOn (std::uint8_t pitch,
                        std::uint8_t channel,
                        std::uint8_t velocity,
                        double atBeat) noexcept
{
    if (pitch >= kNumPitches)
        return;
    notes[pitch] = HeldNote { true, channel, velocity, atBeat };
}

void MidiState::noteOff (std::uint8_t pitch) noexcept
{
    if (pitch >= kNumPitches)
        return;
    notes[pitch].active = false;
}

void MidiState::clear() noexcept
{
    for (auto& n : notes)
        n.active = false;
}

void MidiState::setToUnion (const MidiState& primary, const MidiState& secondary) noexcept
{
    for (int p = 0; p < kNumPitches; ++p)
        notes[p] = primary.notes[p].active ? primary.notes[p]
                                           : secondary.notes[p];
}

bool MidiState::anyHeld() const noexcept
{
    for (const auto& n : notes)
        if (n.active)
            return true;
    return false;
}

}  // namespace hitnotedmx
