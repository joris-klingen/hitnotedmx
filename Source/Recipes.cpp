#include "Recipes.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace hitnotedmx
{

namespace
{
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

float chase_h (double t, int barIdx, int /*pixel*/, int /*nPix*/, int nBars, float tail) noexcept
{
    // Horizontal Chase: whole COLUMNS sweep across the grid (left→right, one
    // full pass per beat) with the comet trail measured in bars — the lateral
    // counterpart of chase_up's one-pixel-at-a-time climb. Flip/Reverse turn
    // it around like the other chases.
    const int pos = static_cast<int> (t * nBars) % nBars;   // head column, 0-based
    return cometBrightness (behindWrapped (pos, barIdx, nBars, +1), nBars, tail);
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

float fountain (double t, int barIdx, int pixel, int nPix, int /*nBars*/, float tail) noexcept
{
    // Water jets shooting UP from the bottom: a head launches twice a beat
    // (two in flight, staggered per bar), rising fast with a trail behind it
    // and FADING toward the top, like a fountain losing energy. Lives in the
    // Wild bank (velocity = beat-speed there, `tail` arrives as 0), so the
    // trail defaults to a mid length; per-bar hashed phase keeps the jets
    // from firing together.
    const float ph       = hash01 (static_cast<std::uint32_t> (barIdx) * 2654435761u);
    const float trail    = tail > 0.0f ? tail : 0.5f;
    const float trailLen = 1.0f + trail * static_cast<float> (nPix) * 0.5f;
    const float top      = static_cast<float> (nPix) + 3.0f;     // head exits past the top
    float v = 0.0f;
    for (int k = 0; k < 2; ++k)
    {
        const double pos   = t + static_cast<double> (ph) + k * 0.5;        // staggered launches
        const float  local = static_cast<float> (pos - std::floor (pos));   // 0..1 within this jet's life
        const float  head  = local * top;                                   // climbs from 0 past the top
        const float  d     = head - static_cast<float> (pixel - 1);         // cell's distance below the head
        if (d < 0.0f || d > trailLen)
            continue;
        const float fade = std::max (0.0f, 1.0f - static_cast<float> (pixel) / static_cast<float> (nPix));
        v = std::max (v, (1.0f - d / trailLen) * fade);   // trail fade × dims toward the top
    }
    return v;
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
    // One crest passing per cycle — one bar at the bank's standard rate.
    const float phase = kTwoPi * (static_cast<float> (t) * 0.5f
                                  - static_cast<float> (pixel - 1) / nPix);
    return (1.0f + std::sin (phase)) * 0.5f;
}

float sparkle (double t, int barIdx, int pixel, int /*nPix*/, int /*nBars*/, float /*tail*/) noexcept
{
    // Random twinkle, fully decoupled per pixel — each cell runs its own rate
    // and phase, so cells pop and fade independently in time and space (no
    // coherent wave). Re-hashed per (cell, its own frame), fading out.
    const std::uint32_t id = static_cast<std::uint32_t> (barIdx * 64 + pixel);
    const float ph    = hash01 (id);
    const float rate  = 3.5f + 4.0f * hash01 (id + 99u);
    const float local = static_cast<float> (t) * rate + ph * 53.0f;
    const int   frame = static_cast<int> (local);
    const float frac  = local - static_cast<float> (frame);
    const float h = hash01 (id * 2654435761u + static_cast<std::uint32_t> (frame) * 40503u);
    return h > 0.72f ? (1.0f - frac) : 0.0f;
}

float tide (double t, int barIdx, int pixel, int nPix, int /*nBars*/, float /*tail*/) noexcept
{
    // A soft fill LEVEL that swells up the bars and recedes (one bar per
    // cycle — the bank's standard breathe rate), with a soft surface. Spatial
    // — distinct from a flat whole-rig fade. A slow per-bar wave tilts and
    // ripples the waterline diagonally across the bars (à la VU smooth) so it
    // isn't a flat horizontal line.
    const float y     = (static_cast<float> (pixel) - 0.5f) / static_cast<float> (nPix);   // 0 bottom..1 top
    const float base  = 0.30f + 0.50f * (0.5f - 0.5f * std::cos (kTwoPi * static_cast<float> (t) / 2.0f));
    const float wave  = 0.10f * std::sin (kTwoPi * (0.5f * static_cast<float> (t) + static_cast<float> (barIdx) * 0.6f));
    const float level = base + wave;
    return std::clamp ((level - y) / 0.12f + 1.0f, 0.0f, 1.0f);   // solid below the surface, soft above
}

float spiral (double t, int barIdx, int pixel, int nPix, int nBars, float tail) noexcept
{
    // Barber-pole helix: a diagonal stripe winding around the bars as it
    // climbs, scrolling UPWARD over time (default up, like the other chases;
    // Flip/Reverse turn it around). The trail (tail) thickens the stripe.
    const float turns = 1.5f;
    const float helix = static_cast<float> (barIdx) / static_cast<float> (nBars)
                      - static_cast<float> (pixel - 1) / static_cast<float> (nPix) * turns
                      + static_cast<float> (t) * 0.5f;
    const float frac  = helix - std::floor (helix);          // 0..1 around the pole
    const float width = 0.12f + 0.25f * tail;                // velocity → stripe thickness
    const float d     = std::min (frac, 1.0f - frac);
    return d < width ? 1.0f - d / width : 0.0f;
}

float burst (double t, int barIdx, int pixel, int nPix, int nBars, float /*tail*/) noexcept
{
    // A clean circle expanding from the grid centre outward, measured in REAL
    // cell units — so it's a true round front: the narrow sides (≈1.5 bars from
    // centre) saturate well before the top/bottom (≈8.5 px away), then it resets
    // and bursts again. Tight soft leading edge, solid fill behind.
    const float cx = static_cast<float> (nBars - 1) * 0.5f;     // centre bar  (1.5)
    const float cy = static_cast<float> (nPix  + 1) * 0.5f;     // centre pixel (9.5)
    const float dx = static_cast<float> (barIdx) - cx;
    const float dy = static_cast<float> (pixel)  - cy;
    const float dist   = std::sqrt (dx * dx + dy * dy);
    const float maxR   = std::sqrt (cx * cx + cy * cy);                       // centre → corner
    const float radius = static_cast<float> (std::fmod (t, 1.0)) * maxR * 1.05f;
    const float front  = radius - dist;
    return std::clamp (front / 0.6f, 0.0f, 1.0f);              // ~0.6-px soft edge, solid behind
}

float stutter (double t, int /*barIdx*/, int /*pixel*/, int /*nPix*/, int /*nBars*/, float /*tail*/) noexcept
{
    // Whole-grid rhythmic gate: on 1/16 steps, ~55% of which fire, each punching
    // then decaying fast within its step — a machine-gun stutter (distinct from
    // the driver strobe, which is a clean shutter). Per-bar patterns are done by
    // hand with the Bar selectors, so this stays whole-grid.
    const int   step = static_cast<int> (t * 4.0);
    const float frac = static_cast<float> (t * 4.0) - static_cast<float> (step);
    if (hash01 (static_cast<std::uint32_t> (step) * 2654435761u) >= 0.55f)
        return 0.0f;                              // this step is a rest
    return std::exp (-frac * 3.0f);               // punch on the step, quick decay
}

float waterfalls (double t, int barIdx, int pixel, int nPix, int /*nBars*/, float /*tail*/) noexcept
{
    // A continuous downward cascade: thin bright streaks stream DOWN each bar
    // with dark gaps between them, like sheets of water. Each bar gets its own
    // hashed phase so the four fall out of sync. Velocity (Wild beat-speed)
    // sets the fall rate. Squared crests keep the falls thin and the gaps dark.
    const float ph = hash01 (static_cast<std::uint32_t> (barIdx) * 2654435761u);
    const float yy = static_cast<float> (pixel) / static_cast<float> (nPix);          // 0 bottom .. 1 top
    const float s  = std::sin (kTwoPi * (3.0f * yy + 1.2f * static_cast<float> (t) + ph));
    const float w  = std::max (0.0f, s);
    return w * w;
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

float radar (double t, int barIdx, int pixel, int nPix, int nBars, float tail) noexcept
{
    // A radar sweep: a THIN (~1-px) spoke from the grid centre, rotating around,
    // with a velocity-driven fading trail behind it — so with some tail it reads
    // like a radar screen. Coords are normalised to a square for the angle (the
    // tall-narrow 4×18 footprint still stretches the rotation — experimental).
    // One turn per two beats.
    const float cx = static_cast<float> (nBars - 1) * 0.5f;
    const float cy = static_cast<float> (nPix  - 1) * 0.5f;
    const float x  = (nBars > 1) ? (static_cast<float> (barIdx)    - cx) / cx : 0.0f;
    const float y  = (nPix  > 1) ? (static_cast<float> (pixel - 1) - cy) / cy : 0.0f;
    const float ang    = std::atan2 (y, x) + kTwoPi * 0.5f;                  // 0..2π
    const float sweep  = static_cast<float> (std::fmod (t * 0.5, 1.0)) * kTwoPi;
    const float behind = std::fmod (sweep - ang + kTwoPi, kTwoPi);          // angle behind the spoke
    // ~0.10 rad head ≈ one pixel at the vertical rim; tail extends the trail.
    const float tailLen = 0.10f + tail * kTwoPi * 0.9f;
    const float b = 1.0f - behind / tailLen;
    return b > 0.0f ? b : 0.0f;
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
    // Soft rings expanding from the grid centre (two in flight, launched half
    // a cycle apart). Lives in TWO banks: as a Breathe (38) a full cycle is
    // one bar at the bank's standard rate; as a Chase (25) it runs 2× that,
    // a ring launching on every beat.
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
    // A soft ring of light around a centre that bounces up and down the bar
    // (up half a bar, down the other half at the bank's standard rate — Speed
    // vel 1 halves it to up-one-bar / down-the-next); the centre stays dark.
    const float centre = (0.5f + 0.5f * std::sin (kTwoPi * static_cast<float> (t) / 2.0f))
                       * static_cast<float> (nPix - 1) + 1.0f;
    const float radius = static_cast<float> (nPix) * 0.18f;
    const float d = std::fabs (static_cast<float> (pixel) - centre);
    const float b = 1.0f - std::fabs (d - radius) / (static_cast<float> (nPix) * 0.10f);
    return b > 0.0f ? b : 0.0f;
}


// ---- ambient breathes -----------------------------------------------------
// Gentle motion with smooth ramps for quiet sections; the periodic movers all
// cycle once per bar at the bank's standard rate (the free-running textures —
// shimmer, glow, aurora, soft ball — keep their own slow drift). exp()
// per pixel is fine here: 72 cells × once per audio block.

float moon_rise (double t, int barIdx, int pixel, int nPix, int nBars, float /*tail*/) noexcept
{
    // A soft gaussian glow climbing the bars — one full cycle per bar at the
    // bank's standard rate — starting below the bottom and drifting off the
    // top. TWO moons run half a period apart so the loop never goes fully
    // dark. Narrow (plenty of black around each moon), and the centre drifts
    // sideways per bar so each moon tracks a diagonal as it climbs rather
    // than a flat horizontal band.
    const float sigma = static_cast<float> (nPix) * 0.13f;   // narrower → more black between moons
    const float lean  = (static_cast<float> (barIdx) - static_cast<float> (nBars - 1) * 0.5f) * 1.6f;
    auto moon = [pixel, nPix, sigma, lean] (float phase)
    {
        const float centre = phase * (static_cast<float> (nPix) + 6.0f) - 3.0f + lean;
        const float d = (static_cast<float> (pixel) - centre) / sigma;
        return std::exp (-d * d);
    };
    const float p0 = static_cast<float> (std::fmod (t, 2.0)) / 2.0f;
    const float p1 = static_cast<float> (std::fmod (t + 1.0, 2.0)) / 2.0f;
    return std::max (moon (p0), moon (p1));
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

float theater (double t, int /*barIdx*/, int pixel, int /*nPix*/, int /*nBars*/, float tail) noexcept
{
    // Classic marquee / theater chase: every 3rd pixel lit, the pattern
    // marching up the bar (~6 steps per beat). `tail` (velocity) adds a soft
    // glow on the two cells trailing each lit one — tail 0 = crisp on/off.
    const int   p     = pixel - 1;
    const int   phase = static_cast<int> (t * 6.0);
    const int   m     = ((p - phase) % 3 + 3) % 3;     // 0 = a lit position
    if (m == 0)
        return 1.0f;
    // m == 2 is the cell just behind a lit one (brighter trail); m == 1 further.
    return tail * (m == 2 ? 0.4f : 0.15f);
}


// ---- breathes (fill) ------------------------------------------------------

float ripple_h (double t, int barIdx, int pixel, int nPix, int nBars, float /*tail*/) noexcept
{
    // Smooth, sparse concentric rings expanding from the grid centre, biased
    // horizontal (the pixel axis contributes only a little). Gaussian ring
    // profile; a ring crosses per half-cycle, one full cycle per bar at the
    // bank's standard rate.
    const float x = nBars > 1 ? static_cast<float> (barIdx) / (nBars - 1) : 0.5f;
    const float y = (static_cast<float> (pixel) - 0.5f) / static_cast<float> (nPix);
    const float dist = std::fabs (x - 0.5f) + 0.30f * std::fabs (y - 0.5f);
    float v = 0.0f;
    for (int k = 0; k < 2; ++k)
    {
        const float r = static_cast<float> (std::fmod (t * 0.35 + k * 0.35, 0.7));
        const float g = (dist - r) / 0.18f;     // wide, soft band
        v = std::max (v, std::exp (-g * g));
    }
    return v * 0.8f;   // hold it below full brightness — a gentle ring, never glaring
}

float bloom (double t, int barIdx, int pixel, int nPix, int nBars, float /*tail*/) noexcept
{
    // A soft disc that grows and shrinks from the grid centre, one cycle per
    // bar at the bank's standard rate.
    const float x = nBars > 1 ? static_cast<float> (barIdx) / (nBars - 1) : 0.5f;
    const float y = (static_cast<float> (pixel) - 0.5f) / static_cast<float> (nPix);
    const float dx = x - 0.5f, dy = y - 0.5f;
    const float dist   = std::sqrt (dx * dx + dy * dy);
    // Contracted floor raised (0.22) so it never collapses to a dot, and a wider
    // edge band (0.20) so the rim fades softly instead of a hard disc.
    const float radius = 0.22f + 0.32f * (0.5f - 0.5f * std::cos (kTwoPi * static_cast<float> (t) / 2.0f));
    return std::clamp ((radius - dist) / 0.20f + 1.0f, 0.0f, 1.0f);
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

float glow (double t, int barIdx, int pixel, int nPix, int /*nBars*/, float /*tail*/) noexcept
{
    // The calmest twinkle in the bank: two VERY slow incommensurate sines per
    // pixel (lower frequencies than `shimmer`) with a hashed phase, so it drifts
    // with super-soft transients and never pops. A touch more shimmer than
    // before, plus a slow spatial gradient drifting up the bars so it reads as a
    // breathing wash with gentle banding rather than a flat field.
    const float ph   = hash01 (static_cast<std::uint32_t> (barIdx * 64 + pixel));
    const float y    = (static_cast<float> (pixel) - 0.5f) / static_cast<float> (nPix);
    const float tw   = 0.26f * std::sin (kTwoPi * (0.12f * static_cast<float> (t) + ph))
                     + 0.15f * std::sin (kTwoPi * (0.07f * static_cast<float> (t) + ph * 1.7f));
    const float grad = 0.13f * std::sin (kTwoPi * (0.05f * static_cast<float> (t) - y * 0.8f
                                                  + static_cast<float> (barIdx) * 0.2f));
    return std::clamp (0.55f + tw + grad, 0.22f, 1.0f);   // never fully dark
}

float drift (double t, int barIdx, int pixel, int nPix, int nBars, float /*tail*/) noexcept
{
    // A wide soft diagonal band drifting up the rig, one pass per bar at the
    // bank's standard rate.
    const float d    = static_cast<float> (pixel - 1) / static_cast<float> (nPix)
                     + static_cast<float> (barIdx) / static_cast<float> (nBars) * 0.5f;
    const float head = static_cast<float> (std::fmod (t * 0.75, 1.5));
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
    // Same decoupled per-pixel twinkle as sparkle, but far sparser (only a
    // handful lit at once) and a little slower.
    const std::uint32_t id = static_cast<std::uint32_t> (barIdx * 64 + pixel);
    const float ph    = hash01 (id);
    const float rate  = 2.5f + 2.5f * hash01 (id + 99u);
    const float local = static_cast<float> (t) * rate + ph * 53.0f;
    const int   frame = static_cast<int> (local);
    const float frac  = local - static_cast<float> (frame);
    const float h = hash01 (id * 2654435761u + static_cast<std::uint32_t> (frame) * 40503u);
    return h > 0.92f ? (1.0f - frac) : 0.0f;   // ~8% of cells, fading out
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

float pong (double t, int barIdx, int pixel, int nPix, int nBars, float tail) noexcept
{
    // Atari Pong across the whole 4×18 grid: ONE ball drifts diagonally
    // (fast vertical bounce, slow horizontal drift — both triangle waves so
    // the speed is constant and reflections are sharp), bouncing between a
    // top and a bottom paddle. Each paddle is 2 wide × 1 tall and slides
    // sideways to track the ball's column.
    const float tf  = static_cast<float> (t);
    const float py  = tf * 0.5f;   // one full bounce per 2 beats → reaches a paddle on each beat
    const float px  = tf * 0.37f + 0.25f;
    const float vYf = 2.0f * std::fabs (py - std::floor (py + 0.5f));   // 0(bottom)..1(top)
    const float vXf = 2.0f * std::fabs (px - std::floor (px + 0.5f));   // 0..1..0 horizontal

    const float ballXf = vXf * static_cast<float> (nBars - 1);
    const int   ballY  = static_cast<int> (2.0f + vYf * static_cast<float> (nPix - 3) + 0.5f);  // rows 2..nPix-1
    const int   ballX  = static_cast<int> (ballXf + 0.5f);                                      // bars 0..nBars-1

    // Each paddle drifts on its OWN slow path, but commits to the ball's
    // column as the ball nears its end — so the two read as independent
    // players that only dart across on their own turn.
    const float topDrift = (0.5f + 0.5f * std::sin (kTwoPi * tf * 0.23f))          * static_cast<float> (nBars - 1);
    const float botDrift = (0.5f + 0.5f * std::sin (kTwoPi * (tf * 0.17f + 0.37f))) * static_cast<float> (nBars - 1);
    const float wTop = vYf * vYf;                       // commit only when ball is high
    const float wBot = (1.0f - vYf) * (1.0f - vYf);     // ...or low
    const int   topX = static_cast<int> (topDrift * (1.0f - wTop) + ballXf * wTop + 0.5f);
    const int   botX = static_cast<int> (botDrift * (1.0f - wBot) + ballXf * wBot + 0.5f);

    const int topLeft = std::min (std::max (topX, 0), nBars - 2);   // 2-wide windows
    const int botLeft = std::min (std::max (botX, 0), nBars - 2);

    // Paddles live on the very top and bottom rows.
    if (pixel == nPix)  return (barIdx == topLeft || barIdx == topLeft + 1) ? 1.0f : 0.0f;
    if (pixel == 1)     return (barIdx == botLeft || barIdx == botLeft + 1) ? 1.0f : 0.0f;

    // The ball, with a velocity-driven trail behind it (single pixel at tail 0).
    if (barIdx != ballX)
        return 0.0f;
    const float saw    = py - std::floor (py + 0.5f);              // -0.5..0.5 (sign = up/down)
    const int   behind = saw > 0.0f ? (ballY - pixel) : (pixel - ballY);   // steps behind the head
    if (behind < 0) return 0.0f;
    if (behind == 0) return 1.0f;
    const float tailLen = 1.0f + tail * static_cast<float> (nPix) * 0.4f;
    const float b = 1.0f - static_cast<float> (behind) / tailLen;
    return b > 0.0f ? b : 0.0f;
}


// ---- colour bank (Multicolor) ----------------------------------------------
// These return RGB already scaled by their own brightness; computeDmx routes
// them in place of the palette colour.

RecipeRGB rainbow_chase (double t, int /*barIdx*/, int pixel, int nPix, int /*nBars*/, float /*param*/) noexcept
{
    // The full hue wheel laid along the bar, scrolling upward (one wheel
    // revolution every 2 beats).
    const float h = static_cast<float> (pixel - 1) / static_cast<float> (nPix)
                  - static_cast<float> (t) * 0.5f;
    return hueToRgb (h, 1.0f);
}

RecipeRGB comet (double t, int /*barIdx*/, int pixel, int nPix, int /*nBars*/, float /*param*/) noexcept
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

RecipeRGB vu_meter (double t, int /*barIdx*/, int pixel, int nPix, int /*nBars*/, float gain) noexcept
{
    // Smooth meter, UNIFORM across all bars, beat-LOCKED (t isn't velocity-
    // scaled — see computeDmx). Velocity sets the RANGE: it snaps to `peak` on
    // each beat, then releases fast back to a 2-pixel green floor within the
    // beat. `peak` is sqrt(gain) for a natural meter feel, plus extra headroom
    // that only kicks in near full velocity (the gain^22 term) so a 127 hit
    // overshoots well past the top and stays PINNED in the red for ~1/3 of the
    // beat — reliably visible — while mid velocities still read as a partial
    // meter (vel ~100 ≈ 16/18 pixels).
    const float floorLvl = 1.0f / static_cast<float> (nPix);   // rest = the single lowest LED
    const float g        = std::clamp (gain, 0.0f, 1.0f);
    const float peak     = std::sqrt (g) * (1.0f + 2.2f * std::pow (g, 22.0f));
    const float ph       = static_cast<float> (std::fmod (t, 1.0));
    const float level    = floorLvl + (peak - floorLvl) * std::exp (-ph * 4.0f);
    const float y        = static_cast<float> (pixel) / static_cast<float> (nPix);
    if (y > level)
        return { 0.0f, 0.0f, 0.0f };
    // At very low signal the lone floor LED DIMS instead of holding full green;
    // brightness rises to full within ~2 pixels of the floor.
    const float bri = std::clamp (0.30f + (level - floorLvl) * 6.0f, 0.30f, 1.0f);
    if (y < 0.5f) { const float u = y / 0.5f;          return { u * bri, (0.4f + 0.5f * u) * bri, 0.0f }; }
    const float u = (y - 0.5f) / 0.5f;                 return { bri, 0.9f * (1.0f - u) * bri, 0.0f };
}

RecipeRGB fire (double t, int barIdx, int pixel, int nPix, int /*nBars*/, float /*param*/) noexcept
{
    // Flickering heat, hotter at the bottom. Two incommensurate sines with a
    // per-cell hashed phase stand in for noise; the heat value drives a
    // black → red → orange → yellow colour ramp.
    const float y = static_cast<float> (pixel) / static_cast<float> (nPix);
    const float ph = hash01 (static_cast<std::uint32_t> (barIdx * 64 + pixel));
    const float n1 = std::sin (kTwoPi * (static_cast<float> (t) * 1.7f + ph * 3.1f));
    const float n2 = std::sin (kTwoPi * (static_cast<float> (t) * 2.9f + ph * 7.7f) + 0.9f);
    const float flicker = 0.5f + 0.5f * (0.65f * n1 + 0.35f * n2);
    // A tiny ±1-pixel wave on the flame height, phase-offset per bar so it
    // ripples across the rig — the top edge dances and the very top pixel
    // lights faintly at the wave's crests, giving the flames some movement.
    const float lift = (1.0f / static_cast<float> (nPix))
                     * std::sin (kTwoPi * (1.3f * static_cast<float> (t) + static_cast<float> (barIdx) * 0.35f));
    const float heat = std::max (0.0f, 1.0f - y + lift) * (0.55f + 0.45f * flicker);
    return { std::min (1.0f, heat * 2.2f),
             std::clamp (heat * 1.4f - 0.25f, 0.0f, 0.85f),
             std::max (0.0f, heat * 0.6f - 0.45f) };
}

RecipeRGB desert_breathe (double t, int barIdx, int pixel, int nPix, int /*nBars*/, float /*param*/) noexcept
{
    // A slow drift across warm sand hues, leaning YELLOW (16-beat hue cycle)
    // with a gentle 8-beat brightness swell phase-offset per bar, plus a slow
    // dune wave rolling up the bars so the sand visibly moves vertically
    // instead of only pulsing in place.
    const float y = (static_cast<float> (pixel) - 1.0f) / static_cast<float> (nPix);
    const float h = 0.09f
                  + 0.05f * std::sin (kTwoPi * static_cast<float> (t) / 16.0f)
                  + 0.03f * y;
    float v = 0.45f + 0.25f * std::sin (kTwoPi * static_cast<float> (t) / 8.0f
                                       + static_cast<float> (barIdx) * 0.6f);
    // Dune wave: a soft brightness band drifting up the bar.
    v += 0.18f * std::sin (kTwoPi * (y * 1.4f - 0.25f * static_cast<float> (t))
                           + static_cast<float> (barIdx) * 0.8f);
    // Per-pixel shimmer (±12%) so the sand glitters.
    const float ph = hash01 (static_cast<std::uint32_t> (barIdx * 64 + pixel));
    v *= 0.88f + 0.12f * std::sin (kTwoPi * (0.8f * static_cast<float> (t) + ph * 5.0f));
    v = std::clamp (v, 0.0f, 1.0f);
    const auto c = hueToRgb (h, v);
    // Knock the saturation back a touch so it reads sandy, not neon.
    return { c.r * 0.95f + v * 0.05f, c.g * 0.85f + v * 0.15f, c.b * 0.85f + v * 0.15f };
}

RecipeRGB vu_smooth (double t, int barIdx, int pixel, int nPix, int /*nBars*/, float /*param*/) noexcept
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

RecipeRGB night_sky (double t, int barIdx, int pixel, int /*nPix*/, int /*nBars*/, float /*param*/) noexcept
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

RecipeRGB police (double t, int barIdx, int pixel, int nPix, int nBars, float /*param*/) noexcept
{
    // Mostly red, with an intensity gradient floating over the whole grid; then
    // scattered bright-blue 2×2 pulses flash through in the top half.
    const float x = nBars > 1 ? static_cast<float> (barIdx) / static_cast<float> (nBars - 1) : 0.5f;
    const float y = static_cast<float> (pixel - 1) / static_cast<float> (nPix);
    const float wave = 0.5f + 0.5f * std::sin (kTwoPi * (x * 0.7f + y * 1.1f + 0.3f * static_cast<float> (t)));
    const RecipeRGB red = { 0.4f + 0.6f * wave, 0.0f, 0.0f };

    if (pixel > nPix / 2)
    {
        for (int s = 0; s < 4; ++s)                              // a few scattered blue pulses
        {
            const float ph  = static_cast<float> (t) * 2.5f + static_cast<float> (s) * 0.37f;
            const int   cyc = static_cast<int> (ph);
            if (ph - static_cast<float> (cyc) >= 0.4f)
                continue;                                        // each flashes ~40% of its cycle
            const std::uint32_t hid = static_cast<std::uint32_t> (s * 131 + cyc * 7919);
            const int bb = static_cast<int> (hash01 (hid)       * static_cast<float> (nBars - 1));         // 2-wide
            const int bp = nPix / 2 + 1 + static_cast<int> (hash01 (hid + 55u) * static_cast<float> (nPix / 2 - 1)); // 2-tall, top half
            if ((barIdx == bb || barIdx == bb + 1) && (pixel == bp || pixel == bp + 1))
                return { 0.1f, 0.1f, 1.0f };                     // blue pulse through
        }
    }
    return red;
}

RecipeRGB embers (double t, int barIdx, int pixel, int nPix, int /*nBars*/, float /*param*/) noexcept
{
    // Slow glowing embers — the fire palette, but calmer and longer-period.
    const float ph   = hash01 (static_cast<std::uint32_t> (barIdx * 64 + pixel));
    const float y    = static_cast<float> (pixel) / static_cast<float> (nPix);
    const float glow = 0.5f + 0.5f * std::sin (kTwoPi * (0.4f * static_cast<float> (t) + ph * 6.0f));
    const float heat = (1.0f - y * 0.5f) * (0.3f + 0.7f * glow);
    return { std::min (1.0f, heat * 1.6f), heat * heat * 0.6f, heat * heat * heat * 0.1f };
}

RecipeRGB plasma (double t, int barIdx, int pixel, int nPix, int nBars, float /*param*/) noexcept
{
    // A cool "flame" field: three interfering sine planes drive BRIGHTNESS
    // (bright tongues over dark troughs) while the hue rides a fixed vertical
    // gradient — blue at the bottom, purple at the top. The time terms run at
    // fire's flicker rate (1.7 & 2.9 cycles/beat) with a per-cell hashed phase,
    // so it shimmers at exactly the same speed and per-pixel as `fire`.
    const float x  = static_cast<float> (barIdx) / static_cast<float> (std::max (1, nBars - 1));
    const float y  = static_cast<float> (pixel - 1) / static_cast<float> (nPix);
    const float ph = hash01 (static_cast<std::uint32_t> (barIdx * 64 + pixel));
    const float v = std::sin (x * 3.0f       + kTwoPi * (static_cast<float> (t) * 1.7f + ph * 3.1f))
                  + std::sin (y * 4.0f       - kTwoPi * (static_cast<float> (t) * 2.9f + ph * 7.7f))
                  + std::sin ((x + y) * 3.0f + kTwoPi * (static_cast<float> (t) * 2.2f + ph * 5.0f));
    const float flame = 0.5f + 0.5f * (v / 3.0f);                          // 0..1
    const float bri   = std::clamp (0.15f + 0.9f * flame * flame, 0.0f, 1.0f);  // dark troughs, bright tongues
    const float hue   = 0.66f + 0.15f * y + 0.03f * flame;                // blue → purple, wobbling
    return hueToRgb (hue, bri);
}

RecipeRGB ocean (double t, int barIdx, int pixel, int nPix, int /*nBars*/, float /*param*/) noexcept
{
    // Teal/blue water with shimmering wave crests rolling up the bars.
    const float ph      = hash01 (static_cast<std::uint32_t> (barIdx * 64 + pixel));
    const float y       = static_cast<float> (pixel - 1) / static_cast<float> (nPix);
    const float wave    = 0.5f + 0.5f * std::sin (kTwoPi * (y * 2.0f - 0.2f * static_cast<float> (t) + ph * 0.3f));
    const float shimmer = 0.7f + 0.3f * std::sin (kTwoPi * (0.5f * static_cast<float> (t) + ph * 3.0f));
    const float v = wave * shimmer;
    return { 0.0f, v * 0.6f, v };
}

RecipeRGB nebula (double t, int barIdx, int pixel, int nPix, int nBars, float /*param*/) noexcept
{
    // Slow cosmic clouds drifting through purples and pinks. Floor keeps the
    // troughs dimly lit — never black; carve holes with a breathe on top.
    const float x = static_cast<float> (barIdx) / static_cast<float> (std::max (1, nBars - 1));
    const float y = static_cast<float> (pixel - 1) / static_cast<float> (nPix);
    const float cloud = std::sin (x * 2.0f + static_cast<float> (t) * 0.3f)
                      + std::sin (y * 3.0f - static_cast<float> (t) * 0.2f);
    const float v   = std::clamp (0.4f + 0.4f * cloud, 0.18f, 1.0f);
    const float hue = 0.78f + 0.1f * std::sin (kTwoPi * (0.1f * static_cast<float> (t)) + y * 3.0f);
    return hueToRgb (hue, v);
}


// ---- colour bank, second octave -------------------------------------------

RecipeRGB sunset (double t, int barIdx, int pixel, int nPix, int /*nBars*/, float /*param*/) noexcept
{
    // Smooth dusk gradient up the bar: soft amber (bottom) → magenta → violet
    // (top). The warm low end is gently dimmed and desaturated so it glows
    // instead of glaring, and the whole band drifts slowly.
    const float y     = static_cast<float> (pixel - 1) / static_cast<float> (nPix);  // 0 bottom..1 top
    const float drift = 0.02f * std::sin (kTwoPi * static_cast<float> (t) / 12.0f);
    const float hue   = 0.06f - 0.28f * y + drift;     // amber → (red) → magenta → violet (wraps)
    float       v     = 0.6f + 0.25f * y;              // a touch softer toward the bottom
    // Per-pixel shimmer (±14%) so the dusk band glitters gently.
    const float ph    = hash01 (static_cast<std::uint32_t> (barIdx * 64 + pixel));
    v *= 0.86f + 0.14f * std::sin (kTwoPi * (0.5f * static_cast<float> (t) + ph * 4.0f));
    const auto  c     = hueToRgb (hue, v);
    const float wash  = 0.32f * (1.0f - y);            // desaturate the warm low end
    return { c.r + wash * (v - c.r), c.g + wash * (v - c.g), c.b + wash * (v - c.b) };
}

RecipeRGB forest (double t, int barIdx, int pixel, int nPix, int /*nBars*/, float /*param*/) noexcept
{
    // Dappled greens with slow shifting light filtering through.
    const float ph    = hash01 (static_cast<std::uint32_t> (barIdx * 64 + pixel));
    const float dapple = 0.5f + 0.5f * std::sin (kTwoPi * (0.3f * static_cast<float> (t) + ph * 4.0f));
    const float hue   = 0.30f + 0.08f * std::sin (kTwoPi * (0.2f * static_cast<float> (t) + ph));
    return hueToRgb (hue, 0.35f + 0.55f * dapple);
}

RecipeRGB lava (double t, int barIdx, int pixel, int nPix, int /*nBars*/, float /*param*/) noexcept
{
    // Thick molten flow — deep red base, slow bright orange cracks creeping.
    const float ph   = hash01 (static_cast<std::uint32_t> (barIdx * 64 + pixel));
    const float flow = std::sin (kTwoPi * (static_cast<float> (pixel) / nPix * 1.5f
                                          - 0.15f * static_cast<float> (t) + ph * 0.5f));
    const float hot  = std::clamp (0.35f + 0.65f * flow, 0.0f, 1.0f);
    return { 0.35f + 0.65f * hot, hot * hot * 0.55f, 0.0f };
}

RecipeRGB borealis (double t, int barIdx, int pixel, int nPix, int nBars, float /*param*/) noexcept
{
    // Aurora curtain: slow vertical waves shimmering through green → teal →
    // violet, brighter toward the top like a sky curtain, drifting hue. Floor
    // keeps the troughs dimly lit — never black; mask with a breathe instead.
    const float y    = static_cast<float> (pixel - 1) / static_cast<float> (nPix);
    const float x    = static_cast<float> (barIdx) / static_cast<float> (std::max (1, nBars - 1));
    const float wave = std::sin (kTwoPi * (y * 1.2f + 0.15f * static_cast<float> (t) + x * 0.6f))
                     + 0.5f * std::sin (kTwoPi * (y * 2.3f - 0.09f * static_cast<float> (t) + x * 1.7f));
    const float hue  = 0.45f + 0.16f * wave + 0.10f * std::sin (kTwoPi * 0.04f * static_cast<float> (t));
    const float v    = std::clamp (0.2f + 0.4f * y + 0.35f * wave, 0.18f, 1.0f);
    return hueToRgb (hue, v);
}

RecipeRGB rouge (double t, int barIdx, int pixel, int nPix, int /*nBars*/, float /*param*/) noexcept
{
    // Red carpet: a smooth deep-red wave climbing the bars, with bright white
    // camera-flash sparkles popping on individual pixels.
    const float y     = static_cast<float> (pixel - 1) / static_cast<float> (nPix);
    const float chase = 0.5f + 0.5f * std::sin (kTwoPi * (y * 1.0f - 0.4f * static_cast<float> (t)
                                                         + static_cast<float> (barIdx) * 0.08f));
    const float v     = 0.30f + 0.60f * chase;
    RecipeRGB c = { v, v * v * 0.08f, v * v * 0.04f };   // deep red, a hair of warmth at the peaks

    // White flashes: sparse, per pixel, a sharp pop that fades.
    const std::uint32_t id = static_cast<std::uint32_t> (barIdx * 64 + pixel);
    const float rate  = 2.0f + 2.0f * hash01 (id + 13u);
    const float local = static_cast<float> (t) * rate + hash01 (id) * 53.0f;
    const int   frame = static_cast<int> (local);
    const float frac  = local - static_cast<float> (frame);
    if (hash01 (id * 2654435761u + static_cast<std::uint32_t> (frame) * 40503u) > 0.94f)
    {
        const float f  = 1.0f - frac;
        const float fl = f * f;                          // sharp flash
        c = { std::min (1.0f, c.r + fl), std::min (1.0f, c.g + fl), std::min (1.0f, c.b + fl) };
    }
    return c;
}

RecipeRGB magma (double t, int barIdx, int pixel, int nPix, int /*nBars*/, float /*param*/) noexcept
{
    // Cracked molten rock: a near-black crust shot through with bright
    // red-orange fissures that glow and creep — mostly dark, hot at the peaks.
    const float ph   = hash01 (static_cast<std::uint32_t> (barIdx * 64 + pixel));
    const float flow = std::sin (kTwoPi * (static_cast<float> (pixel) / nPix * 1.2f
                                          - 0.18f * static_cast<float> (t) + ph * 0.7f))
                     + 0.6f * std::sin (kTwoPi * (0.5f * static_cast<float> (t) + ph * 4.0f));
    const float heat = std::clamp ((flow - 0.4f) * 1.3f, 0.0f, 1.0f);   // dark crust, hot cracks
    return { std::min (1.0f, heat * 1.8f),
             std::clamp (heat * heat * 1.5f - 0.1f, 0.0f, 1.0f),
             std::max (0.0f, heat * heat * heat * 0.8f - 0.1f) };
}

RecipeRGB storm (double t, int barIdx, int pixel, int /*nPix*/, int /*nBars*/, float /*param*/) noexcept
{
    // Cold cloudbank with sudden white lightning flashes. The non-lightning
    // cloud sits as a DARK, blue-dominant murk (so the strikes read brighter
    // against it) rather than a bright grey-blue.
    const float ph   = hash01 (static_cast<std::uint32_t> (barIdx * 64 + pixel));
    const float murk = 0.05f + 0.05f * (0.5f + 0.5f * std::sin (kTwoPi * (0.2f * static_cast<float> (t) + ph * 2.0f)));
    constexpr float period = 0.9f;
    const int   win   = static_cast<int> (t / period);
    const float local = static_cast<float> (t / period - win) * period;
    const float strike = hash01 (static_cast<std::uint32_t> (win) * 2654435761u + 5u) < 0.35f
                       ? std::exp (-local * 9.0f) : 0.0f;
    // Dark blue base cloud; the white strike adds on top.
    return { std::min (1.0f, murk * 0.25f + strike),
             std::min (1.0f, murk * 0.50f + strike),
             std::min (1.0f, murk * 1.10f + strike) };
}

RecipeRGB galaxy (double t, int barIdx, int pixel, int nPix, int nBars, float /*param*/) noexcept
{
    // Very subtle deep blue→violet background, with at most a handful of slow
    // white stars that fade in and out independently and reappear elsewhere.
    const float gx   = nBars > 1 ? static_cast<float> (barIdx) / static_cast<float> (nBars - 1) : 0.5f;
    const float y    = static_cast<float> (pixel - 1) / static_cast<float> (nPix);
    const float wash = 0.5f + 0.5f * std::sin (kTwoPi * (0.05f * static_cast<float> (t) + gx * 0.6f + y * 0.4f));
    RecipeRGB c = hueToRgb (0.72f + 0.05f * y + 0.02f * wash,        // violet, drifting slightly
                            0.14f + 0.14f * y + 0.06f * wash);       // subtle moving gradient

    // A faint per-pixel shimmer over the violet wash so the purple parts glitter
    // gently (±16%) — separate from, and beneath, the white stars.
    const float sph  = hash01 (static_cast<std::uint32_t> (barIdx * 64 + pixel));
    const float shim = 0.84f + 0.16f * std::sin (kTwoPi * (0.4f * static_cast<float> (t) + sph * 5.0f));
    c = { c.r * shim, c.g * shim, c.b * shim };

    // Five star "slots". Each lives one cycle (~1 bar at velocity 100, since
    // velocity scales the clock), fading 0→1→0, then re-rolls to a new cell.
    constexpr int   kStars = 5;
    constexpr float period = 6.25f;   // 4 beats × (100/64): one bar per cycle at vel 100
    for (int s = 0; s < kStars; ++s)
    {
        const float ph    = static_cast<float> (t) / period + static_cast<float> (s) * 0.2f;
        const int   cycle = static_cast<int> (ph);
        const float frac  = ph - static_cast<float> (cycle);                 // 0..1 lifetime
        const std::uint32_t hid = static_cast<std::uint32_t> (s * 101 + cycle * 7919);
        const int   sb = static_cast<int> (hash01 (hid) * static_cast<float> (nBars));
        const int   sp = 1 + static_cast<int> (hash01 (hid + 55u) * static_cast<float> (nPix));
        if (barIdx == sb && pixel == sp)
        {
            const float env  = std::sin (frac * 3.14159265f);   // fade in → out
            const float star = env * env;
            c = { std::min (1.0f, c.r + star), std::min (1.0f, c.g + star), std::min (1.0f, c.b + star) };
        }
    }
    return c;
}

RecipeRGB velvet (double t, int barIdx, int pixel, int nPix, int /*nBars*/, float /*param*/) noexcept
{
    // Deep purple/magenta slow vertical wash drifting up the bars, with sparse
    // cool-white glints catching like light on velvet — the glam sibling to
    // Rouge. Never blasts: the base stays rich and deep.
    const float y    = static_cast<float> (pixel - 1) / static_cast<float> (nPix);
    const float wave = 0.5f + 0.5f * std::sin (kTwoPi * (y * 1.2f - 0.2f * static_cast<float> (t)
                                                        + static_cast<float> (barIdx) * 0.10f));
    const float hue  = 0.82f + 0.06f * std::sin (kTwoPi * 0.05f * static_cast<float> (t));   // violet ↔ magenta
    const float v    = 0.25f + 0.55f * wave;
    RecipeRGB c = hueToRgb (hue, v);

    // Sparse cool-white glints, per pixel, fading.
    const std::uint32_t id = static_cast<std::uint32_t> (barIdx * 64 + pixel);
    const float rate  = 1.5f + 1.5f * hash01 (id + 7u);
    const float local = static_cast<float> (t) * rate + hash01 (id) * 53.0f;
    const int   frame = static_cast<int> (local);
    const float frac  = local - static_cast<float> (frame);
    if (hash01 (id * 2654435761u + static_cast<std::uint32_t> (frame) * 40503u) > 0.95f)
    {
        const float g = 1.0f - frac;
        c = { std::min (1.0f, c.r + g * 0.80f),
              std::min (1.0f, c.g + g * 0.85f),
              std::min (1.0f, c.b + g * 0.90f) };
    }
    return c;
}

RecipeRGB disco (double t, int barIdx, int pixel, int nPix, int nBars, float /*param*/) noexcept
{
    // A dancefloor, not a rainbow. Colours come from a SMALL bold palette
    // (red/amber/green/cyan/blue/magenta), never a continuous random hue — so
    // no muddy in-betweens. The grid is a 4×4 checker of SMALL blocks (each
    // 1/16 of the rig); every beat-step lights FOUR of them, each in its own
    // single colour — so ~1/4 of the grid flashes at a time and it reads like
    // separate club lights firing around the room rather than one big wash.
    constexpr float kPalette[6] = { 0.00f,   // red
                                    0.07f,   // amber
                                    0.33f,   // green
                                    0.50f,   // cyan
                                    0.66f,   // blue
                                    0.85f }; // magenta
    const int step = static_cast<int> (t * 2.0);   // aim changes ~twice per beat

    // Four lamps, each a 2×2-PIXEL square (two adjacent bars × two adjacent
    // pixels ≈ 4 LEDs, regardless of the grid shape) — one per grid QUADRANT,
    // so four are always on and they can never stack. Each lamp holds its aim
    // (square position + one palette colour) for TWO steps (one beat),
    // steadily FULL ON while aimed — no per-step pulsing. Left-side lamps
    // re-aim on even steps, right-side on odd, so ~50% of the lights persist
    // across any step. The incandescent feel lives in the TURN-OFF: the
    // square a lamp just left cools off exponentially over the following beat.
    const int qx = (barIdx * 2) / nBars;        // this cell's quadrant: 0 = left,   1 = right
    const int qy = ((pixel - 1) * 2) / nPix;    //                       0 = bottom, 1 = top
    const int  lamp  = qy * 2 + qx;
    const int  phase = lamp & 1;
    const auto epoch = static_cast<std::uint32_t> ((step + phase) / 2);
    const auto salt  = static_cast<std::uint32_t> (lamp) * 40503u;

    // Quadrant bounds: bars [bLo, bHi), rows [rLo, rHi] (1-based).
    const int bLo = qx == 0 ? 0 : nBars / 2;
    const int bHi = qx == 0 ? nBars / 2 : nBars;
    const int rLo = qy == 0 ? 1 : nPix / 2 + 1;
    const int rHi = qy == 0 ? nPix / 2 : nPix;

    // Is this cell inside the lamp's 2×2 aim for epoch `e`? The square is
    // placed fully inside the quadrant (clamped to 1-wide/-tall on degenerate
    // grids).
    auto lit = [&] (std::uint32_t e) -> bool
    {
        const int bar0 = bLo + static_cast<int> (hash01 (e * 2654435761u + salt)
                             * static_cast<float> (std::max (1, bHi - 1 - bLo)));
        const int pix0 = rLo + static_cast<int> (hash01 (e * 1013904223u + salt)
                             * static_cast<float> (std::max (1, rHi - rLo)));
        return barIdx >= bar0 && barIdx < std::min (bHi, bar0 + 2)
            && pixel  >= pix0 && pixel  < std::min (rHi + 1, pix0 + 2);
    };
    auto colFor = [salt] (std::uint32_t e) { return static_cast<int> (hash01 (e * 22695477u + salt) * 6.0f); };

    if (lit (epoch))
        return hueToRgb (kPalette[colFor (epoch)], 1.0f);   // steadily on while aimed

    // Turn-off tail: the square this lamp aimed at LAST epoch cools off.
    if (epoch > 0u && lit (epoch - 1u))
    {
        // Steps into the current epoch, 0..2 (the epoch began at 2e - phase).
        const float u = static_cast<float> (t * 2.0
                          - (2.0 * static_cast<double> (epoch) - phase));
        const float v = std::exp (-2.2f * u);
        if (v > 0.01f)
            return hueToRgb (kPalette[colFor (epoch - 1u)], v);
    }
    return { 0.0f, 0.0f, 0.0f };
}

RecipeRGB twilight (double t, int /*barIdx*/, int pixel, int nPix, int /*nBars*/, float /*param*/) noexcept
{
    // Calm vertical gradient through deep blue → indigo → rose, breathing.
    const float y = static_cast<float> (pixel - 1) / static_cast<float> (nPix);
    const float hue = 0.60f + 0.30f * y;                 // blue → violet → rose
    const float v   = 0.45f + 0.25f * std::sin (kTwoPi * static_cast<float> (t) / 6.0f);
    return hueToRgb (hue, v);
}

RecipeRGB heatmap (double t, int barIdx, int pixel, int nPix, int nBars, float /*param*/) noexcept
{
    // Thermal-camera field: a moving intensity blob mapped blue→green→yellow→red.
    const float cx = 0.5f + 0.4f * std::sin (kTwoPi * static_cast<float> (t) / 5.0f);
    const float cy = 0.5f + 0.4f * std::sin (kTwoPi * static_cast<float> (t) / 7.0f + 1.0f);
    const float x  = static_cast<float> (barIdx) / static_cast<float> (std::max (1, nBars - 1));
    const float y  = static_cast<float> (pixel - 1) / static_cast<float> (nPix);
    const float dx = (x - cx) / 0.95f, dy = (y - cy) / 0.5f;   // wider horizontal glow
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
// Order is grouped by motion/feel (vertical → diagonal → snakes → helix/wave →
// radial → game). MUST match the menu label lists in TriggerMenu.cpp.
constexpr std::array<DynamicFn, kNumChases> kChasesTable {{
    &chase_up,    // 24  (Chase — default up; Reverse note flips it)
    &ripple,      // 25  (Ripple — also in Breathes at 38; at chase speed a
                  //      ring launches on every beat. Ignores `tail`.)
    &ping_pong,   // 26
    &diag_up,     // 27  (Diag — default up; Reverse note flips it)
    &radar,       // 28  (Radar — thin spoke sweeping around the centre)
    &snake,       // 29
    &theater,     // 30
    &spiral,      // 31
    &wave_train,  // 32
    &expand,      // 33
    &contract,    // 34
    &chase_h,     // 35  (Chase H — whole columns sweeping across, one pass/beat)
}};

constexpr std::array<DynamicFn, kNumBreathes> kBreathesTable {{
    &tide,           // 36
    &sine_wave,      // 37
    &ripple,         // 38
    &ripple_h,       // 39
    &bloom,          // 40
    &halo,           // 41
    &moon_rise,      // 42
    &soft_ball,      // 43
    &drift,          // 44
    &aurora,         // 45
    &shimmer,        // 46
    &glow,           // 47  (was Smooth shimmer)
}};

constexpr std::array<DynamicFn, kNumWild> kWildTable {{
    nullptr,        // 48 strobe (C2) — global shutter applied at the DMX driver
    &sparkle,       // 49
    &sparkle_few,   // 50
    &lightning,     // 51
    &glitch,        // 52
    &fountain,      // 53  (was Static — jets rising from the bottom, fading at the top)
    &rain,          // 54
    &waterfalls,    // 55  (was Stutter)
    &bounce,        // 56
    &fast_ball,     // 57
    &pong,          // 58  (moved in from Chases; beat-synced here, was Zigzag)
    &burst,         // 59  (was Converge)
}};

constexpr std::array<DynamicColorFn, kNumColorDyn> kColorTable {{
    &rainbow_chase,   // 60  ── octave C3: meters → fire → water/nature
    &comet,           // 61
    &vu_meter,        // 62  (kept at offset 2 — velocity-gain exception)
    &vu_smooth,       // 63
    &fire,            // 64
    &embers,          // 65
    &magma,           // 66
    &lava,            // 67
    &heatmap,         // 68
    &ocean,           // 69
    &forest,          // 70
    &desert_breathe,  // 71
    &sunset,          // 72  ── octave C4: sky/space → party/abstract
    &twilight,        // 73
    &borealis,        // 74
    &night_sky,       // 75
    &galaxy,          // 76
    &nebula,          // 77
    &storm,           // 78
    &plasma,          // 79
    &police,          // 80
    &disco,           // 81
    &velvet,          // 82
    &rouge,           // 83
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
