#pragma once

#include <array>
#include <cstdint>

#include "EnttecProDmx.h"  // for kDmxUniverseSize

namespace hitnotedmx
{

// The lighting rig — physical fixtures and their DMX addresses.
//
// 4 RGB bars (18 pixels each, bottom-up) + 2 RGBW spots.
//
//   bar_1  : DMX 1-54    (pixel 1 RGB = ch 1..3, pixel 2 RGB = ch 4..6, …)
//   bar_2  : DMX 55-108
//   bar_3  : DMX 109-162
//   bar_4  : DMX 163-216
//   spot_l : DMX 217-222  (Dim, R, G, B, W, Strobe)
//   spot_r : DMX 223-228
//
// All structs are constexpr/POD so they can live in a constexpr table
// and be accessed from the audio thread without any synchronisation.

inline constexpr int kNumBars       = 4;
inline constexpr int kPixelsPerBar  = 18;
inline constexpr int kNumSpots      = 2;

struct BarFixture
{
    int dmxStart;   // 1-based DMX channel of pixel 1's red component
    int pixels;     // 18 in the extended rig

    // 1-based DMX channels (R, G, B) for `pixel` (1..pixels, 1 = bottom).
    constexpr std::array<int, 3> channelsFor (int pixel) const noexcept
    {
        const int base = dmxStart + (pixel - 1) * 3;
        return { base, base + 1, base + 2 };
    }
};

struct SpotFixture
{
    int dmxStart;

    constexpr int dimmer() const noexcept { return dmxStart + 0; }
    constexpr int red()    const noexcept { return dmxStart + 1; }
    constexpr int green()  const noexcept { return dmxStart + 2; }
    constexpr int blue()   const noexcept { return dmxStart + 3; }
    constexpr int white()  const noexcept { return dmxStart + 4; }
    constexpr int strobe() const noexcept { return dmxStart + 5; }
};

inline constexpr std::array<BarFixture, kNumBars> kBars {{
    { 1,   18 },
    { 55,  18 },
    { 109, 18 },
    { 163, 18 },
}};

inline constexpr std::array<SpotFixture, kNumSpots> kSpots {{
    { 217 },
    { 223 },
}};

// Total channels used by the rig: 4 * 18 * 3 + 2 * 6 = 228.
inline constexpr int kRigChannels = kNumBars * kPixelsPerBar * 3 + kNumSpots * 6;
static_assert (kRigChannels == 228, "rig math wrong");
static_assert (kRigChannels <= kDmxUniverseSize, "rig overruns DMX universe");

}  // namespace hitnotedmx
