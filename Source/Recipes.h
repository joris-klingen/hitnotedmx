#pragma once

#include <cstdint>

namespace hitnotedmx
{

// Dynamic recipes — pure functions of (beat time, bar index, pixel index)
// returning a per-pixel value. Two kinds:
//
//   • Brightness recipes (DynamicFn) return a mask in [0, 1]; the colour
//     comes from the structural selectors or the primary palette.
//   • Colour recipes (DynamicColorFn) return RGB directly and, when held,
//     replace the palette route for the lit pixels.
//
// They are grouped by feel into three brightness banks + one colour bank,
// each a full chromatic octave starting on a C so the trigger menu lines up
// with the keyboard (C3 = MIDI 60, Ableton convention):
//
//   Chases     C0  24..35 (12)   brightness — vertical & grid-aware motion
//   Breathes   C1  36..47 (12)   brightness — slow ambient shapes
//   Wild       C2  48..59 (12)   brightness — energetic / chaotic
//   Multicolor C3..C4  60..83 (24, two octaves) — self-coloured RGB
//
// All brightness recipes return 0..1. `barIdx` is 0-based (0..nBars-1).
// `pixel` is 1-based (1..nPix). `t` is the playhead time in beats.
//
// `tail` (DynamicFn only): a trail-length control in [0, 1] from the note's
// velocity (0 = single-pixel head, 1 = full comet); used by the moving-head
// chases. Colour recipes take no `tail` — their velocity drives SPEED, applied
// by computeDmx scaling `t` before the call.

inline constexpr int kChasesStart   = 24;  // C0
inline constexpr int kNumChases     = 12;
inline constexpr int kBreathesStart = 36;  // C1
inline constexpr int kNumBreathes   = 12;
inline constexpr int kWildStart     = 48;  // C2
inline constexpr int kNumWild       = 12;
inline constexpr int kColorDynStart = 60;  // C3..C4 (two octaves)
inline constexpr int kNumColorDyn   = 24;

// Strobe is NOT a per-pixel recipe: it is a global shutter applied at the
// DMX driver's send loop (see EnttecProDmx::setStrobeHz) so it stays
// perfectly synced to the output clock and free of audio-block jitter. It
// still occupies a slot in the Wild bank — its recipe-table entry is null
// and computeDmx treats it as a no-op; the processor reads the held note and
// drives the driver shutter. 10 Hz = 2-on / 2-off at the 40 Hz send rate
// (exactly even); 20 Hz is the hardware max.
inline constexpr int    kStrobePitch = kWildStart + 1;  // 49
inline constexpr double kStrobeHz    = 10.0;

// Per-recipe velocity exceptions handled in computeDmx:
//   • Wild is beat-synced (velocity → 1/16..1/1 division) EXCEPT sparkle /
//     sparkle_few, which stay free-running (continuous velocity → speed).
//   • Breathes run at half speed EXCEPT ripple.
//   • VU meter is beat-LOCKED (timing always on the beat) and velocity sets
//     its range/gain instead of speed.
inline constexpr int kSparklePitch    = kWildStart + 0;      // 48
inline constexpr int kSparkleFewPitch = kWildStart + 9;      // 57
inline constexpr int kRipplePitch     = kBreathesStart + 2;  // 38
inline constexpr int kVuMeterPitch    = kColorDynStart + 2;  // 62

using DynamicFn = float (*) (double t, int barIdx, int pixel, int nPix, int nBars, float tail) noexcept;

struct RecipeRGB { float r, g, b; };

using DynamicColorFn = RecipeRGB (*) (double t, int barIdx, int pixel, int nPix, int nBars, float param) noexcept;

// Look up the brightness recipe for a MIDI pitch (Chases / Breathes / Wild).
// Returns nullptr outside those banks (and for the null strobe slot).
DynamicFn getDynamicRecipe (int pitch) noexcept;

// Look up the colour recipe for a MIDI pitch (Multicolor). nullptr outside
// [kColorDynStart, kColorDynStart + kNumColorDyn).
DynamicColorFn getColorRecipe (int pitch) noexcept;

// Is this pitch a brightness-dynamic recipe (any of the three banks)?
inline constexpr bool isDynamicPitch (int pitch) noexcept
{
    return (pitch >= kChasesStart   && pitch < kChasesStart   + kNumChases)
        || (pitch >= kBreathesStart && pitch < kBreathesStart + kNumBreathes)
        || (pitch >= kWildStart     && pitch < kWildStart     + kNumWild);
}

// Is this pitch a self-coloured dynamic recipe?
inline constexpr bool isColorDynPitch (int pitch) noexcept
{
    return pitch >= kColorDynStart && pitch < kColorDynStart + kNumColorDyn;
}

// Which brightness bank a pitch belongs to (velocity means different things
// per bank — see computeDmx: Chases → tail, Wild → speed, Breathes → density).
inline constexpr bool isChasesPitch   (int p) noexcept { return p >= kChasesStart   && p < kChasesStart   + kNumChases; }
inline constexpr bool isBreathesPitch (int p) noexcept { return p >= kBreathesStart && p < kBreathesStart + kNumBreathes; }
inline constexpr bool isWildPitch     (int p) noexcept { return p >= kWildStart     && p < kWildStart     + kNumWild; }

// Individual recipes — exposed for direct use if a caller doesn't want to go
// through the dispatcher.
float chase_up   (double t, int barIdx, int pixel, int nPix, int nBars, float tail) noexcept;
float chase_down (double t, int barIdx, int pixel, int nPix, int nBars, float tail) noexcept;
float ping_pong  (double t, int barIdx, int pixel, int nPix, int nBars, float tail) noexcept;
float snake      (double t, int barIdx, int pixel, int nPix, int nBars, float tail) noexcept;
float sine_wave  (double t, int barIdx, int pixel, int nPix, int nBars, float tail) noexcept;
float sparkle    (double t, int barIdx, int pixel, int nPix, int nBars, float tail) noexcept;
float breathe    (double t, int barIdx, int pixel, int nPix, int nBars, float tail) noexcept;
float spiral     (double t, int barIdx, int pixel, int nPix, int nBars, float tail) noexcept;
float converge   (double t, int barIdx, int pixel, int nPix, int nBars, float tail) noexcept;
float alt_swap   (double t, int barIdx, int pixel, int nPix, int nBars, float tail) noexcept;
float diag_up    (double t, int barIdx, int pixel, int nPix, int nBars, float tail) noexcept;
float diag_down  (double t, int barIdx, int pixel, int nPix, int nBars, float tail) noexcept;
float wave_train (double t, int barIdx, int pixel, int nPix, int nBars, float tail) noexcept;
float expand     (double t, int barIdx, int pixel, int nPix, int nBars, float tail) noexcept;
float contract   (double t, int barIdx, int pixel, int nPix, int nBars, float tail) noexcept;
float rain       (double t, int barIdx, int pixel, int nPix, int nBars, float tail) noexcept;
float ripple     (double t, int barIdx, int pixel, int nPix, int nBars, float tail) noexcept;
float halo       (double t, int barIdx, int pixel, int nPix, int nBars, float tail) noexcept;
float moon_rise  (double t, int barIdx, int pixel, int nPix, int nBars, float tail) noexcept;
float soft_ball  (double t, int barIdx, int pixel, int nPix, int nBars, float tail) noexcept;
float aurora     (double t, int barIdx, int pixel, int nPix, int nBars, float tail) noexcept;

// Chases (fill).
float snake_h    (double t, int barIdx, int pixel, int nPix, int nBars, float tail) noexcept;

// Breathes (fill).
float ripple_h   (double t, int barIdx, int pixel, int nPix, int nBars, float tail) noexcept;
float bloom      (double t, int barIdx, int pixel, int nPix, int nBars, float tail) noexcept;
float shimmer    (double t, int barIdx, int pixel, int nPix, int nBars, float tail) noexcept;
float sway       (double t, int barIdx, int pixel, int nPix, int nBars, float tail) noexcept;
float drift      (double t, int barIdx, int pixel, int nPix, int nBars, float tail) noexcept;

// Wild (fill).
float lightning    (double t, int barIdx, int pixel, int nPix, int nBars, float tail) noexcept;
float static_noise (double t, int barIdx, int pixel, int nPix, int nBars, float tail) noexcept;
float glitch       (double t, int barIdx, int pixel, int nPix, int nBars, float tail) noexcept;
float bounce       (double t, int barIdx, int pixel, int nPix, int nBars, float tail) noexcept;
float zigzag       (double t, int barIdx, int pixel, int nPix, int nBars, float tail) noexcept;
float sparkle_few  (double t, int barIdx, int pixel, int nPix, int nBars, float tail) noexcept;
float fast_ball    (double t, int barIdx, int pixel, int nPix, int nBars, float tail) noexcept;
float pong         (double t, int barIdx, int pixel, int nPix, int nBars, float tail) noexcept;

RecipeRGB rainbow_chase  (double t, int barIdx, int pixel, int nPix, int nBars, float param) noexcept;
RecipeRGB comet          (double t, int barIdx, int pixel, int nPix, int nBars, float param) noexcept;
RecipeRGB vu_meter       (double t, int barIdx, int pixel, int nPix, int nBars, float param) noexcept;
RecipeRGB fire           (double t, int barIdx, int pixel, int nPix, int nBars, float param) noexcept;
RecipeRGB desert_breathe (double t, int barIdx, int pixel, int nPix, int nBars, float param) noexcept;
// Multicolor (octave C3 fill).
RecipeRGB vu_smooth      (double t, int barIdx, int pixel, int nPix, int nBars, float param) noexcept;
RecipeRGB night_sky      (double t, int barIdx, int pixel, int nPix, int nBars, float param) noexcept;
RecipeRGB police         (double t, int barIdx, int pixel, int nPix, int nBars, float param) noexcept;
RecipeRGB embers         (double t, int barIdx, int pixel, int nPix, int nBars, float param) noexcept;
RecipeRGB plasma         (double t, int barIdx, int pixel, int nPix, int nBars, float param) noexcept;
RecipeRGB ocean          (double t, int barIdx, int pixel, int nPix, int nBars, float param) noexcept;
RecipeRGB nebula         (double t, int barIdx, int pixel, int nPix, int nBars, float param) noexcept;

// Multicolor (octave C4 — second column).
RecipeRGB sunset         (double t, int barIdx, int pixel, int nPix, int nBars, float param) noexcept;
RecipeRGB forest         (double t, int barIdx, int pixel, int nPix, int nBars, float param) noexcept;
RecipeRGB lava           (double t, int barIdx, int pixel, int nPix, int nBars, float param) noexcept;
RecipeRGB borealis       (double t, int barIdx, int pixel, int nPix, int nBars, float param) noexcept;
RecipeRGB candy          (double t, int barIdx, int pixel, int nPix, int nBars, float param) noexcept;
RecipeRGB magma          (double t, int barIdx, int pixel, int nPix, int nBars, float param) noexcept;
RecipeRGB storm          (double t, int barIdx, int pixel, int nPix, int nBars, float param) noexcept;
RecipeRGB galaxy         (double t, int barIdx, int pixel, int nPix, int nBars, float param) noexcept;
RecipeRGB blocks         (double t, int barIdx, int pixel, int nPix, int nBars, float param) noexcept;
RecipeRGB disco          (double t, int barIdx, int pixel, int nPix, int nBars, float param) noexcept;
RecipeRGB twilight       (double t, int barIdx, int pixel, int nPix, int nBars, float param) noexcept;
RecipeRGB heatmap        (double t, int barIdx, int pixel, int nPix, int nBars, float param) noexcept;

}  // namespace hitnotedmx
