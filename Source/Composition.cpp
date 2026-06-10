#include "Composition.h"

#include <algorithm>
#include <array>

#include "Palette.h"
#include "Recipes.h"
#include "Rig.h"

namespace hitnotedmx
{

// MIDI vocabulary — compiled into per-pitch lookup tables for O(1)
// audio-thread access.
//
// ─── Velocity semantics ────────────────────────────────────────────────────
// A note's velocity is overloaded: it means something different depending on
// which layer the note belongs to. All four interpretations are applied here
// in computeDmx (or in the recipe it dispatches to):
//
//   Layer                    Velocity controls
//   ──────────────────────   ───────────────────────────────────────────────
//   Bar / pixel-zone select  Palette ROUTE: >= 64 → primary, < 64 → secondary
//                            (kVelocityThreshold; see routeForVelocity)
//   Brightness dynamics      TAIL length of the comet head — soft = long trail
//                            (DynamicFn `tail` arg; section 5)
//   Colour dynamics          SPEED of the recipe's animation — hard = faster
//                            (scales the recipe's `t`; section 5 / 7)
//   Palette colour notes     INTENSITY (scale) + colour-FADE duration — hard =
//                            bright & instant, soft = dim & slow (pickColor /
//                            advanceFade)
//
// Spots and blackout ignore velocity. (When the C8 density / soft-edge note
// controls land, velocity will set their intensity — see TODO.)

namespace
{
constexpr int kVelocityThreshold = 64;

// Warm-white tint for the singer spots.
constexpr float kSpotWarmR = 0.4f;
constexpr float kSpotWarmG = 0.15f;

// ---- total blackout (pitch 0, C-2) --------------------------------------
// Lowest note in the vocabulary: a hard kill of the whole rig. The rest of
// the octave-0 triggers sit one semitone above it.
constexpr int kBlackoutNote = 0;


// ---- spot triggers (pitch 1..4) -----------------------------------------
struct SpotMapping
{
    int spotIdx;       // 0 or 1
    bool isWarmWhite;  // true = WW, false = secondary
};

constexpr int kSpotStart       = 1;
constexpr int kNumSpotTriggers = 4;

// pitch → (spotIdx, isWW). pitch 1=L WW, 2=L sec, 3=R WW, 4=R sec.
constexpr std::array<SpotMapping, kNumSpotTriggers> kSpotMap {{
    { 0, true  },
    { 0, false },
    { 1, true  },
    { 1, false },
}};

inline constexpr bool isSpotPitch (int p) { return p >= kSpotStart && p < kSpotStart + kNumSpotTriggers; }


// ---- bar selectors (pitch 5..8) -----------------------------------------
// Set membership per pitch, encoded as a 4-bit mask (bit n = bar n active).
// One pitch per bar; "all bars" was dropped — it's the default when no bar
// note is held, and the whole rig is trivially lit by holding all four.

constexpr int kBarSelStart = 5;
constexpr int kBarSelEnd   = 8;
constexpr int kNumBarSelectors = kBarSelEnd - kBarSelStart + 1;

constexpr std::array<std::uint8_t, kNumBarSelectors> kBarSelectorMask {{
    0b0001,  //  4: bar 1
    0b0010,  //  5: bar 2
    0b0100,  //  6: bar 3
    0b1000,  //  7: bar 4
}};

inline constexpr bool isBarSelPitch (int p) { return p >= kBarSelStart && p <= kBarSelEnd; }


// ---- pixel statics (pitch 12..22, the C-1 octave) -----------------------
// 18-bit mask: bit n (0..17) = pixel (n+1) selected.
//
// Nine zones (authored against the historical 9-pixel layout, each stretched
// to the two adjacent physical pixels 2z-1 / 2z) followed by Even / Odd combs
// that light every other physical pixel.

constexpr int kPixelStaticStart = 12;
constexpr int kPixelStaticEnd   = 23;
constexpr int kNumPixelStatics  = kPixelStaticEnd - kPixelStaticStart + 1;

constexpr std::uint32_t bit (int p)  // 1-based pixel → bit
{
    return static_cast<std::uint32_t> (1u << (p - 1));
}

constexpr std::uint32_t zone (int z)  // 1-based 9-zone index → its two pixels
{
    return bit (2 * z - 1) | bit (2 * z);
}

constexpr std::uint32_t everyN (int first, int step)  // first..18 step → comb mask
{
    std::uint32_t m = 0;
    for (int p = first; p <= kPixelsPerBar; p += step)
        m |= bit (p);
    return m;
}

constexpr std::array<std::uint32_t, kNumPixelStatics> kPixelStaticMask {{
    zone(1),          // 12
    zone(2),          // 13
    zone(3),          // 14
    zone(4),          // 15
    zone(5),          // 16
    zone(6),          // 17
    zone(7),          // 18
    zone(8),          // 19
    zone(9),          // 20
    everyN (2, 2),    // 21 even pixels   (2,4,…,18)
    everyN (1, 2),    // 22 odd pixels    (1,3,…,17)
    everyN (1, 3),    // 23 every 3rd     (1,4,7,10,13,16)
}};

inline constexpr bool isPixelStaticPitch (int p)
{
    return p >= kPixelStaticStart && p <= kPixelStaticEnd;
}


// ---- color routing -------------------------------------------------------
enum class Route : std::uint8_t { None, Primary, Secondary };

inline constexpr Route routeForVelocity (std::uint8_t vel)
{
    return vel >= kVelocityThreshold ? Route::Primary : Route::Secondary;
}


// ---- color picker --------------------------------------------------------
//
// The most-recently-started held note in the palette range wins. We also
// report the winning pitch + velocity so the fade stage (see advanceFade)
// can detect colour changes and derive the per-change fade duration.
struct ColorPick { float r, g, b, intensity; int pitch; std::uint8_t velocity; };

ColorPick pickColor (const MidiState& state,
                     int paletteStart,
                     int paletteEndExclusive) noexcept
{
    double mostRecent = -1.0;
    int mostRecentPitch = -1;
    std::uint8_t mostRecentVel = 0;

    state.forEachHeld ([&] (std::uint8_t pitch, const HeldNote& n)
    {
        if ((int) pitch < paletteStart || (int) pitch >= paletteEndExclusive)
            return;
        if (n.startBeat > mostRecent)
        {
            mostRecent = n.startBeat;
            mostRecentPitch = pitch;
            mostRecentVel = n.velocity;
        }
    });

    if (mostRecentPitch < 0)
        return { 0.0f, 0.0f, 0.0f, 0.0f, -1, 0 };

    const auto& c = kPalette[mostRecentPitch - paletteStart];
    const float intensity = static_cast<float> (mostRecentVel) / 127.0f;
    return { c.r, c.g, c.b, intensity, mostRecentPitch, mostRecentVel };
}

// Longest linear colour fade, reached at velocity 1; velocity 127 snaps.
constexpr double kMaxColorFadeSec = 3.0;

// Advance one palette channel's displayed colour one block toward its
// target. A change in the winning pitch starts a fresh fade from the
// colour currently on screen, with a duration set by the new note's
// velocity. When nothing is held (pitch < 0) the change is instant, so a
// plain release snaps to black — use the black palette note to fade out.
void advanceFade (ColorFadeState::Channel& c,
                  float tr, float tg, float tb,
                  int pitch, std::uint8_t vel, double dt) noexcept
{
    if (pitch != c.lastPitch)
    {
        c.lastPitch = pitch;
        c.start[0] = c.cur[0];
        c.start[1] = c.cur[1];
        c.start[2] = c.cur[2];
        c.elapsed  = 0.0;
        c.durSec   = (pitch < 0)
                   ? 0.0
                   : kMaxColorFadeSec * (1.0 - static_cast<double> (vel) / 127.0);
    }

    c.elapsed += dt;
    const double p = (c.durSec <= 0.0)
                   ? 1.0
                   : std::clamp (c.elapsed / c.durSec, 0.0, 1.0);
    const float fp = static_cast<float> (p);
    c.cur[0] = c.start[0] + (tr - c.start[0]) * fp;
    c.cur[1] = c.start[1] + (tg - c.start[1]) * fp;
    c.cur[2] = c.start[2] + (tb - c.start[2]) * fp;
}

}  // namespace


// Stable per-pixel gate value in [0, 1) for the pixel-density control.
// Deterministic from the pixel's grid position, so the SET of pixels passing
// a given density threshold is fixed frame-to-frame (no flicker) and shrinks
// monotonically as density falls.
//
// Built once at static init: a full-avalanche hash (murmur3 finalizer) of
// each cell's position, rank-normalised WITHIN its bar. The previous
// single-multiply linear hash dropped pixels in visible diagonal stripes
// (e.g. per-bar lit counts of 9/7/4/1 at 25%); the avalanche hash scatters
// randomly, and the per-bar rank guarantees every bar keeps the same
// fraction lit (25% → exactly 4–5 pixels per bar) while the drop order
// stays random.
namespace
{
const std::array<std::array<float, kPixelsPerBar + 1>, kNumBars> kDensityRank = []
{
    auto avalanche = [] (std::uint32_t h)
    {
        h ^= h >> 16;  h *= 0x7FEB352Du;
        h ^= h >> 15;  h *= 0x846CA68Bu;
        h ^= h >> 16;
        return h;
    };

    std::array<std::array<float, kPixelsPerBar + 1>, kNumBars> rank {};
    for (int b = 0; b < kNumBars; ++b)
    {
        std::array<std::uint32_t, kPixelsPerBar + 1> h {};
        for (int p = 1; p <= kPixelsPerBar; ++p)
            h[static_cast<size_t> (p)] = avalanche (static_cast<std::uint32_t> (b * 64 + p));
        for (int p = 1; p <= kPixelsPerBar; ++p)
        {
            int below = 0;
            for (int q = 1; q <= kPixelsPerBar; ++q)
                if (h[static_cast<size_t> (q)] < h[static_cast<size_t> (p)])
                    ++below;
            // +0.5 centres the rank in its bucket: density 1.0 passes every
            // pixel, density 0.0 passes none.
            rank[static_cast<size_t> (b)][static_cast<size_t> (p)] =
                (static_cast<float> (below) + 0.5f) / static_cast<float> (kPixelsPerBar);
        }
    }
    return rank;
}();
}

void computeDmx (const MidiState& state, double tBeats, DmxValues& out,
                 float ledMasterDim, float spotMasterDim,
                 ColorFadeState* fade, double dtSeconds, float pixelDensity) noexcept
{
    out.clear();

    // ---- 1. Blackout short-circuit --------------------------------------
    if (state.isActive (static_cast<std::uint8_t> (kBlackoutNote)))
    {
        // Hard kill — also collapse any in-flight fade so releasing the
        // blackout note resumes from black rather than mid-fade.
        if (fade != nullptr)
            fade->reset();
        return;
    }

    // ---- 2. Palettes -----------------------------------------------------
    const auto primaryC   = pickColor (state, kPrimaryPaletteStart,   kSecondaryPaletteStart);
    const auto secondaryC = pickColor (state, kSecondaryPaletteStart, kSecondaryPaletteEnd);

    // Target colours (palette colour scaled by note velocity = intensity).
    const float tPriR = primaryC.r   * primaryC.intensity;
    const float tPriG = primaryC.g   * primaryC.intensity;
    const float tPriB = primaryC.b   * primaryC.intensity;
    const float tSecR = secondaryC.r * secondaryC.intensity;
    const float tSecG = secondaryC.g * secondaryC.intensity;
    const float tSecB = secondaryC.b * secondaryC.intensity;

    float priR = tPriR, priG = tPriG, priB = tPriB;
    float secR = tSecR, secG = tSecG, secB = tSecB;

    // Linear velocity-driven fade toward the targets (snap if no state).
    if (fade != nullptr)
    {
        advanceFade (fade->primary,   tPriR, tPriG, tPriB,
                     primaryC.pitch,   primaryC.velocity,   dtSeconds);
        advanceFade (fade->secondary, tSecR, tSecG, tSecB,
                     secondaryC.pitch, secondaryC.velocity, dtSeconds);
        priR = fade->primary.cur[0];   priG = fade->primary.cur[1];   priB = fade->primary.cur[2];
        secR = fade->secondary.cur[0]; secG = fade->secondary.cur[1]; secB = fade->secondary.cur[2];
    }

    // ---- 3. Bar layer ----------------------------------------------------
    // For each bar, "highest-velocity utility note touching that bar wins
    // the color route." Encoded as per-bar route + per-bar best-vel-so-far.
    std::array<Route, kNumBars> barRoute   { Route::None, Route::None, Route::None, Route::None };
    std::array<int,   kNumBars> barBestVel { -1, -1, -1, -1 };
    bool barLayerHeld = false;

    state.forEachHeld ([&] (std::uint8_t pitch, const HeldNote& n)
    {
        if (! isBarSelPitch (pitch))
            return;
        barLayerHeld = true;
        const auto mask = kBarSelectorMask[pitch - kBarSelStart];
        const auto route = routeForVelocity (n.velocity);
        for (int b = 0; b < kNumBars; ++b)
        {
            if (! (mask & (1u << b)))
                continue;
            if (static_cast<int> (n.velocity) > barBestVel[b])
            {
                barBestVel[b] = n.velocity;
                barRoute[b]   = route;
            }
        }
    });

    // ---- 4. Pixel-static layer ------------------------------------------
    std::array<Route, kPixelsPerBar + 1> pixelRoute {};   // index 1..18 (0 unused)
    std::array<int,   kPixelsPerBar + 1> pixelBestVel {};
    for (auto& r : pixelRoute)   r = Route::None;
    for (auto& v : pixelBestVel) v = -1;
    bool pixelLayerHeld = false;

    state.forEachHeld ([&] (std::uint8_t pitch, const HeldNote& n)
    {
        if (! isPixelStaticPitch (pitch))
            return;
        pixelLayerHeld = true;
        const auto mask = kPixelStaticMask[pitch - kPixelStaticStart];
        const auto route = routeForVelocity (n.velocity);
        for (int p = 1; p <= kPixelsPerBar; ++p)
        {
            if (! (mask & (1u << (p - 1))))
                continue;
            if (static_cast<int> (n.velocity) > pixelBestVel[p])
            {
                pixelBestVel[p] = n.velocity;
                pixelRoute[p]   = route;
            }
        }
    });

    // ---- 5. Dynamic layer ------------------------------------------------
    // Brightness dynamics contribute brightness/motion only — they do NOT
    // pick a colour route. The colour comes from the bar/zone selectors
    // (whose velocity routes primary vs secondary) or the primary palette by
    // default, so a chase renders in whatever colour the structural layers
    // chose. For dynamics, velocity instead drives the trail length. Each
    // held note keeps its own tail, so simultaneous chases can trail
    // independently. Small fixed-size buffers; the audio thread never
    // allocates.
    //
    // Colour dynamics (the self-coloured Multicolor bank) are gathered
    // separately: they emit RGB directly and, when held, replace the palette
    // route for the lit pixels. Brightness dynamics still multiply on top, so
    // "rainbow + snake" is a rainbow-coloured snake. For colour dynamics the
    // note's VELOCITY controls SPEED (it scales the animation clock) rather
    // than a trail length.
    struct ActiveRecipe      { DynamicFn      fn; float tail; };
    struct ActiveColorRecipe { DynamicColorFn fn; float speed; };
    constexpr int kMaxRecipes = 16;
    std::array<ActiveRecipe,      kMaxRecipes> recipes {};
    std::array<ActiveColorRecipe, kMaxRecipes> colorRecipes {};
    int nRecipes = 0;
    int nColorRecipes = 0;
    bool dynamicLayerHeld = false;

    state.forEachHeld ([&] (std::uint8_t pitch, const HeldNote& n)
    {
        // Soft notes trail long, hard notes snap to a single-pixel head.
        const float tail = 1.0f - static_cast<float> (n.velocity) / 127.0f;

        if (isDynamicPitch (pitch))
        {
            auto fn = getDynamicRecipe (pitch);
            if (fn == nullptr)
                return;   // e.g. strobe (pitch 49) — applied as a driver-level shutter
            dynamicLayerHeld = true;
            if (nRecipes < kMaxRecipes) recipes[nRecipes++] = { fn, tail };
        }
        else if (isColorDynPitch (pitch))
        {
            auto fn = getColorRecipe (pitch);
            // Velocity → speed: ~0.2× (soft) up to 2.5× (hard), 1× near mid.
            const float speed = std::clamp (static_cast<float> (n.velocity) / 64.0f, 0.2f, 2.5f);
            if (fn != nullptr && nColorRecipes < kMaxRecipes)
                colorRecipes[nColorRecipes++] = { fn, speed };
        }
    });

    // ---- 6. Spot layer ---------------------------------------------------
    std::array<bool, kNumSpots> spotWW  { false, false };
    std::array<bool, kNumSpots> spotSec { false, false };

    state.forEachHeld ([&] (std::uint8_t pitch, const HeldNote&)
    {
        if (! isSpotPitch (pitch))
            return;
        const auto& m = kSpotMap[pitch - kSpotStart];
        if (m.isWarmWhite) spotWW [m.spotIdx] = true;
        else               spotSec[m.spotIdx] = true;
    });

    // ---- 7. Compose bars -------------------------------------------------
    const int nBars = kNumBars;
    for (int barIdx = 0; barIdx < nBars; ++barIdx)
    {
        const auto& bar = kBars[barIdx];

        const float barBrightness = barLayerHeld
                                  ? (barRoute[barIdx] != Route::None ? 1.0f : 0.0f)
                                  : 1.0f;
        const Route barR = barLayerHeld ? barRoute[barIdx] : Route::None;

        for (int pixel = 1; pixel <= bar.pixels; ++pixel)
        {
            // Pixel-density thinning: for dark rooms, run fewer LEDs without
            // dimming the ones that stay on. The per-bar rank gates each
            // pixel, so density<1 drops pixels in a fixed (non-flickering)
            // random order — evenly across the bars — while passing pixels
            // keep full-brightness flashes. density==1 lights everything.
            if (pixelDensity < 1.0f
                && kDensityRank[static_cast<size_t> (barIdx)][static_cast<size_t> (pixel)] >= pixelDensity)
                continue;   // out was cleared at entry, so this pixel stays black

            const float pixBrightness = pixelLayerHeld
                                      ? (pixelRoute[pixel] != Route::None ? 1.0f : 0.0f)
                                      : 1.0f;
            const Route pixR = pixelLayerHeld ? pixelRoute[pixel] : Route::None;

            float dynBrightness = 1.0f;
            if (dynamicLayerHeld)
            {
                float dyn = 0.0f;
                for (int i = 0; i < nRecipes; ++i)
                    dyn = std::max (dyn,
                                    recipes[i].fn (tBeats, barIdx, pixel,
                                                   bar.pixels, nBars, recipes[i].tail));
                dynBrightness = dyn;
            }

            const float brightness = barBrightness * pixBrightness * dynBrightness;
            const auto channels = bar.channelsFor (pixel);
            if (brightness <= 0.0f)
            {
                out.set (channels[0], 0.0f);
                out.set (channels[1], 0.0f);
                out.set (channels[2], 0.0f);
                continue;
            }

            float cr, cg, cb;
            if (nColorRecipes > 0)
            {
                // A self-coloured recipe is held: the recipe IS the colour,
                // overriding the palette route (and the colour-fade stage —
                // these animate their own colour continuously). Multiple
                // colour recipes combine per-channel max, matching the
                // additive feel of the brightness dynamics.
                cr = cg = cb = 0.0f;
                for (int i = 0; i < nColorRecipes; ++i)
                {
                    // Velocity scales the animation clock (speed).
                    const auto c = colorRecipes[i].fn (tBeats * colorRecipes[i].speed,
                                                       barIdx, pixel, bar.pixels, nBars);
                    cr = std::max (cr, c.r);
                    cg = std::max (cg, c.g);
                    cb = std::max (cb, c.b);
                }
            }
            else
            {
                // Most-specific route wins; fall back to primary. Brightness
                // dynamics don't route colour — they only modulate above.
                Route route = pixR != Route::None ? pixR
                            : barR != Route::None ? barR
                            : Route::Primary;

                const bool usePri = (route != Route::Secondary);
                cr = usePri ? priR : secR;
                cg = usePri ? priG : secG;
                cb = usePri ? priB : secB;
            }

            out.set (channels[0], cr * brightness * ledMasterDim);
            out.set (channels[1], cg * brightness * ledMasterDim);
            out.set (channels[2], cb * brightness * ledMasterDim);
        }
    }

    // ---- 8. Compose spots ------------------------------------------------
    for (int s = 0; s < kNumSpots; ++s)
    {
        const auto& spot = kSpots[s];
        float dim = 0.0f, r = 0.0f, g = 0.0f, b = 0.0f, w = 0.0f;
        if (spotWW[s])
        {
            dim = 1.0f;
            w   = 1.0f;
            // Tint W with R + a touch of G → incandescent warm white
            r = std::max (r, kSpotWarmR);
            g = std::max (g, kSpotWarmG);
        }
        if (spotSec[s])
        {
            dim = 1.0f;
            r = std::max (r, secR);
            g = std::max (g, secG);
            b = std::max (b, secB);
        }
        out.set (spot.dimmer(), dim * spotMasterDim);
        out.set (spot.red(),    r);
        out.set (spot.green(),  g);
        out.set (spot.blue(),   b);
        out.set (spot.white(),  w);
        out.set (spot.strobe(), 0.0f);
    }
}

}  // namespace hitnotedmx
