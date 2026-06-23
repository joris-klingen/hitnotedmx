#pragma once

#include <array>
#include <cstdint>

namespace hitnotedmx
{

// Per-pitch held-note tracker. We only ever care about which notes are
// CURRENTLY held — the recipes look at the live set, not a historical
// timeline — so the state collapses to a fixed-size array indexed by
// pitch.
//
// All operations are O(1) (no allocations, no locking). Updated by the
// audio thread inside processBlock; read by both the audio thread (to
// compute composition) and, in later commits, possibly by the GUI for
// diagnostic display (cheap atomic reads).

struct HeldNote
{
    bool   active   { false };
    std::uint8_t channel  { 1 };   // MIDI channel 1..16
    std::uint8_t velocity { 0 };   // 1..127
    double startBeat { 0.0 };
};

class MidiState
{
public:
    static constexpr int kNumPitches = 128;

    // Called from the audio thread for each note-on the host gives us.
    // Overwrites any prior unmatched note-on at the same pitch (Live can
    // do this if the upstream sequencer fires two on-events back to back).
    void noteOn (std::uint8_t pitch, std::uint8_t channel,
                 std::uint8_t velocity, double atBeat) noexcept;

    // Called from the audio thread for each note-off. Releases the pitch
    // regardless of channel — duplicate-pitch tracking across channels is
    // not something the MIDI vocabulary cares about.
    void noteOff (std::uint8_t pitch) noexcept;

    // Drop everything. Called on prepareToPlay / reset / sample rate change.
    void clear() noexcept;

    // Rebuild this state as the per-pitch union of two sources: `primary` wins
    // when it holds a pitch, otherwise `secondary`. Lets live MIDI and the
    // click-preview be tracked in SEPARATE states — so a live note-off can't
    // clear a preview-held slot (or vice-versa) — and merged just before
    // composition. O(kNumPitches), no allocations.
    void setToUnion (const MidiState& primary, const MidiState& secondary) noexcept;

    bool isActive (std::uint8_t pitch) const noexcept
    {
        return notes[pitch].active;
    }

    const HeldNote& get (std::uint8_t pitch) const noexcept
    {
        return notes[pitch];
    }

    // Walk every held note. Useful for the recipe composition pass.
    template <typename Fn>
    void forEachHeld (Fn&& fn) const
    {
        for (int p = 0; p < kNumPitches; ++p)
            if (notes[p].active)
                fn (static_cast<std::uint8_t> (p), notes[p]);
    }

    // Tiny utility: any note currently held?
    bool anyHeld() const noexcept;

private:
    std::array<HeldNote, kNumPitches> notes {};
};

}  // namespace hitnotedmx
