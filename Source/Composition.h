#pragma once

#include <array>
#include <cstdint>

#include "EnttecProDmx.h"   // kDmxUniverseSize
#include "MidiState.h"

namespace hitnotedmx
{

// Per-channel DMX values in [0, 1]. 1-based DMX channel addressing
// (channel 1 lives at data[0]). Preallocated once and reused — the
// composition pass never allocates.
class DmxValues
{
public:
    static constexpr int kSize = kDmxUniverseSize;

    void clear() noexcept
    {
        for (auto& v : data) v = 0.0f;
    }

    // dmxChannel is 1-based (1..512).
    void set (int dmxChannel, float v) noexcept
    {
        if (dmxChannel >= 1 && dmxChannel <= kSize)
            data[dmxChannel - 1] = v;
    }

    float get (int dmxChannel) const noexcept
    {
        if (dmxChannel >= 1 && dmxChannel <= kSize)
            return data[dmxChannel - 1];
        return 0.0f;
    }

    // Raw 0-based view for fast scans.
    const float* raw() const noexcept { return data.data(); }
    float*       raw() noexcept       { return data.data(); }

private:
    std::array<float, kSize> data {};
};

// Compute per-channel DMX state at the given playhead time. Mirrors
// _compute_state in midi_to_dmx.py:
//
//   1. If the blackout pitch (84) is held, all channels are 0.
//   2. Compute primary and secondary palette colors from active
//      color notes (most recent wins; no crossfade — see comment in
//      Composition.cpp for the simplification).
//   3. Build bar / pixel / dynamic masks from active utility notes.
//      Each layer defaults to "all lit" if no note in that layer is
//      held. Lit pixels = intersection.
//   4. Per pixel, choose color route: pixel > dynamic > bar (most-
//      specific wins). Apply color × brightness to the three DMX
//      channels of that pixel.
//   5. Spots: warm-white tint if WW pitches held, plus secondary
//      colour overlay if their sec pitches held. RGBW channels set
//      directly (dimmer master).
//
// `outValues` is cleared at entry; bars and spots are fully written
// for every audio block (DMX channels never inherit stale state from
// a previous block). Channels outside the rig footprint (>= 121) are
// untouched.
void computeDmx (const MidiState& state, double tBeats, DmxValues& outValues) noexcept;

}  // namespace hitnotedmx
