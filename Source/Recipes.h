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
// (1..nPix). `t` is the current playhead time in beats. `tail` is a
// trail-length control in [0, 1] derived from the trigger note's velocity
// (0 = instant single-pixel head, 1 = a full-bar fading comet); only the
// moving-head chases (chase_up/down, ping_pong, snake) use it.

inline constexpr int kDynamicPitchStart = 24;
inline constexpr int kDynamicPitchEnd   = 35;  // inclusive
inline constexpr int kNumDynamics       = kDynamicPitchEnd - kDynamicPitchStart + 1;

// Strobe is NOT a per-pixel recipe: it is a global shutter applied at the
// DMX driver's send loop (see EnttecProDmx::setStrobeHz) so it stays
// perfectly synced to the output clock and free of audio-block jitter. It
// still occupies its slot in the dynamic vocabulary/menu — its recipe-table
// entry is null and computeDmx treats it as a no-op; the processor reads
// the held note and drives the driver shutter. 10 Hz = 2-on / 2-off at the
// 40 Hz send rate (exactly even); 20 Hz is the hardware max.
inline constexpr int    kStrobePitch = kDynamicPitchStart + 9;  // 33
inline constexpr double kStrobeHz    = 10.0;

using DynamicFn = float (*) (double t, int barIdx, int pixel, int nPix, int nBars, float tail) noexcept;

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
float chase_up   (double t, int barIdx, int pixel, int nPix, int nBars, float tail) noexcept;
float chase_down (double t, int barIdx, int pixel, int nPix, int nBars, float tail) noexcept;
float ping_pong  (double t, int barIdx, int pixel, int nPix, int nBars, float tail) noexcept;
float snake      (double t, int barIdx, int pixel, int nPix, int nBars, float tail) noexcept;
float sine_wave  (double t, int barIdx, int pixel, int nPix, int nBars, float tail) noexcept;
float sparkle    (double t, int barIdx, int pixel, int nPix, int nBars, float tail) noexcept;
float breathe    (double t, int barIdx, int pixel, int nPix, int nBars, float tail) noexcept;
float sweep_up   (double t, int barIdx, int pixel, int nPix, int nBars, float tail) noexcept;
float sweep_down (double t, int barIdx, int pixel, int nPix, int nBars, float tail) noexcept;
float kick_pulse (double t, int barIdx, int pixel, int nPix, int nBars, float tail) noexcept;
float alt_swap   (double t, int barIdx, int pixel, int nPix, int nBars, float tail) noexcept;

}  // namespace hitnotedmx
