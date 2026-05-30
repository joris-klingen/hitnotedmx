#include "Composition.h"

#include <algorithm>
#include <array>

#include "Palette.h"
#include "Recipes.h"
#include "Rig.h"

namespace hitnotedmx
{

// MIDI vocabulary — these mirror the SPOT_NOTES / BAR_SELECTORS /
// PIXEL_STATICS dicts in midi_to_dmx.py exactly. We compile them down
// into per-pitch lookup tables for O(1) audio-thread access.

namespace
{
constexpr int kVelocityThreshold = 64;

// Warm-white tint for the singer spots, matching midi_to_dmx.py.
constexpr float kSpotWarmR = 0.4f;
constexpr float kSpotWarmG = 0.15f;

// ---- spot triggers (pitch 0..3) -----------------------------------------
struct SpotMapping
{
    int spotIdx;       // 0 or 1
    bool isWarmWhite;  // true = WW, false = secondary
};

constexpr int kNumSpotTriggers = 4;

// pitch → (spotIdx, isWW). pitch 0=L WW, 1=L sec, 2=R WW, 3=R sec.
constexpr std::array<SpotMapping, kNumSpotTriggers> kSpotMap {{
    { 0, true  },
    { 0, false },
    { 1, true  },
    { 1, false },
}};

inline constexpr bool isSpotPitch (int p) { return p >= 0 && p < kNumSpotTriggers; }


// ---- bar selectors (pitch 4..11) ----------------------------------------
// Set membership per pitch, encoded as a 4-bit mask (bit n = bar n active).

constexpr int kBarSelStart = 4;
constexpr int kBarSelEnd   = 11;
constexpr int kNumBarSelectors = kBarSelEnd - kBarSelStart + 1;

constexpr std::array<std::uint8_t, kNumBarSelectors> kBarSelectorMask {{
    0b1111,  //  4: all
    0b0001,  //  5: bar 1
    0b0010,  //  6: bar 2
    0b0100,  //  7: bar 3
    0b1000,  //  8: bar 4
    0b0011,  //  9: bars 1+2
    0b1100,  // 10: bars 3+4
    0b1001,  // 11: bars 1+4
}};

inline constexpr bool isBarSelPitch (int p) { return p >= kBarSelStart && p <= kBarSelEnd; }


// ---- pixel statics (pitch 12..23) ---------------------------------------
// 9-bit mask: bit n (0..8) = pixel (n+1) selected.

constexpr int kPixelStaticStart = 12;
constexpr int kPixelStaticEnd   = 23;
constexpr int kNumPixelStatics  = kPixelStaticEnd - kPixelStaticStart + 1;

constexpr std::uint16_t bit (int p)  // 1-based pixel → bit
{
    return static_cast<std::uint16_t> (1u << (p - 1));
}

constexpr std::array<std::uint16_t, kNumPixelStatics> kPixelStaticMask {{
    bit(1),                                                          // 12 {1}
    static_cast<std::uint16_t> (bit(2) | bit(3)),                    // 13 {2,3}
    bit(4),                                                          // 14 {4}
    static_cast<std::uint16_t> (bit(5) | bit(6)),                    // 15 {5,6}
    static_cast<std::uint16_t> (bit(7) | bit(8)),                    // 16 {7,8}
    bit(9),                                                          // 17 {9}
    static_cast<std::uint16_t> (bit(1) | bit(2) | bit(3)),           // 18 {1..3}
    static_cast<std::uint16_t> (bit(4) | bit(5) | bit(6)),           // 19 {4..6}
    static_cast<std::uint16_t> (bit(7) | bit(8) | bit(9)),           // 20 {7..9}
    static_cast<std::uint16_t> (bit(1) | bit(9)),                    // 21 {1,9}
    static_cast<std::uint16_t> (bit(1) | bit(3) | bit(5) | bit(7) | bit(9)),  // 22 odd
    static_cast<std::uint16_t> (bit(2) | bit(5) | bit(8)),           // 23 {2,5,8}
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

inline constexpr bool isPrimaryColorPitch (int p)
{
    return p >= kPrimaryPaletteStart && p < kSecondaryPaletteStart;
}

inline constexpr bool isSecondaryColorPitch (int p)
{
    return p >= kSecondaryPaletteStart && p < kBlackoutNote;
}


// ---- color picker --------------------------------------------------------
//
// SIMPLIFICATION: the Python translator crossfades the two most-recent
// overlapping color notes linearly across `min(a.end, b.end)`. In the
// live model we don't know future end times, so we just use the most-
// recently started note as the winner. If we ever want a fade-in
// behaviour we can use (currentBeat - mostRecent.startBeat) as the
// fade-progress against a configurable window. For v1, snap.
struct ColorPick { float r, g, b, intensity; };

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
        return { 0.0f, 0.0f, 0.0f, 0.0f };

    const auto& c = kPalette[mostRecentPitch - paletteStart];
    const float intensity = static_cast<float> (mostRecentVel) / 127.0f;
    return { c.r, c.g, c.b, intensity };
}

}  // namespace


void computeDmx (const MidiState& state, double tBeats, DmxValues& out) noexcept
{
    out.clear();

    // ---- 1. Blackout short-circuit --------------------------------------
    if (state.isActive (static_cast<std::uint8_t> (kBlackoutNote)))
        return;

    // ---- 2. Palettes -----------------------------------------------------
    const auto primaryC   = pickColor (state, kPrimaryPaletteStart,   kSecondaryPaletteStart);
    const auto secondaryC = pickColor (state, kSecondaryPaletteStart, kBlackoutNote);

    const float priR = primaryC.r   * primaryC.intensity;
    const float priG = primaryC.g   * primaryC.intensity;
    const float priB = primaryC.b   * primaryC.intensity;
    const float secR = secondaryC.r * secondaryC.intensity;
    const float secG = secondaryC.g * secondaryC.intensity;
    const float secB = secondaryC.b * secondaryC.intensity;

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
    std::array<Route, kPixelsPerBar + 1> pixelRoute {};   // index 1..9 (0 unused)
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

    // ---- 5. Dynamic layer: split into primary/secondary recipe lists ----
    // Small fixed-size buffers; the audio thread never allocates.
    constexpr int kMaxRecipesPerSide = 16;
    std::array<DynamicFn, kMaxRecipesPerSide> primaryRecipes {};
    std::array<DynamicFn, kMaxRecipesPerSide> secondaryRecipes {};
    int nPrimary = 0;
    int nSecondary = 0;
    bool dynamicLayerHeld = false;

    state.forEachHeld ([&] (std::uint8_t pitch, const HeldNote& n)
    {
        if (! isDynamicPitch (pitch))
            return;
        dynamicLayerHeld = true;
        auto fn = getDynamicRecipe (pitch);
        if (fn == nullptr)
            return;
        if (n.velocity >= kVelocityThreshold)
        {
            if (nPrimary < kMaxRecipesPerSide) primaryRecipes[nPrimary++] = fn;
        }
        else
        {
            if (nSecondary < kMaxRecipesPerSide) secondaryRecipes[nSecondary++] = fn;
        }
    });

    // ---- 6. Spot layer ---------------------------------------------------
    std::array<bool, kNumSpots> spotWW  { false, false };
    std::array<bool, kNumSpots> spotSec { false, false };

    state.forEachHeld ([&] (std::uint8_t pitch, const HeldNote&)
    {
        if (! isSpotPitch (pitch))
            return;
        const auto& m = kSpotMap[pitch];
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
            const float pixBrightness = pixelLayerHeld
                                      ? (pixelRoute[pixel] != Route::None ? 1.0f : 0.0f)
                                      : 1.0f;
            const Route pixR = pixelLayerHeld ? pixelRoute[pixel] : Route::None;

            float dynBrightness = 1.0f;
            Route dynR = Route::None;
            if (dynamicLayerHeld)
            {
                float dynPri = 0.0f;
                for (int i = 0; i < nPrimary; ++i)
                    dynPri = std::max (dynPri,
                                       primaryRecipes[i] (tBeats, barIdx, pixel,
                                                          bar.pixels, nBars));
                float dynSec = 0.0f;
                for (int i = 0; i < nSecondary; ++i)
                    dynSec = std::max (dynSec,
                                       secondaryRecipes[i] (tBeats, barIdx, pixel,
                                                            bar.pixels, nBars));
                if (dynPri >= dynSec) { dynBrightness = dynPri; dynR = Route::Primary;   }
                else                  { dynBrightness = dynSec; dynR = Route::Secondary; }
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

            // Most-specific route wins; fall back to primary.
            Route route = pixR != Route::None ? pixR
                        : dynR != Route::None ? dynR
                        : barR != Route::None ? barR
                        : Route::Primary;

            const bool usePri = (route != Route::Secondary);
            const float cr = usePri ? priR : secR;
            const float cg = usePri ? priG : secG;
            const float cb = usePri ? priB : secB;

            out.set (channels[0], cr * brightness);
            out.set (channels[1], cg * brightness);
            out.set (channels[2], cb * brightness);
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
        out.set (spot.dimmer(), dim);
        out.set (spot.red(),    r);
        out.set (spot.green(),  g);
        out.set (spot.blue(),   b);
        out.set (spot.white(),  w);
        out.set (spot.strobe(), 0.0f);
    }
}

}  // namespace hitnotedmx
