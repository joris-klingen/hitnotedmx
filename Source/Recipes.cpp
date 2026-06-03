#include "Recipes.h"

#include <array>
#include <cmath>

namespace hitnotedmx
{

namespace
{
// Per-pixel deterministic phase offset in [0, 1) for the sparkle recipe.
// The constant is the Knuth golden-ratio integer hash multiplier.
float sparklePhase (int barIdx, int pixel) noexcept
{
    const std::uint32_t h =
        static_cast<std::uint32_t> ((barIdx * 31 + pixel) * 0x9E3779B1u);
    return static_cast<float> (h) / 4294967296.0f;
}

constexpr float kTwoPi = 6.28318530717958647692f;

// Longest comet tail, at tail == 1, expressed as a fraction of the track
// length (a full bar). tail == 0 collapses to the original single-pixel head.
constexpr float kMaxTailFrac = 1.0f;

// Comet brightness for a moving head. `behind` is how many steps the queried
// cell sits *behind* the head along the direction of travel (0 = the head
// itself); callers compute it per track topology (with or without wrap).
// `trackLen` is the track length in cells; `tail` in [0, 1] sets the trail.
inline float cometBrightness (int behind, int trackLen, float tail) noexcept
{
    if (behind < 0)
        return 0.0f;                       // ahead of the head
    const float tailLen = tail * kMaxTailFrac * static_cast<float> (trackLen);
    if (tailLen <= 0.0f)
        return behind == 0 ? 1.0f : 0.0f;  // instant: head only
    const float b = 1.0f - static_cast<float> (behind) / tailLen;
    return b > 0.0f ? b : 0.0f;
}

// Steps `pixel` sits behind an integer head `pos` on a wrapping track of
// `len` cells, moving in `dir` (+1 toward higher indices, -1 toward lower).
inline int behindWrapped (int pos, int pixel, int len, int dir) noexcept
{
    const int raw = dir > 0 ? (pos - pixel) : (pixel - pos);
    return ((raw % len) + len) % len;
}
}

// ---- the 12 recipes ------------------------------------------------------
// `t` in beats; bars are 0-indexed; pixels are 1-indexed.

float chase_up (double t, int /*barIdx*/, int pixel, int nPix, int /*nBars*/, float tail) noexcept
{
    const int pos = static_cast<int> (t * nPix) % nPix + 1;  // head 1..nPix, ascending
    return cometBrightness (behindWrapped (pos, pixel, nPix, +1), nPix, tail);
}

float chase_down (double t, int /*barIdx*/, int pixel, int nPix, int /*nBars*/, float tail) noexcept
{
    const int pos = nPix - (static_cast<int> (t * nPix) % nPix);  // head descending
    return cometBrightness (behindWrapped (pos, pixel, nPix, -1), nPix, tail);
}

float ping_pong (double t, int /*barIdx*/, int pixel, int nPix, int /*nBars*/, float tail) noexcept
{
    const double phase = std::fmod (t, 2.0);
    int pos, dir;
    if (phase < 1.0) { pos = static_cast<int> (phase * nPix) + 1;            dir = +1; }
    else             { pos = nPix - static_cast<int> ((phase - 1.0) * nPix); dir = -1; }

    // Bouncing track: no wrap, so the tail clips at the ends rather than
    // reappearing on the far side.
    const int behind = dir > 0 ? (pos - pixel) : (pixel - pos);
    return cometBrightness (behind, nPix, tail);
}

float snake (double t, int barIdx, int pixel, int nPix, int nBars, float tail) noexcept
{
    const int total = nBars * nPix;
    const int pos = static_cast<int> ((t / nBars) * total) % total;  // 0-based global head
    const int gid = barIdx * nPix + (pixel - 1);                     // 0-based global cell
    const int behind = ((pos - gid) % total + total) % total;        // wraps around the rig
    return cometBrightness (behind, nPix, tail);  // tail scaled to one bar
}

float sine_wave (double t, int /*barIdx*/, int pixel, int nPix, int /*nBars*/, float /*tail*/) noexcept
{
    const float phase = kTwoPi * (static_cast<float> (t) - static_cast<float> (pixel - 1) / nPix);
    return (1.0f + std::sin (phase)) * 0.5f;
}

float sparkle (double t, int barIdx, int pixel, int /*nPix*/, int /*nBars*/, float /*tail*/) noexcept
{
    constexpr float rate = 4.0f;
    const float val = std::sin (kTwoPi * (static_cast<float> (t) * rate + sparklePhase (barIdx, pixel)));
    return val > 0.6f ? 1.0f : 0.0f;
}

float breathe (double t, int /*barIdx*/, int /*pixel*/, int /*nPix*/, int /*nBars*/, float /*tail*/) noexcept
{
    return (1.0f + std::sin (kTwoPi * static_cast<float> (t) / 4.0f)) * 0.5f;
}

float sweep_up (double t, int /*barIdx*/, int pixel, int nPix, int /*nBars*/, float /*tail*/) noexcept
{
    const double phase = std::fmod (t, 1.0);
    const int fill = static_cast<int> (phase * nPix) + 1;
    return pixel <= fill ? 1.0f : 0.0f;
}

float sweep_down (double t, int /*barIdx*/, int pixel, int nPix, int /*nBars*/, float /*tail*/) noexcept
{
    const double phase = std::fmod (t, 1.0);
    const int fill = nPix - static_cast<int> (phase * nPix);
    return pixel >= fill ? 1.0f : 0.0f;
}

float kick_pulse (double t, int /*barIdx*/, int /*pixel*/, int /*nPix*/, int /*nBars*/, float /*tail*/) noexcept
{
    const double phase = std::fmod (t, 1.0);
    if (phase >= 0.25)
        return 0.0f;
    const float v = 1.0f - static_cast<float> (phase) * 4.0f;
    return v > 0.0f ? v : 0.0f;
}

float alt_swap (double t, int barIdx, int /*pixel*/, int /*nPix*/, int /*nBars*/, float /*tail*/) noexcept
{
    const bool beatEven = (static_cast<int> (t) % 2) == 0;
    const bool barEven  = (barIdx % 2) == 0;
    return beatEven == barEven ? 1.0f : 0.0f;
}


// ---- dispatcher ----------------------------------------------------------

namespace
{
// pitch − kDynamicPitchStart → function pointer.
constexpr std::array<DynamicFn, kNumDynamics> kRecipeTable {{
    &chase_up,    // 24
    &chase_down,  // 25
    &ping_pong,   // 26
    &snake,       // 27
    &sine_wave,   // 28
    &sparkle,     // 29
    &breathe,     // 30
    &sweep_up,    // 31
    &sweep_down,  // 32
    nullptr,      // 33 strobe — global shutter applied at the DMX driver
    &kick_pulse,  // 34
    &alt_swap,    // 35
}};
}

DynamicFn getDynamicRecipe (int pitch) noexcept
{
    if (! isDynamicPitch (pitch))
        return nullptr;
    return kRecipeTable[pitch - kDynamicPitchStart];
}

}  // namespace hitnotedmx
