#pragma once

#include <array>
#include <cstdint>

#include "EnttecProDmx.h"  // for kDmxUniverseSize

namespace hitnotedmx
{

// The lighting rig — physical fixtures and their DMX addresses.
//
// A grid of vertical RGB bars (cols × rows pixels, bottom-up) + 2 RGBW spots.
// The GRID SHAPE is a runtime setting (the editor's grid section / saved
// state); everything below 512 channels is allowed. The spots are pinned at
// the BOTTOM of the universe so a grid re-shape never re-patches them:
//
//   spot_l : DMX 1-6    (Dim, R, G, B, W, Strobe)
//   spot_r : DMX 7-12
//   bar_1  : DMX 13-…   (pixel 1 RGB = ch 13..15, pixel 2 RGB = ch 16..18, …)
//   bar_2+ : contiguous after bar 1 (rows × 3 channels per bar)
//
// Audio-thread safety: the shape lives in a plain `Rig` POD owned by the
// audio thread; every buffer that depends on it is preallocated at the
// kMaxBars × kMaxRows bound, so applying a new shape never allocates.

inline constexpr int kNumSpots       = 2;
inline constexpr int kSpotChannels   = 6;
inline constexpr int kBarChannelBase = kNumSpots * kSpotChannels + 1;   // 13

// Hard grid bounds. Rows are capped at 32 so the pixel-zone masks stay a
// single uint32; cols at 8 keeps the widest grid drawable in the fixed
// editor. The universe allows (512 - 12) / 3 = 166 cells, checked at grid-set
// time (cols × rows ≤ kMaxGridCells) on top of the per-axis caps.
inline constexpr int kMaxBars      = 8;
inline constexpr int kMaxRows      = 32;
inline constexpr int kMaxGridCells = (kDmxUniverseSize - kNumSpots * kSpotChannels) / 3;   // 166
inline constexpr int kDefaultBars  = 4;
inline constexpr int kDefaultRows  = 18;

// The runtime grid shape + DMX addressing math. A plain POD (two ints), so
// the audio thread reads it without synchronisation once applied.
struct Rig
{
    int cols = kDefaultBars;   // bar count
    int rows = kDefaultRows;   // pixels per bar

    // 1-based DMX channel of bar `barIdx`'s pixel 1 red component.
    constexpr int barStart (int barIdx) const noexcept
    {
        return kBarChannelBase + barIdx * rows * 3;
    }

    // 1-based DMX channels (R, G, B) for `pixel` (1..rows, 1 = bottom).
    constexpr std::array<int, 3> channelsFor (int barIdx, int pixel) const noexcept
    {
        const int base = barStart (barIdx) + (pixel - 1) * 3;
        return { base, base + 1, base + 2 };
    }

    constexpr int cells()       const noexcept { return cols * rows; }
    constexpr int rigChannels() const noexcept { return kNumSpots * kSpotChannels + cells() * 3; }
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

inline constexpr std::array<SpotFixture, kNumSpots> kSpots {{
    { 1 },
    { 7 },
}};

static_assert (kBarChannelBase + kMaxGridCells * 3 - 1 <= kDmxUniverseSize,
               "max grid overruns DMX universe");
static_assert (kDefaultBars * kDefaultRows <= kMaxGridCells, "default grid too big");
static_assert (Rig {}.rigChannels() == kDefaultBars * kDefaultRows * 3 + kNumSpots * kSpotChannels,
               "rig math wrong");

}  // namespace hitnotedmx
