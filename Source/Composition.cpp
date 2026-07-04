#include "Composition.h"

#include <algorithm>
#include <array>
#include <cmath>

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
// which layer the note belongs to. All of these interpretations are applied
// here in computeDmx (or in the recipe it dispatches to):
//
//   Layer                    Velocity controls
//   ──────────────────────   ───────────────────────────────────────────────
//   Bar select               Per-bar BRIGHTNESS ceiling (velocity / 127) —
//                            soft bar = dim rig, hard bar = full
//   Pixel-zone select        Palette ROUTE: >= 64 → primary, < 64 → secondary
//                            (kVelocityThreshold; see routeForVelocity)
//   Chases (brightness)      TAIL length of the comet head — hard = long trail.
//                            UNDER global speed (G8): picks the palette ROUTE
//                            (>=64 primary, <64 secondary) instead.
//   Wild (brightness)        Beat-synced SPEED division (127 = 1/16 … 0 = 1/1);
//                            sparkle / sparkle_few stay free-running. UNDER G8:
//                            picks the palette ROUTE instead.
//   Breathes (brightness)    GLOW-MASK depth — 127 = the breathe untouched,
//                            softer notes dim it into a drifting patchy wash
//                            (breatheGlowMask); half the chase rate, so the
//                            periodic breathes cycle once per bar
//   Colour dynamics          SPEED of the recipe's animation — hard = faster;
//                            EXCEPT VU meter: beat-locked, velocity → gain
//   Speed (G8)               GLOBAL recipe-speed multiplier for all four banks
//                            — a DISCRETE power-of-two ladder, doubling every
//                            14 velocity: vel 1 = 0.5x (a 2-bar breathe),
//                            15 = 1x, then 2x / 4x / 8x cap at 57+; see
//                            section 5
//   Palette colour notes     Colour-FADE duration only — hard = instant snap,
//                            soft = slow rise to FULL colour (advanceFade).
//                            Brightness comes from the bar-selector layer.
//   Master controls          Bump velocity = flash brightness (zero sustain:
//                            fires on note ONSET, decays regardless of hold);
//                            Release velocity = bump release length (1/16 note
//                            .. 1 bar). Crossfade velocity = bar fade length.
//                            To-black / from-black velocity = their own fade
//                            rate (127 = instant, 0 = 1 bar); from-black at
//                            velocity 1 holds full black. Spread velocity =
//                            per-bar phase offset. Flip (spatial) / Reverse
//                            (temporal retrace) / Freeze: no vel.
//
// Spots, blackout and freeze ignore velocity. The C8 octave's remaining free
// notes (123–127) are pencilled in for density / soft-edge / speed note
// controls (see TODO) — velocity will set their intensity.

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

// ---- bar-level master fades (octave -2, beside the bar selectors) -------
// To/from-black are bar-level masks (spots are excluded — only blackout takes
// the spots dark), so they live in the Spots & bars octave next to the bar
// selectors where they're handy when designing, not up in the master octave.
constexpr int kFromBlackNote = 9;    // A-2  — snap to black on onset, then rise to the scene
constexpr int kToBlackNote   = 10;   // A#-2 — snap to the scene on onset, then fall to black

// ---- master / global hits (top of the keyboard, octave 8) ---------------
// Played from the top free notes as "master" controls, above the palette.
// Unlike every other trigger these are NOT per-fixture: they override or hold
// the whole composed frame. Mirrored in the vocabulary's "Master" column
// (TriggerVocabulary.cpp) — keep the note numbers in step (the mapping-frozen
// test guards the labels, not these behaviour constants).
constexpr int kBumpNote        = 120;  // C8  — momentary flash (white, or the primary hue if one is held)
constexpr int kBumpReleaseNote = 121;  // C#8 — velocity sets the bump release length (1/16 .. 1 bar)
constexpr int kCrossfadeNote   = 122;  // D8  — global crossfade of the bars; velocity = fade length
constexpr int kFreezeNote      = 123;  // D#8 — hold the current frame while held
constexpr int kReverseNote     = 124;  // E8  — real reverse: chases/breathes retrace (recipe phase runs backward)
constexpr int kFlipNote        = 125;  // F8  — flip recipe direction (mirrored sampling coords)
constexpr int kSpreadNote      = 126;  // F#8 — per-bar phase offset; velocity = amount
constexpr int kSpeedNote       = 127;  // G8  — velocity = global recipe-speed multiplier (vel 64 = 1x)


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
// Four POSITIONAL selectors — Left / Mid left / Mid right / Right — computed
// at runtime for any column count. The ENDS are exclusive: Left owns the
// outermost left bar(s), Right the outermost right, and NO other selector
// ever touches them. The end width grows with the grid — 1 bar up to 6
// columns, 2 bars from 7 (e = max(1, (cols+1)/4)). The MIDS split the bars
// between the ends half-and-half and may overlap EACH OTHER: with an odd
// number of middle bars both take the centre bar (3 cols → both mids are the
// centre bar; 5 cols → Mid left = bars 2-3, Mid right = 3-4, 1-based). At 4
// columns this is exactly one bar per selector (the original per-bar
// mapping). Degenerate shapes: 2 cols leaves no middle (mids select
// nothing); 1 col makes Left and Right the same single bar (disjointness is
// impossible). "All bars" stays the default when no bar note is held.

constexpr int kBarSelStart = 5;
constexpr int kBarSelEnd   = 8;

inline constexpr bool isBarSelPitch (int p) { return p >= kBarSelStart && p <= kBarSelEnd; }

// Does selector sel (0..3, low pitch = left) cover 0-based bar b of a
// cols-wide grid?
inline constexpr bool selectorCoversBar (int sel, int b, int cols)
{
    const int e = ((cols + 1) / 4) < 1 ? 1 : (cols + 1) / 4;   // bars per end
    if (sel == 0) return b < e;                    // Left
    if (sel == 3) return b >= cols - e;            // Right
    const int m = cols - 2 * e;                    // middle bars between the ends
    if (m <= 0) return false;                      // no middle → mids empty
    const int k = (m + 1) / 2;                     // per-mid share (mids overlap iff m odd)
    return sel == 1 ? (b >= e && b < e + k)                    // Mid left
                    : (b >= cols - e - k && b < cols - e);     // Mid right
}

// Refactor guards, spelling out the contract above.
static_assert (selectorCoversBar (0, 0, 4) && ! selectorCoversBar (0, 1, 4)
            && selectorCoversBar (1, 1, 4) && ! selectorCoversBar (1, 2, 4)
            && selectorCoversBar (2, 2, 4) && ! selectorCoversBar (2, 1, 4)
            && selectorCoversBar (3, 3, 4) && ! selectorCoversBar (3, 2, 4),
               "4-col mapping must stay one bar per selector");
static_assert (selectorCoversBar (1, 1, 3) && selectorCoversBar (2, 1, 3)
            && ! selectorCoversBar (1, 0, 3) && ! selectorCoversBar (2, 2, 3),
               "3 cols: mids share the centre bar, ends stay exclusive");
static_assert (selectorCoversBar (1, 2, 5) && selectorCoversBar (2, 2, 5)     // both mids: centre bar
            && selectorCoversBar (1, 1, 5) && selectorCoversBar (2, 3, 5)
            && ! selectorCoversBar (1, 0, 5) && ! selectorCoversBar (2, 4, 5),
               "5 cols: mids overlap on the centre, never on the ends");
static_assert (! selectorCoversBar (0, 1, 6) && ! selectorCoversBar (3, 4, 6),
               "6 cols: ends still one bar each");
static_assert (selectorCoversBar (0, 1, 7) && selectorCoversBar (3, 5, 7)
            && ! selectorCoversBar (1, 1, 7) && ! selectorCoversBar (2, 5, 7),
               "7 cols: ends widen to two bars, still exclusive");


// ---- pixel statics (pitch 12..23, the C-1 octave) -----------------------
// Per-note pixel mask: bit n = pixel (n+1) selected, 1 = bottom. rows ≤
// kMaxRows = 32 keeps each mask a single uint32.
//
// The nine zones are contiguous bottom-up row spans — pixel p belongs to
// zone ((p-1)*9)/rows, which at rows = 18 reproduces the original 2-pixel
// pairs (1-2, 3-4, … 17-18) exactly. Then the Even / Odd / Thirds combs,
// modular on the pixel index so they're row-count-agnostic. The masks for
// the LIVE row count are built in GridState::rebuild().

constexpr int kPixelStaticStart = 12;
constexpr int kPixelStaticEnd   = 23;
static_assert (kPixelStaticEnd - kPixelStaticStart + 1 == GridState::kNumPixelMasks,
               "pixel-static note range and mask table out of step");

constexpr std::uint32_t bit (int p)  // 1-based pixel → bit
{
    return static_cast<std::uint32_t> (1u << (p - 1));
}

constexpr std::uint32_t everyN (int first, int step, int rows)  // first..rows step → comb mask
{
    std::uint32_t m = 0;
    for (int p = first; p <= rows; p += step)
        m |= bit (p);
    return m;
}

// Mask for pixel-static slot `idx` (0..11) at a given row count.
constexpr std::uint32_t pixelMaskFor (int idx, int rows)
{
    if (idx < 9)          // zones 1-9: contiguous bottom-up spans
    {
        std::uint32_t m = 0;
        for (int p = 1; p <= rows; ++p)
            if (((p - 1) * 9) / rows == idx)
                m |= bit (p);
        return m;
    }
    if (idx == 9)  return everyN (2, 2, rows);   // even pixels
    if (idx == 10) return everyN (1, 2, rows);   // odd pixels
    return everyN (1, 3, rows);                  // every 3rd
}

// Behaviour-preserving refactor guard: at the original 18 rows each zone must
// remain its adjacent-pixel pair (old table was zone(z) = bit(2z-1) | bit(2z)).
static_assert (pixelMaskFor (0, 18) == (bit (1)  | bit (2)),  "zone 1 mask changed");
static_assert (pixelMaskFor (8, 18) == (bit (17) | bit (18)), "zone 9 mask changed");
static_assert (pixelMaskFor (9, 18) == everyN (2, 2, 18),     "even mask changed");

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
struct ColorPick { float r, g, b; int pitch; std::uint8_t velocity; };

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
        return { 0.0f, 0.0f, 0.0f, -1, 0 };

    const auto c = paletteColorFor (paletteStart, mostRecentPitch - paletteStart);
    return { c.r, c.g, c.b, mostRecentPitch, mostRecentVel };
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

constexpr float kTwoPi = 6.28318530717958647692f;

inline float hashf (std::uint32_t h) noexcept   // avalanche → [0,1)
{
    h ^= h >> 16;  h *= 0x7FEB352Du;
    h ^= h >> 15;  h *= 0x846CA68Bu;
    h ^= h >> 16;
    return static_cast<float> (h) / 4294967296.0f;
}

// Velocity → beat-synced clock multiplier for the Wild bank: 127 = 1/16,
// 0 = 1/1. Powers of two keep the recipes' frame boundaries on the beat grid.
inline float wildBeatSpeed (std::uint8_t vel) noexcept
{
    if (vel <  26) return 0.25f;   // 1/1  (whole note)
    if (vel <  52) return 0.5f;    // 1/2
    if (vel <  77) return 1.0f;    // 1/4
    if (vel < 103) return 2.0f;    // 1/8
    return 4.0f;                   // 1/16
}

// Smooth intensity mask for the Breathes bank (driven by the note's
// velocity). Velocity 127 leaves the breathe untouched; lower velocities
// blend in a glow-style field — two very slow incommensurate per-pixel sines
// plus a drifting spatial gradient, the same recipe as `glow` (pitch 47) —
// as a MULTIPLIER on top of the breathe. So a soft note dims the shape into
// a gently drifting patchy wash: a smooth intensity reduction, never an
// on/off gate, and never fully dark (the field bottoms out at ~0.22).
inline float breatheGlowMask (int barIdx, int pixel, int nPix, double t,
                              float depth) noexcept
{
    if (depth <= 0.001f)
        return 1.0f;   // velocity 127 → the breathe passes through untouched
    const float ph   = hashf (static_cast<std::uint32_t> (barIdx * 64 + pixel));
    const float y    = (static_cast<float> (pixel) - 0.5f) / static_cast<float> (nPix);
    const float tw   = 0.26f * std::sin (kTwoPi * (0.12f * static_cast<float> (t) + ph))
                     + 0.15f * std::sin (kTwoPi * (0.07f * static_cast<float> (t) + ph * 1.7f));
    const float grad = 0.13f * std::sin (kTwoPi * (0.05f * static_cast<float> (t) - y * 0.8f
                                                   + static_cast<float> (barIdx) * 0.2f));
    const float field = std::clamp (0.55f + tw + grad, 0.22f, 1.0f);
    return 1.0f + depth * (field - 1.0f);   // lerp: 1 (vel 127) → the glow field (vel 1)
}

}  // namespace


// Rebuild the derived per-grid tables for the current shape. Called on the
// audio thread when the editor's grid request changes: noexcept, no
// allocation, bounded by kMaxBars × kMaxRows² int ops — cheaper than a single
// computeDmx block.
//
// densityRank: a stable per-pixel gate value in [0, 1) for the pixel-density
// control. Deterministic from the pixel's grid position, so the SET of pixels
// passing a given density threshold is fixed frame-to-frame (no flicker) and
// shrinks monotonically as density falls. A full-avalanche hash (murmur3
// finalizer) of each cell's position, rank-normalised WITHIN its bar: a
// single-multiply linear hash dropped pixels in visible diagonal stripes; the
// avalanche hash scatters randomly, and the per-bar rank guarantees every bar
// keeps the same fraction lit while the drop order stays random. (The b*64+p
// hash input is collision-free while kMaxRows < 64.)
void GridState::rebuild() noexcept
{
    for (int i = 0; i < kNumPixelMasks; ++i)
        pixelMask[static_cast<size_t> (i)] = pixelMaskFor (i, rig.rows);

    auto avalanche = [] (std::uint32_t h)
    {
        h ^= h >> 16;  h *= 0x7FEB352Du;
        h ^= h >> 15;  h *= 0x846CA68Bu;
        h ^= h >> 16;
        return h;
    };

    for (int b = 0; b < rig.cols; ++b)
    {
        std::array<std::uint32_t, kMaxRows + 1> h {};
        for (int p = 1; p <= rig.rows; ++p)
            h[static_cast<size_t> (p)] = avalanche (static_cast<std::uint32_t> (b * 64 + p));
        for (int p = 1; p <= rig.rows; ++p)
        {
            int below = 0;
            for (int q = 1; q <= rig.rows; ++q)
                if (h[static_cast<size_t> (q)] < h[static_cast<size_t> (p)])
                    ++below;
            // +0.5 centres the rank in its bucket: density 1.0 passes every
            // pixel, density 0.0 passes none.
            densityRank[static_cast<size_t> (b)][static_cast<size_t> (p)] =
                (static_cast<float> (below) + 0.5f) / static_cast<float> (rig.rows);
        }
    }
}

void computeDmx (const MidiState& state, double tBeats, DmxValues& out,
                 const GridState& grid,
                 float ledMasterDim, float spotMasterDim,
                 ColorFadeState* fade, double dtSeconds, float pixelDensity,
                 SelectionMask* sel, BumpState* bump) noexcept
{
    // ---- 1. Blackout / freeze short-circuits ----------------------------
    // Blackout (pitch 0) is a hard kill and dominates everything, freeze
    // included.
    if (state.isActive (static_cast<std::uint8_t> (kBlackoutNote)))
    {
        out.clear();
        if (sel != nullptr)
            sel->clear();
        // Collapse any in-flight fade / bump tail so releasing blackout resumes
        // from black rather than mid-effect.
        if (fade != nullptr)
            fade->reset();
        if (bump != nullptr)
            bump->reset();
        return;
    }

    // Freeze (123) pauses the animation clock and holds the PREVIOUS frame: we
    // advance `animBeats` only on non-frozen blocks, rewrite `tBeats` to it, and
    // return early while held — so the recipes (and bump tails, all beat-driven)
    // continue from exactly where they froze rather than jumping ahead.
    const bool frozen = state.isActive (static_cast<std::uint8_t> (kFreezeNote));
    if (bump != nullptr)
    {
        if (! bump->haveRaw)   bump->animBeats = tBeats;
        else if (! frozen)     bump->animBeats += std::max (0.0, tBeats - bump->lastRaw);
        bump->lastRaw = tBeats;
        bump->haveRaw = true;
        tBeats = bump->animBeats;   // everything downstream runs on the paused clock
    }
    if (frozen)
        return;   // hold the last composed frame (and selection) untouched

    out.clear();
    if (sel != nullptr)
        sel->clear();

    // ---- 2. Palettes -----------------------------------------------------
    const auto primaryC   = pickColor (state, kPrimaryPaletteStart,   kSecondaryPaletteStart);
    const auto secondaryC = pickColor (state, kSecondaryPaletteStart, kSecondaryPaletteEnd);

    // Target colours at FULL intensity. A palette note's velocity drives only
    // the fade duration (advanceFade), not brightness — so a soft note rises
    // slowly to full colour. The brightness ceiling is the bar-selector layer.
    const float tPriR = primaryC.r;
    const float tPriG = primaryC.g;
    const float tPriB = primaryC.b;
    const float tSecR = secondaryC.r;
    const float tSecG = secondaryC.g;
    const float tSecB = secondaryC.b;

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
    // For each bar, the highest-velocity selector touching it sets the bar's
    // BRIGHTNESS ceiling (velocity / 127). Bars no longer pick a colour route
    // — that is the pixel-zone layer's job.
    std::array<int, kMaxBars> barBestVel;
    barBestVel.fill (-1);
    bool barLayerHeld = false;

    state.forEachHeld ([&] (std::uint8_t pitch, const HeldNote& n)
    {
        if (! isBarSelPitch (pitch))
            return;
        barLayerHeld = true;
        const int sel = pitch - kBarSelStart;   // 0 = left … 3 = right
        for (int b = 0; b < grid.rig.cols; ++b)
        {
            if (! selectorCoversBar (sel, b, grid.rig.cols))
                continue;
            if (static_cast<int> (n.velocity) > barBestVel[static_cast<size_t> (b)])
                barBestVel[static_cast<size_t> (b)] = n.velocity;
        }
    });

    // ---- 4. Pixel-static layer ------------------------------------------
    std::array<Route, kMaxRows + 1> pixelRoute {};   // index 1..rows (0 unused)
    std::array<int,   kMaxRows + 1> pixelBestVel {};
    for (auto& r : pixelRoute)   r = Route::None;
    for (auto& v : pixelBestVel) v = -1;
    bool pixelLayerHeld = false;

    state.forEachHeld ([&] (std::uint8_t pitch, const HeldNote& n)
    {
        if (! isPixelStaticPitch (pitch))
            return;
        pixelLayerHeld = true;
        const auto mask = grid.pixelMask[static_cast<size_t> (pitch - kPixelStaticStart)];
        const auto route = routeForVelocity (n.velocity);
        for (int p = 1; p <= grid.rig.rows; ++p)
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
    // Brightness dynamics contribute brightness/motion only — they do NOT pick
    // a colour route; the colour comes from the bar/zone selectors or the
    // primary palette. What the note's VELOCITY means depends on the bank:
    //   • Chases   → trail length (tail; hard = long comet)
    //   • Wild     → beat-synced speed (127 = 1/16 … 0 = 1/1) — except sparkle /
    //                sparkle_few, which stay free (continuous velocity speed)
    //   • Breathes → glow-mask depth (127 = untouched; softer notes dim the
    //                shape into a drifting wash); half the chase rate, so the
    //                periodic breathes cycle once per bar
    // `tScale` multiplies tBeats before the call; `arg` is the recipe's tail
    // (chases) or the breathe glow-mask depth.
    //
    // Colour dynamics (Multicolor) emit RGB directly and replace the palette
    // route for the lit pixels (brightness dynamics still multiply on top).
    // Velocity → SPEED, except VU meter, which is beat-LOCKED with velocity →
    // gain. Small fixed-size buffers; the audio thread never allocates.
    enum DynMode { kTail = 0, kSpeed = 1, kGlow = 2 };
    struct ActiveRecipe      { DynamicFn      fn; int mode; float tScale; float arg; };
    struct ActiveColorRecipe { DynamicColorFn fn; float tScale; float param; };
    constexpr int kMaxRecipes = 16;
    std::array<ActiveRecipe,      kMaxRecipes> recipes {};
    std::array<ActiveColorRecipe, kMaxRecipes> colorRecipes {};
    int nRecipes = 0;
    int nColorRecipes = 0;
    bool dynamicLayerHeld = false;
    bool strobeHeld = false;   // drives the rig white so the driver shutter has something to chop

    // Global speed (G8 / kSpeedNote): its velocity scales EVERY recipe's rate
    // as a DISCRETE power-of-two ladder — each 14-velocity step doubles the
    // rate, from a half-speed bottom step to an 8x cap: vel 1-14 = 0.5x,
    // 15-28 = 1x, 29-42 = 2x, 43-56 = 4x, 57+ = 8x. Absent = 1x (default
    // speeds). Anchored on the breathes' natural 1-bar cycle: vel 1 = 2 bars
    // (halo up one bar, down the next), 15 = the default 1 bar, 29 = 2 beats,
    // 43 = 1 beat. Powers of two keep every bank's cycles on the bar grid
    // (the phase clocks re-lock to the song position at transport start —
    // see BumpState::resyncClocks); the 8x cap is where the natively-faster
    // chases (1 cycle/beat) stop reading as motion and start aliasing. While
    // it's held, chase/wild velocity is freed from tail/beat-speed duty and
    // instead picks the palette ROUTE (>=64 primary, <64 secondary); the
    // strongest (highest-velocity) chase/wild wins. dynRoute feeds the colour
    // resolution in the bar compose below.
    const bool  speedHeld = state.isActive (static_cast<std::uint8_t> (kSpeedNote));
    const float gMult = [&]() -> float
    {
        if (! speedHeld)
            return 1.0f;
        const int vel   = state.get (static_cast<std::uint8_t> (kSpeedNote)).velocity;
        const int level = std::clamp ((vel - 1) / 14, 0, 4);
        return 0.5f * static_cast<float> (1 << level);   // 0.5x / 1x / 2x / 4x / 8x
    }();

    // Flip (F8): flip recipe DIRECTION by sampling each recipe at mirrored grid
    // coords (bar → nBars-1-bar, pixel → nPix+1-pixel). An instant SPATIAL
    // mirror — the head jumps to the mirror cell and continues. Recipes default
    // to "up"; this turns most movers around with no per-recipe code (symmetric
    // ones no-op; rotational/ping-pong won't mirror cleanly — acceptable).
    const bool flipHeld = state.isActive (static_cast<std::uint8_t> (kFlipNote));

    // Spread (F#8): a per-bar phase offset (in beats) so the four bars de-sync
    // — spices up tight/sine looks. Velocity = amount, 0 .. kMaxSpreadBeats.
    constexpr float kMaxSpreadBeats = 1.0f;
    const bool  spreadHeld  = state.isActive (static_cast<std::uint8_t> (kSpreadNote));
    const float spreadBeats = spreadHeld
        ? static_cast<float> (state.get (static_cast<std::uint8_t> (kSpreadNote)).velocity) / 127.0f * kMaxSpreadBeats
        : 0.0f;

    // Reverse (D#8): a *temporal* reverse — the continuous movers (chases +
    // breathes) run on an accumulated phase that runs BACKWARD while held, so
    // they retrace from the current state (vs Flip's instant mirror). The
    // beat-anchored banks (Wild, Multicolor) keep absolute time so their
    // beat-lock holds. The phase is kept non-negative (recipes wrap with fmod /
    // truncated %, which misbehave on negative t); the clamp at 0 is effectively
    // never reached mid-song. Needs `bump` for the accumulator — without it (the
    // render tools) it falls back to absolute forward time.
    const bool   realReverseHeld = state.isActive (static_cast<std::uint8_t> (kReverseNote));
    const double recipePhase = [&]
    {
        if (bump == nullptr)
            return tBeats * static_cast<double> (gMult);
        const double d = bump->havePhase ? (tBeats - bump->lastPhaseBeats) : 0.0;
        bump->lastPhaseBeats = tBeats;
        if (! bump->havePhase) { bump->recipePhase = tBeats * static_cast<double> (gMult); bump->havePhase = true; }
        else bump->recipePhase = std::max (0.0,
                 bump->recipePhase + (realReverseHeld ? -d : d) * static_cast<double> (gMult));
        return bump->recipePhase;
    }();

    Route dynRoute    = Route::None;
    int   dynRouteVel = -1;

    state.forEachHeld ([&] (std::uint8_t pitch, const HeldNote& n)
    {
        const float vel = static_cast<float> (n.velocity);

        if (isDynamicPitch (pitch))
        {
            auto fn = getDynamicRecipe (pitch);
            if (fn == nullptr)
            {
                // Strobe (pitch 48): no per-pixel recipe — the flash is a
                // driver-level shutter. Here we just light the rig white (via
                // the white-default path below) so the shutter chops white
                // when strobe is held alone; a colour/recipe held alongside
                // shows through instead.
                if (pitch == kStrobePitch)
                    strobeHeld = true;
                return;
            }
            dynamicLayerHeld = true;
            if (nRecipes >= kMaxRecipes)
                return;
            // Under global speed, chase/wild velocity picks the colour route
            // (not tail/beat-speed); track the strongest one.
            const bool routeByVel = speedHeld && (isWildPitch (pitch) || isChasesPitch (pitch));
            if (routeByVel && static_cast<int> (n.velocity) > dynRouteVel)
            {
                dynRouteVel = n.velocity;
                dynRoute    = routeForVelocity (n.velocity);
            }

            if (isWildPitch (pitch))
            {
                // Beat-speed from velocity normally; under global speed the
                // velocity is the colour route, so run at a neutral 1x (the
                // global multiplier sets the actual rate).
                float tScale = 1.0f;
                if (! speedHeld)
                {
                    const bool freeRunning = (pitch == kSparklePitch || pitch == kSparkleFewPitch);
                    tScale = freeRunning ? std::clamp (vel / 64.0f, 0.2f, 2.5f)
                                         : wildBeatSpeed (n.velocity);
                }
                recipes[nRecipes++] = { fn, kSpeed, tScale, 0.0f };
            }
            else if (isBreathesPitch (pitch))
            {
                // Half the chase rate — the periodic breathes cycle once per
                // bar. Velocity = the glow-mask blend depth (127 untouched).
                recipes[nRecipes++] = { fn, kGlow, 0.5f, 1.0f - vel / 127.0f };
            }
            else   // chases — velocity is tail length, or (under global speed) the colour route
                recipes[nRecipes++] = { fn, kTail, 1.0f, speedHeld ? 0.5f : vel / 127.0f };
        }
        else if (isColorDynPitch (pitch))
        {
            auto fn = getColorRecipe (pitch);
            if (fn == nullptr || nColorRecipes >= kMaxRecipes)
                return;
            if (pitch == kVuMeterPitch)        // beat-locked timing, velocity → gain
                colorRecipes[nColorRecipes++] = { fn, 1.0f, vel / 127.0f };
            else if (pitch == kDiscoPitch)     // beat-locked so it pulses on the song beat
                colorRecipes[nColorRecipes++] = { fn, 1.0f, 0.0f };
            else                               // velocity → speed (~0.2×..2.5×)
                colorRecipes[nColorRecipes++] = { fn, std::clamp (vel / 64.0f, 0.2f, 2.5f), 0.0f };
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

    // ---- 6b. White default ----------------------------------------------
    // A held DYNAMIC recipe or strobe with no palette colour renders full
    // white — it's content that needs a colour to show through. Bar / zone
    // SELECTORS no longer default to white: held alone they stay black on the
    // rig, and the visualiser shows a grey selection outline instead (see the
    // `selectorOnly` mask below). The secondary accent still whites-out for
    // the spot 'col' triggers. Seed the fade state so a colour played next
    // fades in from white rather than black.
    const bool contentHeld = dynamicLayerHeld || strobeHeld;
    if (contentHeld && primaryC.pitch < 0)
    {
        priR = priG = priB = 1.0f;
        if (fade != nullptr) { fade->primary.cur[0] = fade->primary.cur[1] = fade->primary.cur[2] = 1.0f; }
    }
    if ((contentHeld || spotSec[0] || spotSec[1]) && secondaryC.pitch < 0)
    {
        secR = secG = secB = 1.0f;
        if (fade != nullptr) { fade->secondary.cur[0] = fade->secondary.cur[1] = fade->secondary.cur[2] = 1.0f; }
    }

    // "Armed but unlit": a bar/zone selector is held but nothing lights the rig
    // (no palette colour, no dynamic/strobe). The selected cells stay black on
    // DMX; we flag them below so the visualiser can outline them in grey.
    const bool selectorOnly = (barLayerHeld || pixelLayerHeld)
                            && primaryC.pitch < 0 && secondaryC.pitch < 0
                            && ! contentHeld;

    // ---- 7. Compose bars -------------------------------------------------
    // Global-speed-scaled beat clock for the recipes (gMult = 1 when G8 absent).
    const double recipeBeats = tBeats * static_cast<double> (gMult);
    const int nBars = grid.rig.cols;
    const int nPix  = grid.rig.rows;
    for (int barIdx = 0; barIdx < nBars; ++barIdx)
    {
        // Spread (F#8): offset each bar's recipe clock so the bars de-sync.
        // `recipeBeatsBar` is absolute beat time (Wild / Multicolor);
        // `recipePhaseBar` is the reversible phase clock (Chases / Breathes).
        const double recipeBeatsBar = recipeBeats + static_cast<double> (spreadBeats) * barIdx;
        const double recipePhaseBar = recipePhase + static_cast<double> (spreadBeats) * barIdx;

        // Bar-selector velocity sets the per-bar brightness ceiling; an unheld
        // bar (when the layer is active) stays dark, no layer held = full.
        const float barBrightness = barLayerHeld
                                  ? (barBestVel[barIdx] >= 0
                                        ? static_cast<float> (barBestVel[barIdx]) / 127.0f
                                        : 0.0f)
                                  : 1.0f;

        for (int pixel = 1; pixel <= nPix; ++pixel)
        {
            // Pixel-density thinning: for dark rooms, run fewer LEDs without
            // dimming the ones that stay on. The per-bar rank gates each
            // pixel, so density<1 drops pixels in a fixed (non-flickering)
            // random order — evenly across the bars — while passing pixels
            // keep full-brightness flashes. density==1 lights everything.
            if (pixelDensity < 1.0f
                && grid.densityRank[static_cast<size_t> (barIdx)][static_cast<size_t> (pixel)] >= pixelDensity)
                continue;   // out was cleared at entry, so this pixel stays black

            const float pixBrightness = pixelLayerHeld
                                      ? (pixelRoute[pixel] != Route::None ? 1.0f : 0.0f)
                                      : 1.0f;
            const Route pixR = pixelLayerHeld ? pixelRoute[pixel] : Route::None;

            // Flip (F8): sample recipes at mirrored grid coords to flip
            // direction. The OUTPUT pixel stays real — only the sampling moves.
            const int rBar = flipHeld ? (nBars - 1 - barIdx) : barIdx;
            const int rPix = flipHeld ? (nPix + 1 - pixel)   : pixel;

            float dynBrightness = 1.0f;
            if (dynamicLayerHeld)
            {
                float dyn = 0.0f;
                for (int i = 0; i < nRecipes; ++i)
                {
                    const auto& r = recipes[i];
                    // Chases (kTail) + Breathes (kDensity) ride the reversible
                    // phase clock; Wild (kSpeed) stays on absolute beat time.
                    const double base = (r.mode == kSpeed) ? recipeBeatsBar : recipePhaseBar;
                    const double rt = base * static_cast<double> (r.tScale);
                    float v;
                    if (r.mode == kGlow)         // Breathes: soft velocity dimming
                        v = r.fn (rt, rBar, rPix, nPix, nBars, 0.0f)
                          * breatheGlowMask (barIdx, pixel, nPix, rt, r.arg);
                    else                          // Chases (tail=arg) / Wild (arg unused)
                        v = r.fn (rt, rBar, rPix, nPix, nBars, r.arg);
                    dyn = std::max (dyn, v);
                }
                dynBrightness = dyn;
            }

            const float brightness = barBrightness * pixBrightness * dynBrightness;
            const auto channels = grid.rig.channelsFor (barIdx, pixel);
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
                    const auto& r = colorRecipes[i];   // tScale = speed (1 for VU, beat-locked)
                    const auto c = r.fn (recipeBeatsBar * static_cast<double> (r.tScale),
                                         rBar, rPix, nPix, nBars, r.param);
                    cr = std::max (cr, c.r);
                    cg = std::max (cg, c.g);
                    cb = std::max (cb, c.b);
                }
            }
            else
            {
                // Zone route wins; then a chase/wild's own route under global
                // speed (dynRoute); otherwise primary. Bars don't route colour.
                Route route = pixR     != Route::None ? pixR
                            : dynRoute != Route::None ? dynRoute
                            : Route::Primary;

                const bool usePri = (route != Route::Secondary);
                cr = usePri ? priR : secR;
                cg = usePri ? priG : secG;
                cb = usePri ? priB : secB;
            }

            // Armed but unlit: in the selector-only state this in-region cell
            // resolves to black — flag it for the visualiser's grey outline.
            if (sel != nullptr && selectorOnly && cr <= 0.0f && cg <= 0.0f && cb <= 0.0f)
                sel->cell[static_cast<size_t> (barIdx)][static_cast<size_t> (pixel)] = true;

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

    // ---- 9. Master controls (crossfade + flash bump + to/from-black) ------
    // Whole-rig overrides. State lives in `bump`, advanced in beat-time so the
    // timing is tempo-relative.
    //   • crossfade (D8) — slew the BAR frame toward the composed scene so look
    //     changes glide; velocity = fade length (1/16 note .. 1 bar). Bars only.
    //   • bump (C8) — flash toward white, or the primary hue if one is held
    //     (velocity = brightness). ZERO SUSTAIN: fires on each note ONSET and
    //     decays back to the *scene* regardless of hold, so only the note start
    //     matters. Its release length is the Release note (C#8): 1/16 note at
    //     vel 1 .. 1 bar at vel 127, default 1/8 note when absent.
    //   • to/from-black (A#-2 / A-2) — to-black snaps to the scene on ONSET then
    //     falls to black; from-black snaps to black then rises, EXCEPT at
    //     velocity 1 where it holds full black (lay black down, then reveal with
    //     a higher-velocity note). BOTH reset to the scene when the note ENDS,
    //     per-note. They glide at their OWN note's velocity (127 = instant, 0 =
    //     one bar). Spots excluded — only blackout (C-2) darkens them.
    if (bump != nullptr)
    {
        const double dBeats = bump->haveLast ? std::max (0.0, tBeats - bump->lastBeats) : 0.0;
        bump->lastBeats = tBeats;
        bump->haveLast  = true;

        // Crossfade (D8): slew the displayed BAR frame toward the freshly
        // composed scene so look changes glide instead of snapping. Velocity →
        // fade length (1/16 note at vel 1 → 1 bar at vel 127); absent → instant
        // (the buffer just tracks, so enabling it starts from the current
        // frame). Bars only — spots stay snappy and the bump/black overlays
        // below punch through on top. At long lengths it also motion-blurs the
        // recipes (a smear), by design.
        const bool  xfadeHeld = state.isActive (static_cast<std::uint8_t> (kCrossfadeNote));
        float xfadeRate = 1.0f;
        if (xfadeHeld)
        {
            const float v = static_cast<float> (state.get (static_cast<std::uint8_t> (kCrossfadeNote)).velocity);
            const float beats = 0.25f * std::pow (16.0f, (v - 1.0f) / 126.0f);   // 1/16 .. 1 bar
            xfadeRate = (beats <= 0.0f) ? 1.0f : std::min (1.0f, static_cast<float> (dBeats) / beats);
        }
        for (int barIdx = 0; barIdx < grid.rig.cols; ++barIdx)
        {
            for (int pixel = 1; pixel <= grid.rig.rows; ++pixel)
            {
                const auto ch = grid.rig.channelsFor (barIdx, pixel);
                for (int c = 0; c < 3; ++c)
                {
                    // kMaxRows stride, so an index means the same cell at any
                    // grid shape (the buffer is max-grid sized).
                    const std::size_t idx = (static_cast<std::size_t> (barIdx) * kMaxRows
                                             + static_cast<std::size_t> (pixel - 1)) * 3
                                          + static_cast<std::size_t> (c);
                    const float target = out.get (ch[static_cast<size_t> (c)]);
                    float& disp = bump->xfade[idx];
                    disp = (! bump->xfadeHave || ! xfadeHeld) ? target              // track / init
                                                              : disp + (target - disp) * xfadeRate;
                    out.set (ch[static_cast<size_t> (c)], disp);
                }
            }
        }
        bump->xfadeHave = true;

        // Velocity → per-block glide rate (vel 127 = instant, 0 = one bar).
        auto ratePerBlock = [dBeats] (float vel) -> float
        {
            const float beats = (1.0f - vel / 127.0f) * 4.0f;
            return (beats <= 0.0f) ? 1.0f : static_cast<float> (dBeats) / beats;
        };

        // Bump release length: the Release note (C#8) scales it from 1/16 note
        // (vel 1) to 1 bar (vel 127), exponential; absent → a default 1/8 note.
        // The per-block decay rate is this block's beat delta over that length.
        const bool  bumpRelHeld = state.isActive (static_cast<std::uint8_t> (kBumpReleaseNote));
        const float bumpRelBeats = bumpRelHeld
            ? 0.25f * std::pow (16.0f,
                (static_cast<float> (state.get (static_cast<std::uint8_t> (kBumpReleaseNote)).velocity) - 1.0f) / 126.0f)
            : 0.5f;   // default 1/8 note
        const float bumpRate = (bumpRelBeats <= 0.0f) ? 1.0f : static_cast<float> (dBeats) / bumpRelBeats;

        // Zero-sustain flash: the bump fires on the note's ONSET (keyed off the
        // held note's start beat, so back-to-back clip notes still re-trigger)
        // and then ALWAYS decays — even while the note is still held — so the
        // hold length is irrelevant and you only program the note start. The
        // decay rate is the Release length (bumpRate).
        auto advance = [&] (BumpState::Env& e, int note)
        {
            bool onset = false;
            if (state.isActive (static_cast<std::uint8_t> (note)))
            {
                const auto& n = state.get (static_cast<std::uint8_t> (note));
                if (n.startBeat != e.lastStart)   // new onset → instant flash
                {
                    e.level     = 1.0f;           // instant attack
                    e.amount    = static_cast<float> (n.velocity) / 127.0f;
                    e.lastStart = n.startBeat;
                    onset = true;
                }
            }
            else
            {
                e.lastStart = -1.0;   // released → next note-on is a fresh onset
            }
            // Decay from the onset regardless of hold; skip the onset block so
            // the peak flash renders at full before it starts falling.
            if (! onset && e.level > 0.0f)
                e.level = std::max (0.0f, e.level - bumpRate);
        };
        advance (bump->flash, kBumpNote);

        // To/from-black master fader (output scaled by 1 - blackLevel below).
        // These glide at the rate set by THEIR OWN note's velocity (the bumps'
        // Release note doesn't apply here):
        //   • To black — fade toward black while held, back to the scene when
        //     released (auto-release); the held to-black velocity sets the rate.
        //   • From black — snaps to full black on the note's ONSET, then fades
        //     back up to the scene at the from-black velocity: a "reveal".
        const bool toBlackHeld   = state.isActive (static_cast<std::uint8_t> (kToBlackNote));
        const bool fromBlackHeld = state.isActive (static_cast<std::uint8_t> (kFromBlackNote));

        // From-black at velocity 1 is a "hold black" sentinel: instead of rising
        // back to the scene it stays fully black for as long as it's held. This
        // lets you lay black down first (a vel-1 from-black note) and then reveal
        // with a higher-velocity from-black note that ramps up. Vel >= 2 rises.
        const bool fromBlackHold = fromBlackHeld
            && state.get (static_cast<std::uint8_t> (kFromBlackNote)).velocity <= 1;

        // Each new note re-triggers, keyed off the held note's START BEAT (so
        // back-to-back clip notes, which never read as "released" between them,
        // still fire): from-black snaps to full black then rises to the scene;
        // to-black snaps to the full scene ("full on") then falls to black.
        if (fromBlackHeld)
        {
            const auto& fb = state.get (static_cast<std::uint8_t> (kFromBlackNote));
            if (fb.startBeat != bump->lastFromBlackStart)
            {
                bump->blackLevel = 1.0f;
                bump->blackVel   = static_cast<float> (fb.velocity);
                bump->lastFromBlackStart = fb.startBeat;
            }
        }
        else
        {
            bump->lastFromBlackStart = -1.0;   // next from-black note is a fresh onset
        }
        if (toBlackHeld)
        {
            const auto& tb = state.get (static_cast<std::uint8_t> (kToBlackNote));
            if (tb.startBeat != bump->lastToBlackStart)
            {
                bump->blackLevel = 0.0f;   // snap to full scene, then fall to black
                bump->blackVel   = static_cast<float> (tb.velocity);
                bump->lastToBlackStart = tb.startBeat;
            }
        }
        else
        {
            bump->lastToBlackStart = -1.0;     // next to-black note is a fresh onset
        }

        // While a black note is HELD, glide toward its target (to-black → black,
        // from-black → scene) at the note's own velocity rate. When NEITHER is
        // held the note has ended — reset straight to the scene, so the next
        // note starts fresh (each beat re-snaps to black / restarts the fade).
        if (toBlackHeld || fromBlackHeld)
        {
            // to-black → black; from-black → scene, unless the vel-1 "hold
            // black" sentinel keeps the target at full black.
            const float blackTarget = (toBlackHeld || fromBlackHold) ? 1.0f : 0.0f;
            const float blackRate   = ratePerBlock (bump->blackVel);
            if (bump->blackLevel < blackTarget) bump->blackLevel = std::min (blackTarget, bump->blackLevel + blackRate);
            else                                bump->blackLevel = std::max (blackTarget, bump->blackLevel - blackRate);
        }
        else
        {
            bump->blackLevel = 0.0f;   // note ended → reset to scene
        }

        const float cov  = bump->flash.level;   // flash coverage 0..1
        const float bri  = bump->flash.amount;  // captured velocity (brightness)
        const float kCut = bump->blackLevel;    // fade-to-black dim factor 0..1

        if (cov > 0.0f || kCut > 0.0f)
        {
            if (sel != nullptr)
                sel->clear();

            // Single bump: white when no primary colour is held, else the primary
            // hue. (W channel on the spots flashes only on a white bump.)
            const bool  colWhite = primaryC.pitch < 0;
            const float colR = colWhite ? 1.0f : primaryC.r;
            const float colG = colWhite ? 1.0f : primaryC.g;
            const float colB = colWhite ? 1.0f : primaryC.b;
            const float colW = colWhite ? 1.0f : 0.0f;

            // Bump flash overlay: scene → flash toward the target. The
            // to/from-black dip (× (1 - kCut)) is applied to the BARS on top; the
            // SPOTS ignore it on purpose — only the blackout note (C-2) takes the
            // spots dark, so a fade-to-black can drop the rig while the singer
            // spots stay lit. Spots still take the bump flash.
            auto flash = [=] (float v, float tgt) -> float
            {
                return v * (1.0f - cov) + tgt * bri * cov;
            };

            for (int barIdx = 0; barIdx < grid.rig.cols; ++barIdx)
            {
                for (int pixel = 1; pixel <= grid.rig.rows; ++pixel)
                {
                    const auto ch = grid.rig.channelsFor (barIdx, pixel);
                    out.set (ch[0], flash (out.get (ch[0]), colR * ledMasterDim) * (1.0f - kCut));
                    out.set (ch[1], flash (out.get (ch[1]), colG * ledMasterDim) * (1.0f - kCut));
                    out.set (ch[2], flash (out.get (ch[2]), colB * ledMasterDim) * (1.0f - kCut));
                }
            }
            for (int s = 0; s < kNumSpots; ++s)
            {
                const auto& spot = kSpots[s];
                out.set (spot.dimmer(), flash (out.get (spot.dimmer()), spotMasterDim));
                out.set (spot.red(),    flash (out.get (spot.red()),    colR));
                out.set (spot.green(),  flash (out.get (spot.green()),  colG));
                out.set (spot.blue(),   flash (out.get (spot.blue()),   colB));
                out.set (spot.white(),  flash (out.get (spot.white()),  colW));
            }
        }
    }
}

}  // namespace hitnotedmx
