#include "Recipes.h"

#include <algorithm>
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

// Continuous-coordinate comet for tracks that aren't integer pixel grids
// (diagonals, bar-spanning paths). A small minimum band keeps the head
// visible at tail == 0 instead of collapsing between cells.
inline float cometBrightnessF (float behind, float trackLen, float tail) noexcept
{
    if (behind < 0.0f)
        return 0.0f;
    const float tailLen = 0.08f * trackLen + tail * kMaxTailFrac * trackLen;
    const float b = 1.0f - behind / tailLen;
    return b > 0.0f ? b : 0.0f;
}

// Full-avalanche integer hash (murmur3 finalizer) → [0, 1). Used wherever a
// recipe needs decorrelated per-cell or per-event randomness; unlike a
// single-multiply linear hash, nearby inputs produce unrelated outputs.
inline float hash01 (std::uint32_t h) noexcept
{
    h ^= h >> 16;  h *= 0x7FEB352Du;
    h ^= h >> 15;  h *= 0x846CA68Bu;
    h ^= h >> 16;
    return static_cast<float> (h) / 4294967296.0f;
}

// h in [0,1) → fully-saturated RGB on the hue wheel, scaled by v.
inline RecipeRGB hueToRgb (float h, float v) noexcept
{
    h = h - std::floor (h);
    const float x = h * 6.0f;
    const int   i = static_cast<int> (x);
    const float f = x - static_cast<float> (i);
    switch (i)
    {
        case 0:  return { v,            v * f,        0.0f         };
        case 1:  return { v * (1 - f),  v,            0.0f         };
        case 2:  return { 0.0f,         v,            v * f        };
        case 3:  return { 0.0f,         v * (1 - f),  v            };
        case 4:  return { v * f,        0.0f,         v            };
        default: return { v,            0.0f,         v * (1 - f)  };
    }
}
}

// ---- Chases / Breathes / Wild members ------------------------------------
// `t` in beats; bars are 0-indexed; pixels are 1-indexed. Recipes are listed
// roughly in original-authoring order; the dispatcher (bottom of file) maps
// them to their MIDI octaves.

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

float alt_swap (double t, int barIdx, int /*pixel*/, int /*nPix*/, int /*nBars*/, float /*tail*/) noexcept
{
    const bool beatEven = (static_cast<int> (t) % 2) == 0;
    const bool barEven  = (barIdx % 2) == 0;
    return beatEven == barEven ? 1.0f : 0.0f;
}


// ---- grid-aware chases & wipes -------------------------------------------
// These use both grid axes; the chase_*/sweep_* recipes above are almost
// entirely vertical (per-bar pixel motion). Horizontal-only motion (sweeping
// whole bars left/right) is now done with the Bar selectors instead.

float diag_up (double t, int barIdx, int pixel, int nPix, int nBars, float tail) noexcept
{
    // A band travelling along the rising diagonal: position on the diagonal
    // combines bar and pixel, so the front crosses the rig corner-to-corner
    // over two beats.
    constexpr float track = 2.0f;
    const float d = static_cast<float> (pixel - 1) / static_cast<float> (nPix)
                  + static_cast<float> (barIdx) / static_cast<float> (nBars);
    const float head   = static_cast<float> (std::fmod (t, static_cast<double> (track)));
    const float behind = std::fmod (head - d + track, track);
    return cometBrightnessF (behind, track, tail);
}

float diag_down (double t, int barIdx, int pixel, int nPix, int nBars, float tail) noexcept
{
    constexpr float track = 2.0f;
    const float d = static_cast<float> (nPix - pixel) / static_cast<float> (nPix)
                  + static_cast<float> (barIdx) / static_cast<float> (nBars);
    const float head   = static_cast<float> (std::fmod (t, static_cast<double> (track)));
    const float behind = std::fmod (head - d + track, track);
    return cometBrightnessF (behind, track, tail);
}

float wave_train (double t, int /*barIdx*/, int pixel, int nPix, int /*nBars*/, float /*tail*/) noexcept
{
    // Three sine crests per bar, drifting upward; squared to sharpen the
    // crests and darken the troughs.
    const float x = static_cast<float> (pixel - 1) / static_cast<float> (nPix);
    const float s = (1.0f + std::sin (kTwoPi * (3.0f * x - static_cast<float> (t)))) * 0.5f;
    return s * s;
}

float expand (double t, int /*barIdx*/, int pixel, int nPix, int /*nBars*/, float /*tail*/) noexcept
{
    // Wipe outward from the bar's midpoint to both ends over one beat.
    const double phase = std::fmod (t, 1.0);
    const float centre = (static_cast<float> (nPix) + 1.0f) * 0.5f;
    const float reach  = static_cast<float> (phase) * static_cast<float> (nPix) * 0.5f;
    return std::fabs (static_cast<float> (pixel) - centre) <= reach + 0.5f ? 1.0f : 0.0f;
}

float contract (double t, int /*barIdx*/, int pixel, int nPix, int /*nBars*/, float /*tail*/) noexcept
{
    // Wipe inward from both ends toward the midpoint over one beat.
    const double phase = std::fmod (t, 1.0);
    const float centre = (static_cast<float> (nPix) + 1.0f) * 0.5f;
    const float reach  = (1.0f - static_cast<float> (phase)) * static_cast<float> (nPix) * 0.5f;
    return std::fabs (static_cast<float> (pixel) - centre) >= reach - 0.5f ? 1.0f : 0.0f;
}


// ---- textures ------------------------------------------------------------

float rain (double t, int barIdx, int pixel, int nPix, int /*nBars*/, float /*tail*/) noexcept
{
    // Two independent drop streams per bar. Each stream drops once per
    // 1.5-beat window; the start of the drop inside its window is re-hashed
    // every window, so the timing never settles into a visible loop. A drop
    // falls the full bar in one beat with a short trail above the head.
    constexpr float period = 1.5f;
    float v = 0.0f;
    for (int s = 0; s < 2; ++s)
    {
        const std::uint32_t stream = static_cast<std::uint32_t> (barIdx * 2 + s);
        const double pos    = t / period + static_cast<double> (hash01 (stream));
        const int    window = static_cast<int> (pos);
        const float  local  = static_cast<float> (pos - window) * period;
        const float  delay  = hash01 (static_cast<std::uint32_t> (window) * 64u + stream)
                            * (period - 1.0f);
        const float  ft     = local - delay;     // time since this drop started
        if (ft < 0.0f || ft > 1.0f)
            continue;
        const float head = static_cast<float> (nPix) * (1.0f - ft);
        const float d    = static_cast<float> (pixel) - head;   // trail sits above
        if (d >= -0.5f && d < 3.0f)
            v = std::max (v, 1.0f - std::max (d, 0.0f) / 3.0f);
    }
    return v;
}

float ripple (double t, int barIdx, int pixel, int nPix, int nBars, float /*tail*/) noexcept
{
    // Soft rings expanding from the centre of the 4×18 grid; a new ring
    // launches every beat (two in flight), each crossing the grid in 2 beats.
    const float x = nBars > 1 ? static_cast<float> (barIdx) / static_cast<float> (nBars - 1) : 0.5f;
    const float y = (static_cast<float> (pixel) - 0.5f) / static_cast<float> (nPix);
    const float dx = x - 0.5f, dy = y - 0.5f;
    const float dist = std::sqrt (dx * dx + dy * dy);   // 0 .. ~0.71

    float v = 0.0f;
    for (int k = 0; k < 2; ++k)
    {
        const float r = static_cast<float> (std::fmod (t + k, 2.0)) * 0.4f;
        const float b = 1.0f - std::fabs (dist - r) / 0.10f;
        v = std::max (v, b);
    }
    return v > 0.0f ? v : 0.0f;
}

float halo (double t, int /*barIdx*/, int pixel, int nPix, int /*nBars*/, float /*tail*/) noexcept
{
    // A soft ring of light around a centre that bounces slowly up and down
    // the bar (4-beat period); the centre itself stays dark.
    const float centre = (0.5f + 0.5f * std::sin (kTwoPi * static_cast<float> (t) / 4.0f))
                       * static_cast<float> (nPix - 1) + 1.0f;
    const float radius = static_cast<float> (nPix) * 0.18f;
    const float d = std::fabs (static_cast<float> (pixel) - centre);
    const float b = 1.0f - std::fabs (d - radius) / (static_cast<float> (nPix) * 0.10f);
    return b > 0.0f ? b : 0.0f;
}


// ---- ambient breathes -----------------------------------------------------
// Gentle, sub-bar-rate motion with smooth ramps for quiet sections. exp()
// per pixel is fine here: 72 cells × once per audio block.

float moon_rise (double t, int /*barIdx*/, int pixel, int nPix, int /*nBars*/, float /*tail*/) noexcept
{
    // A soft gaussian glow climbing the bars over 16 beats, starting below
    // the bottom and drifting off the top before looping.
    const float phase  = static_cast<float> (std::fmod (t, 16.0)) / 16.0f;
    const float centre = phase * (static_cast<float> (nPix) + 6.0f) - 3.0f;
    const float d = (static_cast<float> (pixel) - centre) / (static_cast<float> (nPix) * 0.18f);
    return std::exp (-d * d);
}

float soft_ball (double t, int barIdx, int pixel, int nPix, int nBars, float /*tail*/) noexcept
{
    // A 2-D gaussian blob drifting on a lissajous path; the 7- and 11-beat
    // periods are incommensurate so the path doesn't visibly repeat.
    const float cx = 0.5f + 0.45f * std::sin (kTwoPi * static_cast<float> (t) / 7.0f);
    const float cy = 0.5f + 0.40f * std::sin (kTwoPi * static_cast<float> (t) / 11.0f + 1.3f);
    const float x = nBars > 1 ? static_cast<float> (barIdx) / static_cast<float> (nBars - 1) : 0.5f;
    const float y = (static_cast<float> (pixel) - 0.5f) / static_cast<float> (nPix);
    const float dx = (x - cx) / 0.35f;
    const float dy = (y - cy) / 0.22f;
    return std::exp (-(dx * dx + dy * dy));
}

float aurora (double t, int barIdx, int pixel, int nPix, int nBars, float /*tail*/) noexcept
{
    // Two slow counter-drifting spatial waves, phase-shifted per bar —
    // a shimmering curtain. Squared to bias dark with soft crests.
    const float y = (static_cast<float> (pixel) - 0.5f) / static_cast<float> (nPix);
    const float x = static_cast<float> (barIdx) / static_cast<float> (std::max (1, nBars - 1));
    const float a = std::sin (kTwoPi * (y * 1.3f + 0.13f * static_cast<float> (t)) + x * 2.1f);
    const float b = std::sin (kTwoPi * (y * 0.7f - 0.071f * static_cast<float> (t)) + x * 4.7f + 1.7f);
    const float v = 0.5f + 0.5f * (0.6f * a + 0.4f * b);
    return v * v * 0.9f;
}


// ---- chases (fill) --------------------------------------------------------

float snake_h (double t, int barIdx, int pixel, int nPix, int nBars, float tail) noexcept
{
    // Boustrophedon that runs HORIZONTALLY across the bars on each pixel row,
    // then steps up to the next row (reverse direction) — the horizontal
    // cousin of `snake`. One row per beat.
    const int total = nBars * nPix;
    const int row    = pixel - 1;                                    // 0 = bottom
    const int along  = (row % 2 == 0) ? barIdx : (nBars - 1 - barIdx);
    const int pathId = row * nBars + along;                          // 0..total-1
    const int head   = static_cast<int> (t * nBars) % total;
    const int behind = ((head - pathId) % total + total) % total;
    return cometBrightness (behind, nBars, tail);  // tail scaled to one sweep
}


// ---- breathes (fill) ------------------------------------------------------

float ripple_h (double t, int barIdx, int pixel, int nPix, int nBars, float /*tail*/) noexcept
{
    // Smooth, sparse concentric rings expanding from the grid centre, biased
    // horizontal (the pixel axis contributes only a little). Gaussian ring
    // profile + slow speed so it glides instead of snapping like a swap.
    const float x = nBars > 1 ? static_cast<float> (barIdx) / (nBars - 1) : 0.5f;
    const float y = (static_cast<float> (pixel) - 0.5f) / static_cast<float> (nPix);
    const float dist = std::fabs (x - 0.5f) + 0.30f * std::fabs (y - 0.5f);
    float v = 0.0f;
    for (int k = 0; k < 2; ++k)
    {
        const float r = static_cast<float> (std::fmod (t * 0.3 + k * 0.35, 0.7));
        const float g = (dist - r) / 0.18f;     // wide, soft band
        v = std::max (v, std::exp (-g * g));
    }
    return v;
}

float bloom (double t, int barIdx, int pixel, int nPix, int nBars, float /*tail*/) noexcept
{
    // A soft disc that grows and shrinks from the grid centre over 8 beats.
    const float x = nBars > 1 ? static_cast<float> (barIdx) / (nBars - 1) : 0.5f;
    const float y = (static_cast<float> (pixel) - 0.5f) / static_cast<float> (nPix);
    const float dx = x - 0.5f, dy = y - 0.5f;
    const float dist   = std::sqrt (dx * dx + dy * dy);
    const float radius = 0.08f + 0.45f * (0.5f - 0.5f * std::cos (kTwoPi * static_cast<float> (t) / 8.0f));
    return std::clamp ((radius - dist) / 0.12f + 1.0f, 0.0f, 1.0f);
}

float shimmer (double t, int barIdx, int pixel, int /*nPix*/, int /*nBars*/, float /*tail*/) noexcept
{
    // Gentle per-pixel twinkle from two slow incommensurate sines — calmer,
    // smoother cousin of sparkle/aurora.
    const float ph = hash01 (static_cast<std::uint32_t> (barIdx * 64 + pixel));
    const float v  = 0.42f
                   + 0.34f * std::sin (kTwoPi * (0.25f * static_cast<float> (t) + ph))
                   + 0.24f * std::sin (kTwoPi * (0.13f * static_cast<float> (t) + ph * 2.0f));
    return std::clamp (v, 0.0f, 1.0f);
}

float sway (double t, int barIdx, int pixel, int nPix, int nBars, float /*tail*/) noexcept
{
    // A soft vertical curtain that sways left↔right over 6 beats, with a
    // slow upward brightness gradient.
    const float x      = nBars > 1 ? static_cast<float> (barIdx) / (nBars - 1) : 0.5f;
    const float centre = 0.5f + 0.4f * std::sin (kTwoPi * static_cast<float> (t) / 6.0f);
    const float horiz  = std::max (0.0f, 1.0f - std::fabs (x - centre) / 0.4f);
    const float grad   = 0.5f + 0.5f * std::sin (kTwoPi * (static_cast<float> (pixel - 1)
                                / static_cast<float> (nPix) * 0.5f - static_cast<float> (t) / 6.0f));
    return horiz * grad;
}

float drift (double t, int barIdx, int pixel, int nPix, int nBars, float /*tail*/) noexcept
{
    // A wide soft diagonal band drifting slowly up the rig (sub-bar rate).
    const float d    = static_cast<float> (pixel - 1) / static_cast<float> (nPix)
                     + static_cast<float> (barIdx) / static_cast<float> (nBars) * 0.5f;
    const float head = static_cast<float> (std::fmod (t * 0.15, 1.5));
    return std::max (0.0f, 1.0f - std::fabs (d - head) / 0.3f);
}


// ---- wild (fill) ----------------------------------------------------------

float lightning (double t, int barIdx, int /*pixel*/, int /*nPix*/, int /*nBars*/, float /*tail*/) noexcept
{
    // Random whole-grid strikes (~40% of 0.7-beat windows fire), fast decay,
    // one bar struck brighter than the rest.
    constexpr float period = 0.7f;
    const int   window = static_cast<int> (t / period);
    const float local  = static_cast<float> (t / period - window) * period;
    if (hash01 (static_cast<std::uint32_t> (window) * 2654435761u + 11u) > 0.4f)
        return 0.0f;
    const float decay = std::exp (-local * 7.0f);
    const float bb    = hash01 (static_cast<std::uint32_t> (window * 7 + barIdx)) > 0.4f ? 1.0f : 0.45f;
    return decay * bb;
}

float static_noise (double t, int barIdx, int pixel, int /*nPix*/, int /*nBars*/, float /*tail*/) noexcept
{
    // Per-pixel TV-static flicker, re-hashed 12×/beat.
    const int frame = static_cast<int> (t * 12.0);
    const float h = hash01 (static_cast<std::uint32_t> (frame * 131 + barIdx * 64 + pixel));
    return h > 0.5f ? h : 0.0f;
}

float glitch (double t, int barIdx, int pixel, int nPix, int /*nBars*/, float /*tail*/) noexcept
{
    // Random pixel blocks pop on per bar, re-rolled 6×/beat.
    const int frame = static_cast<int> (t * 6.0);
    if (hash01 (static_cast<std::uint32_t> (frame * 7 + barIdx)) > 0.5f)
        return 0.0f;
    const int start = static_cast<int> (hash01 (static_cast<std::uint32_t> (frame * 13 + barIdx)) * nPix);
    const int len   = 2 + static_cast<int> (hash01 (static_cast<std::uint32_t> (frame * 17 + barIdx)) * 5);
    return (pixel - 1 >= start && pixel - 1 < start + len) ? 1.0f : 0.0f;
}

float bounce (double t, int /*barIdx*/, int pixel, int nPix, int /*nBars*/, float /*tail*/) noexcept
{
    // A ball bouncing up/down each bar (|sin|), two beats per bounce.
    const float h    = std::fabs (std::sin (kTwoPi * static_cast<float> (t) * 0.5f));
    const int   head = 1 + static_cast<int> (h * (nPix - 1));
    const int   d    = std::abs (pixel - head);
    return d == 0 ? 1.0f : (d <= 1 ? 0.4f : 0.0f);
}

float zigzag (double t, int barIdx, int pixel, int nPix, int nBars, float /*tail*/) noexcept
{
    // A single fast head zig-zagging up/down while hopping across the bars.
    const float tri  = std::fabs (static_cast<float> (std::fmod (t * 2.0, 2.0)) - 1.0f);
    const int   head = 1 + static_cast<int> (tri * (nPix - 1));
    if (barIdx != static_cast<int> (t * 2.0) % nBars)
        return 0.0f;
    const int d = std::abs (pixel - head);
    return d == 0 ? 1.0f : (d == 1 ? 0.4f : 0.0f);
}

float sparkle_few (double t, int barIdx, int pixel, int /*nPix*/, int /*nBars*/, float /*tail*/) noexcept
{
    // Like sparkle but far sparser — only a handful of pixels lit at once.
    constexpr float rate = 3.0f;
    const float val = std::sin (kTwoPi * (static_cast<float> (t) * rate + sparklePhase (barIdx, pixel)));
    return val > 0.93f ? 1.0f : 0.0f;
}

float fast_ball (double t, int barIdx, int pixel, int nPix, int nBars, float /*tail*/) noexcept
{
    // A quick ball bouncing up/down, hopping to a new bar every beat.
    const float h    = std::fabs (std::sin (kTwoPi * static_cast<float> (t) * 1.5f));
    const int   head = 1 + static_cast<int> (h * (nPix - 1));
    if (barIdx != static_cast<int> (t) % nBars)
        return 0.0f;
    const int d = std::abs (pixel - head);
    return d == 0 ? 1.0f : (d == 1 ? 0.3f : 0.0f);
}

float pong (double t, int barIdx, int pixel, int nPix, int /*nBars*/, float /*tail*/) noexcept
{
    // Atari Pong: 2-pixel paddles parked at the top and bottom of each bar,
    // with a ball bouncing between them (phase-offset per bar).
    if (pixel <= 2 || pixel >= nPix - 1)
        return 1.0f;                                   // paddles
    const float h    = std::fabs (std::sin (kTwoPi * (static_cast<float> (t) * 0.8f + barIdx * 0.2f)));
    const int   ball = 3 + static_cast<int> (h * (nPix - 5));
    return pixel == ball ? 1.0f : 0.0f;
}


// ---- colour bank (Multicolor) ----------------------------------------------
// These return RGB already scaled by their own brightness; computeDmx routes
// them in place of the palette colour.

RecipeRGB rainbow_chase (double t, int /*barIdx*/, int pixel, int nPix, int /*nBars*/) noexcept
{
    // The full hue wheel laid along the bar, scrolling upward (one wheel
    // revolution every 2 beats).
    const float h = static_cast<float> (pixel - 1) / static_cast<float> (nPix)
                  - static_cast<float> (t) * 0.5f;
    return hueToRgb (h, 1.0f);
}

RecipeRGB comet (double t, int /*barIdx*/, int pixel, int nPix, int /*nBars*/) noexcept
{
    // A white-hot head running up the bar, cooling through amber to deep red
    // along a fixed ~0.7-bar trail (colour recipes take their speed from
    // velocity, not their tail length).
    const int pos = static_cast<int> (t * nPix) % nPix + 1;
    const int behind = behindWrapped (pos, pixel, nPix, +1);
    if (behind == 0)
        return { 1.0f, 1.0f, 1.0f };
    const float tailLen = 0.7f * static_cast<float> (nPix);
    const float f = 1.0f - static_cast<float> (behind) / tailLen;
    if (f <= 0.0f)
        return { 0.0f, 0.0f, 0.0f };
    return { f, f * f * 0.8f, f * f * f * 0.3f };
}

RecipeRGB vu_meter (double t, int barIdx, int pixel, int nPix, int /*nBars*/) noexcept
{
    // Classic meter: green → yellow → red up the bar. Each bar bounces to a
    // re-hashed peak every beat and decays toward its floor.
    const float phase = static_cast<float> (std::fmod (t, 1.0));
    const int   beat  = static_cast<int> (t);
    const float peak  = 0.55f + 0.45f * hash01 (static_cast<std::uint32_t> (beat * 4 + barIdx));
    const float level = peak * (1.0f - 0.8f * phase);
    const float y = static_cast<float> (pixel) / static_cast<float> (nPix);
    if (y > level)
        return { 0.0f, 0.0f, 0.0f };
    if (y < 0.60f)
        return { 0.0f, 1.0f, 0.0f };
    if (y < 0.85f)
        return { 1.0f, 0.9f, 0.0f };
    return { 1.0f, 0.0f, 0.0f };
}

RecipeRGB fire (double t, int barIdx, int pixel, int nPix, int /*nBars*/) noexcept
{
    // Flickering heat, hotter at the bottom. Two incommensurate sines with a
    // per-cell hashed phase stand in for noise; the heat value drives a
    // black → red → orange → yellow colour ramp.
    const float y = static_cast<float> (pixel) / static_cast<float> (nPix);
    const float ph = hash01 (static_cast<std::uint32_t> (barIdx * 64 + pixel));
    const float n1 = std::sin (kTwoPi * (static_cast<float> (t) * 1.7f + ph * 3.1f));
    const float n2 = std::sin (kTwoPi * (static_cast<float> (t) * 2.9f + ph * 7.7f) + 0.9f);
    const float flicker = 0.5f + 0.5f * (0.65f * n1 + 0.35f * n2);
    const float heat = (1.0f - y) * (0.55f + 0.45f * flicker);
    return { std::min (1.0f, heat * 2.2f),
             std::clamp (heat * 1.4f - 0.25f, 0.0f, 0.85f),
             std::max (0.0f, heat * 0.6f - 0.45f) };
}

RecipeRGB desert_breathe (double t, int barIdx, int pixel, int nPix, int /*nBars*/) noexcept
{
    // A slow drift across warm sand/dusk hues (16-beat hue cycle) with a
    // gentle 8-beat brightness swell, phase-offset per bar.
    const float h = 0.04f
                  + 0.06f * std::sin (kTwoPi * static_cast<float> (t) / 16.0f)
                  + 0.03f * static_cast<float> (pixel) / static_cast<float> (nPix);
    const float v = 0.45f + 0.35f * std::sin (kTwoPi * static_cast<float> (t) / 8.0f
                                              + static_cast<float> (barIdx) * 0.6f);
    const auto c = hueToRgb (h, v);
    // Knock the saturation back a touch so it reads sandy, not neon.
    return { c.r * 0.95f + v * 0.05f, c.g * 0.85f + v * 0.15f, c.b * 0.85f + v * 0.15f };
}

RecipeRGB vu_smooth (double t, int barIdx, int pixel, int nPix, int /*nBars*/) noexcept
{
    // Fluent VU meter: the level glides on layered sines, the colour is a
    // smooth gradient (dark green → yellow → red up the bar), and the whole
    // meter flashes on the beat.
    const float level = std::clamp (
        0.5f + 0.3f * std::sin (kTwoPi * (0.6f * static_cast<float> (t) + barIdx * 0.7f))
             + 0.2f * std::sin (kTwoPi * (1.3f * static_cast<float> (t) + barIdx * 1.7f)),
        0.05f, 1.0f);
    const float y = static_cast<float> (pixel) / static_cast<float> (nPix);
    if (y > level)
        return { 0.0f, 0.0f, 0.0f };

    // Smooth colour ramp by absolute height.
    RecipeRGB ramp;
    if (y < 0.5f) { const float u = y / 0.5f;          ramp = { u, 0.4f + 0.5f * u, 0.0f }; }
    else          { const float u = (y - 0.5f) / 0.5f; ramp = { 1.0f, 0.9f * (1.0f - u), 0.0f }; }

    // Flash on the beat (decays over the first fifth), never fully dark.
    const float ph    = static_cast<float> (std::fmod (t, 1.0));
    const float flash = 0.5f + 0.5f * std::max (0.0f, 1.0f - ph * 5.0f);
    return { ramp.r * flash, ramp.g * flash, ramp.b * flash };
}

RecipeRGB night_sky (double t, int barIdx, int pixel, int /*nPix*/, int /*nBars*/) noexcept
{
    // Deep-blue sky with a scattering of twinkling cool-white stars.
    const RecipeRGB base { 0.0f, 0.02f, 0.08f };
    const std::uint32_t id = static_cast<std::uint32_t> (barIdx * 64 + pixel);
    if (hash01 (id + 777u) > 0.18f)         // most cells are empty sky
        return base;
    const float ph = hash01 (id);
    const float tw = 0.5f + 0.5f * std::sin (kTwoPi * (0.5f * static_cast<float> (t) + ph * 4.0f));
    const float b  = tw * tw;
    return { base.r + b * 0.8f, base.g + b * 0.85f, std::min (1.0f, base.b + b) };
}

RecipeRGB skyline (double t, int barIdx, int pixel, int nPix, int /*nBars*/) noexcept
{
    // A city skyline at night: each bar is a building of random height; lit
    // windows glow warm amber and flicker slowly, the sky above stays dark.
    const std::uint32_t id = static_cast<std::uint32_t> (barIdx * 64 + pixel);
    const float bh = 0.4f + 0.5f * hash01 (static_cast<std::uint32_t> (barIdx) + 99u);  // building height
    const float y  = static_cast<float> (pixel) / static_cast<float> (nPix);
    if (y > bh)                              return { 0.0f, 0.0f, 0.01f };  // sky
    if (hash01 (id + 333u) >= 0.5f)          return { 0.02f, 0.012f, 0.0f }; // dark wall
    const float fl = std::sin (kTwoPi * (0.3f * static_cast<float> (t) + hash01 (id) * 5.0f));
    const float on = fl > -0.3f ? 1.0f : 0.18f;
    return { 1.0f * on, 0.68f * on, 0.24f * on };  // warm window
}

RecipeRGB embers (double t, int barIdx, int pixel, int nPix, int /*nBars*/) noexcept
{
    // Slow glowing embers — the fire palette, but calmer and longer-period.
    const float ph   = hash01 (static_cast<std::uint32_t> (barIdx * 64 + pixel));
    const float y    = static_cast<float> (pixel) / static_cast<float> (nPix);
    const float glow = 0.5f + 0.5f * std::sin (kTwoPi * (0.4f * static_cast<float> (t) + ph * 6.0f));
    const float heat = (1.0f - y * 0.5f) * (0.3f + 0.7f * glow);
    return { std::min (1.0f, heat * 1.6f), heat * heat * 0.6f, heat * heat * heat * 0.1f };
}

RecipeRGB plasma (double t, int barIdx, int pixel, int nPix, int nBars) noexcept
{
    // Classic shifting plasma field — three interfering sine planes mapped to
    // the hue wheel.
    const float x = static_cast<float> (barIdx) / static_cast<float> (std::max (1, nBars - 1));
    const float y = static_cast<float> (pixel - 1) / static_cast<float> (nPix);
    const float v = std::sin (x * 3.0f + static_cast<float> (t))
                  + std::sin (y * 4.0f - static_cast<float> (t) * 0.7f)
                  + std::sin ((x + y) * 3.0f + static_cast<float> (t) * 0.5f);
    return hueToRgb (0.5f + v / 6.0f, 0.9f);
}

RecipeRGB ocean (double t, int barIdx, int pixel, int nPix, int /*nBars*/) noexcept
{
    // Teal/blue water with shimmering wave crests rolling up the bars.
    const float ph      = hash01 (static_cast<std::uint32_t> (barIdx * 64 + pixel));
    const float y       = static_cast<float> (pixel - 1) / static_cast<float> (nPix);
    const float wave    = 0.5f + 0.5f * std::sin (kTwoPi * (y * 2.0f - 0.2f * static_cast<float> (t) + ph * 0.3f));
    const float shimmer = 0.7f + 0.3f * std::sin (kTwoPi * (0.5f * static_cast<float> (t) + ph * 3.0f));
    const float v = wave * shimmer;
    return { 0.0f, v * 0.6f, v };
}

RecipeRGB nebula (double t, int barIdx, int pixel, int nPix, int nBars) noexcept
{
    // Slow cosmic clouds drifting through purples and pinks.
    const float x = static_cast<float> (barIdx) / static_cast<float> (std::max (1, nBars - 1));
    const float y = static_cast<float> (pixel - 1) / static_cast<float> (nPix);
    const float cloud = std::sin (x * 2.0f + static_cast<float> (t) * 0.3f)
                      + std::sin (y * 3.0f - static_cast<float> (t) * 0.2f);
    const float v   = std::clamp (0.4f + 0.4f * cloud, 0.0f, 1.0f);
    const float hue = 0.78f + 0.1f * std::sin (kTwoPi * (0.1f * static_cast<float> (t)) + y * 3.0f);
    return hueToRgb (hue, v);
}


// ---- colour bank, second octave -------------------------------------------

RecipeRGB sunset (double t, int /*barIdx*/, int pixel, int nPix, int /*nBars*/) noexcept
{
    // Dusk gradient drifting up the bar: deep purple (top) → magenta → orange
    // (bottom), the whole band sinking slowly.
    const float y = static_cast<float> (pixel - 1) / static_cast<float> (nPix);
    const float h = 0.02f + 0.20f * y + 0.04f * std::sin (kTwoPi * static_cast<float> (t) / 10.0f);
    const float wrapped = h < 0.0f ? h + 1.0f : h;     // keep into the red/purple end
    const float hue = 0.92f + 0.16f * wrapped;          // ~violet → orange
    return hueToRgb (hue, 0.85f);
}

RecipeRGB forest (double t, int barIdx, int pixel, int nPix, int /*nBars*/) noexcept
{
    // Dappled greens with slow shifting light filtering through.
    const float ph    = hash01 (static_cast<std::uint32_t> (barIdx * 64 + pixel));
    const float dapple = 0.5f + 0.5f * std::sin (kTwoPi * (0.3f * static_cast<float> (t) + ph * 4.0f));
    const float hue   = 0.30f + 0.08f * std::sin (kTwoPi * (0.2f * static_cast<float> (t) + ph));
    return hueToRgb (hue, 0.35f + 0.55f * dapple);
}

RecipeRGB lava (double t, int barIdx, int pixel, int nPix, int /*nBars*/) noexcept
{
    // Thick molten flow — deep red base, slow bright orange cracks creeping.
    const float ph   = hash01 (static_cast<std::uint32_t> (barIdx * 64 + pixel));
    const float flow = std::sin (kTwoPi * (static_cast<float> (pixel) / nPix * 1.5f
                                          - 0.15f * static_cast<float> (t) + ph * 0.5f));
    const float hot  = std::clamp (0.35f + 0.65f * flow, 0.0f, 1.0f);
    return { 0.35f + 0.65f * hot, hot * hot * 0.55f, 0.0f };
}

RecipeRGB ice (double t, int barIdx, int pixel, int nPix, int /*nBars*/) noexcept
{
    // Cold crystalline cyan/white with a slow glassy shimmer.
    const float ph      = hash01 (static_cast<std::uint32_t> (barIdx * 64 + pixel));
    const float shimmer = 0.5f + 0.5f * std::sin (kTwoPi * (0.4f * static_cast<float> (t) + ph * 5.0f));
    const float v = 0.4f + 0.6f * shimmer;
    return { v * 0.6f, v * 0.85f, v };                  // icy blue-white
}

RecipeRGB candy (double t, int barIdx, int pixel, int nPix, int nBars) noexcept
{
    // Soft pastel rainbow scrolling gently.
    const float h = static_cast<float> (pixel - 1) / static_cast<float> (nPix)
                  + static_cast<float> (barIdx) * 0.05f - 0.15f * static_cast<float> (t);
    const auto c = hueToRgb (h, 1.0f);
    return { 0.5f + 0.5f * c.r, 0.5f + 0.5f * c.g, 0.5f + 0.5f * c.b };  // pastel
}

RecipeRGB toxic (double t, int barIdx, int pixel, int nPix, int /*nBars*/) noexcept
{
    // Bubbling radioactive green/yellow ooze.
    const float ph  = hash01 (static_cast<std::uint32_t> (barIdx * 64 + pixel));
    const float bub = std::sin (kTwoPi * (0.7f * static_cast<float> (t) + ph * 6.0f))
                    + std::sin (kTwoPi * (1.3f * static_cast<float> (t) + ph * 3.0f));
    const float v   = std::clamp (0.4f + 0.4f * bub, 0.0f, 1.0f);
    return { v * 0.7f, v, 0.0f };                        // acid green-yellow
}

RecipeRGB storm (double t, int barIdx, int pixel, int /*nPix*/, int /*nBars*/) noexcept
{
    // Cold grey-blue cloudbank with sudden white lightning flashes.
    const float ph   = hash01 (static_cast<std::uint32_t> (barIdx * 64 + pixel));
    const float murk = 0.12f + 0.10f * std::sin (kTwoPi * (0.2f * static_cast<float> (t) + ph * 2.0f));
    constexpr float period = 0.9f;
    const int   win   = static_cast<int> (t / period);
    const float local = static_cast<float> (t / period - win) * period;
    const float strike = hash01 (static_cast<std::uint32_t> (win) * 2654435761u + 5u) < 0.35f
                       ? std::exp (-local * 9.0f) : 0.0f;
    const float w = std::min (1.0f, murk + strike);
    return { w * 0.8f, w * 0.85f, std::min (1.0f, w + 0.1f) };
}

RecipeRGB galaxy (double t, int barIdx, int pixel, int nPix, int nBars) noexcept
{
    // Deep-space colour clouds with the occasional bright star.
    const float x = static_cast<float> (barIdx) / static_cast<float> (std::max (1, nBars - 1));
    const float y = static_cast<float> (pixel - 1) / static_cast<float> (nPix);
    const float cloud = 0.5f + 0.5f * std::sin (kTwoPi * (x * 1.3f + y * 0.7f + 0.08f * static_cast<float> (t)));
    const float hue   = 0.62f + 0.18f * cloud;           // blue → violet
    auto c = hueToRgb (hue, 0.25f + 0.4f * cloud);
    const std::uint32_t id = static_cast<std::uint32_t> (barIdx * 64 + pixel);
    if (hash01 (id + 909u) < 0.06f)                       // sparse stars
    {
        const float tw = 0.5f + 0.5f * std::sin (kTwoPi * (static_cast<float> (t) + hash01 (id) * 6.0f));
        const float s = tw * tw;
        c = { std::min (1.0f, c.r + s), std::min (1.0f, c.g + s), std::min (1.0f, c.b + s) };
    }
    return c;
}

RecipeRGB reef (double t, int barIdx, int pixel, int nPix, int /*nBars*/) noexcept
{
    // Coral-reef palette: warm corals and teals waving gently.
    const float ph   = hash01 (static_cast<std::uint32_t> (barIdx * 64 + pixel));
    const float wave = 0.5f + 0.5f * std::sin (kTwoPi * (static_cast<float> (pixel) / nPix * 1.5f
                                              - 0.2f * static_cast<float> (t) + ph));
    const float hue  = ph < 0.5f ? 0.03f + 0.05f * wave    // coral
                                 : 0.48f + 0.05f * wave;   // teal
    return hueToRgb (hue, 0.4f + 0.6f * wave);
}

RecipeRGB disco (double t, int barIdx, int pixel, int nPix, int /*nBars*/) noexcept
{
    // Saturday-night colour blocks: each bar segment flips to a new hue on a
    // steady pulse.
    const int   seg   = (pixel - 1) / 3;
    const int   beat  = static_cast<int> (t * 2.0);
    const float hue   = hash01 (static_cast<std::uint32_t> (beat * 16 + barIdx * 4 + seg));
    return hueToRgb (hue, 1.0f);
}

RecipeRGB twilight (double t, int /*barIdx*/, int pixel, int nPix, int /*nBars*/) noexcept
{
    // Calm vertical gradient through deep blue → indigo → rose, breathing.
    const float y = static_cast<float> (pixel - 1) / static_cast<float> (nPix);
    const float hue = 0.60f + 0.30f * y;                 // blue → violet → rose
    const float v   = 0.45f + 0.25f * std::sin (kTwoPi * static_cast<float> (t) / 6.0f);
    return hueToRgb (hue, v);
}

RecipeRGB heatmap (double t, int barIdx, int pixel, int nPix, int nBars) noexcept
{
    // Thermal-camera field: a moving intensity blob mapped blue→green→yellow→red.
    const float cx = 0.5f + 0.4f * std::sin (kTwoPi * static_cast<float> (t) / 5.0f);
    const float cy = 0.5f + 0.4f * std::sin (kTwoPi * static_cast<float> (t) / 7.0f + 1.0f);
    const float x  = static_cast<float> (barIdx) / static_cast<float> (std::max (1, nBars - 1));
    const float y  = static_cast<float> (pixel - 1) / static_cast<float> (nPix);
    const float dx = (x - cx) / 0.5f, dy = (y - cy) / 0.5f;
    const float heat = std::clamp (1.0f - std::sqrt (dx * dx + dy * dy), 0.0f, 1.0f);
    // blue(0) → cyan → green → yellow → red(1)
    const float hue = 0.66f * (1.0f - heat);
    return hueToRgb (hue, 0.2f + 0.8f * heat);
}


// ---- dispatcher ----------------------------------------------------------

namespace
{
// One table per feel-group; entry order MUST match the menu's row order and
// the per-octave note assignment (Chases at C0, Breathes C1, Wild C2,
// Multicolor C3).
constexpr std::array<DynamicFn, kNumChases> kChasesTable {{
    &chase_up,    // 24
    &chase_down,  // 25
    &ping_pong,   // 26
    &snake,       // 27
    &sweep_up,    // 28
    &sweep_down,  // 29
    &diag_up,     // 30
    &diag_down,   // 31
    &wave_train,  // 32
    &expand,      // 33
    &contract,    // 34
    &snake_h,     // 35
}};

constexpr std::array<DynamicFn, kNumBreathes> kBreathesTable {{
    &sine_wave,   // 36
    &breathe,     // 37
    &ripple,      // 38
    &halo,        // 39
    &moon_rise,   // 40
    &soft_ball,   // 41
    &aurora,      // 42
    &ripple_h,    // 43
    &bloom,       // 44
    &shimmer,     // 45
    &sway,        // 46
    &drift,       // 47
}};

constexpr std::array<DynamicFn, kNumWild> kWildTable {{
    &sparkle,       // 48
    nullptr,        // 49 strobe — global shutter applied at the DMX driver
    &alt_swap,      // 50
    &rain,          // 51
    &lightning,     // 52
    &static_noise,  // 53
    &glitch,        // 54
    &bounce,        // 55
    &zigzag,        // 56
    &sparkle_few,   // 57
    &fast_ball,     // 58
    &pong,          // 59
}};

constexpr std::array<DynamicColorFn, kNumColorDyn> kColorTable {{
    &rainbow_chase,   // 60  ── octave C3
    &comet,           // 61
    &vu_meter,        // 62
    &fire,            // 63
    &desert_breathe,  // 64
    &vu_smooth,       // 65
    &night_sky,       // 66
    &skyline,         // 67
    &embers,          // 68
    &plasma,          // 69
    &ocean,           // 70
    &nebula,          // 71
    &sunset,          // 72  ── octave C4
    &forest,          // 73
    &lava,            // 74
    &ice,             // 75
    &candy,           // 76
    &toxic,           // 77
    &storm,           // 78
    &galaxy,          // 79
    &reef,            // 80
    &disco,           // 81
    &twilight,        // 82
    &heatmap,         // 83
}};
}

DynamicFn getDynamicRecipe (int pitch) noexcept
{
    if (pitch >= kChasesStart && pitch < kChasesStart + kNumChases)
        return kChasesTable[static_cast<std::size_t> (pitch - kChasesStart)];
    if (pitch >= kBreathesStart && pitch < kBreathesStart + kNumBreathes)
        return kBreathesTable[static_cast<std::size_t> (pitch - kBreathesStart)];
    if (pitch >= kWildStart && pitch < kWildStart + kNumWild)
        return kWildTable[static_cast<std::size_t> (pitch - kWildStart)];
    return nullptr;
}

DynamicColorFn getColorRecipe (int pitch) noexcept
{
    if (! isColorDynPitch (pitch))
        return nullptr;
    return kColorTable[static_cast<std::size_t> (pitch - kColorDynStart)];
}

}  // namespace hitnotedmx
