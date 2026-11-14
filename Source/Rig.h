#pragma once

#include <array>
#include <cstdint>

#include "EnttecProDmx.h"  // for kDmxUniverseSize

namespace hitnotedmx
{

// The lighting rig — physical fixtures and their DMX addresses.
//
// A grid of vertical RGB bars (cols × rows pixels, bottom-up) + 2 spots.
// The SPOT TYPE is a runtime setting too (the editor's RGBL / RGBW toggle,
// see SpotKind) — both types are 6 channels, so the patch never moves:
//
//   RGBL (default) : ADJ Saber Spot RGBL in its **6CH RGBL** mode —
//                    R, G, B, Lime, Dimmer, Dimmer fine. No white emitter;
//                    warm white is mixed (kSpotLWarm* in Composition.cpp).
//   RGBW           : the original spots — Dim, R, G, B, W, Strobe.
// The GRID SHAPE is a runtime setting (the editor's grid section / saved
// state); everything below 512 channels is allowed. The spots are pinned at
// the BOTTOM of the universe so a grid re-shape never re-patches them:
//
//   spot_l : DMX 1-6    (channel order per SpotKind, above)
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

// Which fixture sits on the two spot slots. Both are 6-channel, so only the
// channel ORDER and the meaning of the fourth emitter differ.
enum class SpotKind : std::uint8_t
{
    rgbl = 0,   // ADJ Saber Spot RGBL, 6CH RGBL mode (default)
    rgbw = 1,   // original RGBW spot
};

// The runtime grid shape + spot type + DMX addressing math. A plain POD, so
// the audio thread reads it without synchronisation once applied.
struct Rig
{
    int      cols  = kDefaultBars;     // bar count
    int      rows  = kDefaultRows;     // pixels per bar
    SpotKind spots = SpotKind::rgbl;   // fixture type on the two spot slots

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

// 1-based DMX channels of one spot, resolved for a SpotKind.
struct SpotChannels
{
    int red, green, blue;
    int fourth;   // the fourth emitter: Lime (RGBL) or White (RGBW)
    int dimmer;
    int spare;    // Dimmer fine (RGBL) / Strobe (RGBW) — always written 0
};

struct SpotFixture
{
    int dmxStart;

    constexpr SpotChannels channels (SpotKind kind) const noexcept
    {
        return kind == SpotKind::rgbw
            //       R             G             B             W             Dim           Strobe
            ? SpotChannels { dmxStart + 1, dmxStart + 2, dmxStart + 3, dmxStart + 4, dmxStart + 0, dmxStart + 5 }
            //       R             G             B             Lime          Dim           Dim fine
            : SpotChannels { dmxStart + 0, dmxStart + 1, dmxStart + 2, dmxStart + 3, dmxStart + 4, dmxStart + 5 };
    }
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
