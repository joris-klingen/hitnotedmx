#pragma once

#include <cstdint>

namespace hitnotedmx
{

// Dynamic recipes — pure functions of (beat time, bar index, pixel
// index) that return a per-pixel brightness mask in [0, 1].
//
// Dispatched by MIDI pitch:
//
//   pitch 24 → chase_up         pitch 30 → breathe
//   pitch 25 → chase_down       pitch 31 → sweep_up
//   pitch 26 → ping_pong        pitch 32 → sweep_down
//   pitch 27 → snake            pitch 33 → strobe
//   pitch 28 → sine_wave        pitch 34 → kick_pulse
//   pitch 29 → sparkle          pitch 35 → alt_swap
//
// All return 0..1. `barIdx` is 0-based (0..nBars-1). `pixel` is 1-based
// (1..nPix). `t` is the current playhead time in beats.

inline constexpr int kDynamicPitchStart = 24;
inline constexpr int kDynamicPitchEnd   = 35;  // inclusive
inline constexpr int kNumDynamics       = kDynamicPitchEnd - kDynamicPitchStart + 1;

using DynamicFn = float (*) (double t, int barIdx, int pixel, int nPix, int nBars) noexcept;

// Look up the recipe for a MIDI pitch. Returns nullptr if pitch is
// outside [kDynamicPitchStart, kDynamicPitchEnd].
DynamicFn getDynamicRecipe (int pitch) noexcept;

// Is this pitch a dynamic recipe?
inline constexpr bool isDynamicPitch (int pitch) noexcept
{
    return pitch >= kDynamicPitchStart && pitch <= kDynamicPitchEnd;
}

// Individual recipes — exposed for direct use if a caller doesn't want
// to go through the dispatcher.
float chase_up   (double t, int barIdx, int pixel, int nPix, int nBars) noexcept;
float chase_down (double t, int barIdx, int pixel, int nPix, int nBars) noexcept;
float ping_pong  (double t, int barIdx, int pixel, int nPix, int nBars) noexcept;
float snake      (double t, int barIdx, int pixel, int nPix, int nBars) noexcept;
float sine_wave  (double t, int barIdx, int pixel, int nPix, int nBars) noexcept;
float sparkle    (double t, int barIdx, int pixel, int nPix, int nBars) noexcept;
float breathe    (double t, int barIdx, int pixel, int nPix, int nBars) noexcept;
float sweep_up   (double t, int barIdx, int pixel, int nPix, int nBars) noexcept;
float sweep_down (double t, int barIdx, int pixel, int nPix, int nBars) noexcept;
float strobe     (double t, int barIdx, int pixel, int nPix, int nBars) noexcept;
float kick_pulse (double t, int barIdx, int pixel, int nPix, int nBars) noexcept;
float alt_swap   (double t, int barIdx, int pixel, int nPix, int nBars) noexcept;

}  // namespace hitnotedmx
