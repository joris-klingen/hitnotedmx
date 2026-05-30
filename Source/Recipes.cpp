#include "Recipes.h"

#include <array>
#include <cmath>

namespace hitnotedmx
{

namespace
{
// Per-pixel deterministic phase offset in [0, 1) for the sparkle recipe.
// Mirrors _sparkle_phase in midi_to_dmx.py — the constant is the Knuth
// golden-ratio integer hash multiplier.
float sparklePhase (int barIdx, int pixel) noexcept
{
    const std::uint32_t h =
        static_cast<std::uint32_t> ((barIdx * 31 + pixel) * 0x9E3779B1u);
    return static_cast<float> (h) / 4294967296.0f;
}

constexpr float kTwoPi = 6.28318530717958647692f;
}

// ---- the 12 recipes ------------------------------------------------------
// All ports mirror midi_to_dmx.py line-for-line. `t` in beats; bars are
// 0-indexed; pixels are 1-indexed.

float chase_up (double t, int /*barIdx*/, int pixel, int nPix, int /*nBars*/) noexcept
{
    const int pos = static_cast<int> (t * nPix) % nPix + 1;
    return pixel == pos ? 1.0f : 0.0f;
}

float chase_down (double t, int /*barIdx*/, int pixel, int nPix, int /*nBars*/) noexcept
{
    const int pos = nPix - (static_cast<int> (t * nPix) % nPix);
    return pixel == pos ? 1.0f : 0.0f;
}

float ping_pong (double t, int /*barIdx*/, int pixel, int nPix, int /*nBars*/) noexcept
{
    const double phase = std::fmod (t, 2.0);
    int pos;
    if (phase < 1.0)
        pos = static_cast<int> (phase * nPix) + 1;
    else
        pos = nPix - static_cast<int> ((phase - 1.0) * nPix);
    return pixel == pos ? 1.0f : 0.0f;
}

float snake (double t, int barIdx, int pixel, int nPix, int nBars) noexcept
{
    const int total = nBars * nPix;
    const int pos = static_cast<int> ((t / nBars) * total) % total;
    const int cb = pos / nPix;
    const int cp = (pos % nPix) + 1;
    return (barIdx == cb && pixel == cp) ? 1.0f : 0.0f;
}

float sine_wave (double t, int /*barIdx*/, int pixel, int nPix, int /*nBars*/) noexcept
{
    const float phase = kTwoPi * (static_cast<float> (t) - static_cast<float> (pixel - 1) / nPix);
    return (1.0f + std::sin (phase)) * 0.5f;
}

float sparkle (double t, int barIdx, int pixel, int /*nPix*/, int /*nBars*/) noexcept
{
    constexpr float rate = 4.0f;
    const float val = std::sin (kTwoPi * (static_cast<float> (t) * rate + sparklePhase (barIdx, pixel)));
    return val > 0.6f ? 1.0f : 0.0f;
}

float breathe (double t, int /*barIdx*/, int /*pixel*/, int /*nPix*/, int /*nBars*/) noexcept
{
    return (1.0f + std::sin (kTwoPi * static_cast<float> (t) / 4.0f)) * 0.5f;
}

float sweep_up (double t, int /*barIdx*/, int pixel, int nPix, int /*nBars*/) noexcept
{
    const double phase = std::fmod (t, 1.0);
    const int fill = static_cast<int> (phase * nPix) + 1;
    return pixel <= fill ? 1.0f : 0.0f;
}

float sweep_down (double t, int /*barIdx*/, int pixel, int nPix, int /*nBars*/) noexcept
{
    const double phase = std::fmod (t, 1.0);
    const int fill = nPix - static_cast<int> (phase * nPix);
    return pixel >= fill ? 1.0f : 0.0f;
}

float strobe (double t, int /*barIdx*/, int /*pixel*/, int /*nPix*/, int /*nBars*/) noexcept
{
    return (static_cast<int> (t * 16.0) % 2 == 0) ? 1.0f : 0.0f;
}

float kick_pulse (double t, int /*barIdx*/, int /*pixel*/, int /*nPix*/, int /*nBars*/) noexcept
{
    const double phase = std::fmod (t, 1.0);
    if (phase >= 0.25)
        return 0.0f;
    const float v = 1.0f - static_cast<float> (phase) * 4.0f;
    return v > 0.0f ? v : 0.0f;
}

float alt_swap (double t, int barIdx, int /*pixel*/, int /*nPix*/, int /*nBars*/) noexcept
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
    &strobe,      // 33
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
