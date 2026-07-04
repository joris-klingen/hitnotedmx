#pragma once

#include <array>
#include <cstdint>

#include "EnttecProDmx.h"   // kDmxUniverseSize
#include "MidiState.h"
#include "Rig.h"            // Rig, kMaxBars, kMaxRows

namespace hitnotedmx
{

// Per-channel DMX values in [0, 1]. 1-based DMX channel addressing
// (channel 1 lives at data[0]). Preallocated once and reused — the
// composition pass never allocates.
class DmxValues
{
public:
    static constexpr int kSize = kDmxUniverseSize;

    void clear() noexcept
    {
        for (auto& v : data) v = 0.0f;
    }

    // dmxChannel is 1-based (1..512).
    void set (int dmxChannel, float v) noexcept
    {
        if (dmxChannel >= 1 && dmxChannel <= kSize)
            data[dmxChannel - 1] = v;
    }

    float get (int dmxChannel) const noexcept
    {
        if (dmxChannel >= 1 && dmxChannel <= kSize)
            return data[dmxChannel - 1];
        return 0.0f;
    }

    // Raw 0-based view for fast scans.
    const float* raw() const noexcept { return data.data(); }
    float*       raw() noexcept       { return data.data(); }

private:
    std::array<float, kSize> data {};
};

// Parallel to DmxValues: the on-screen "armed but unlit" preview. A cell is
// true when a bar/zone selector is held over it but nothing is lighting it
// (no palette colour, no dynamic/strobe) — computeDmx leaves the DMX black
// there and the visualiser draws a grey selection outline instead. Never
// affects DMX. Preallocated and reused; the composition pass never allocates.
struct SelectionMask
{
    // [bar][pixel], pixel 1-based (index 0 unused) to match the rig. Sized to
    // the max grid; the live shape only touches [0..cols)[1..rows].
    std::array<std::array<bool, kMaxRows + 1>, kMaxBars> cell {};

    void clear() noexcept { for (auto& b : cell) b.fill (false); }
};

// The runtime grid shape plus the lookup tables derived from it (pixel-zone
// masks + the per-bar density rank). rebuild() is noexcept and allocation-free
// (bounded by kMaxBars × kMaxRows²) so the audio thread can call it when the
// editor requests a new shape — all state stays audio-thread-owned.
struct GridState
{
    Rig rig;

    // Pixel statics (pitches 12..23): bit n = pixel (n+1), 1 = bottom.
    // Zones 1-9 are contiguous bottom-up row spans (pixel p → zone
    // ((p-1)*9)/rows, reproducing the original 2-pixel pairs at rows = 18),
    // followed by the Even / Odd / Thirds combs. rows ≤ kMaxRows = 32 keeps
    // each mask a single uint32.
    static constexpr int kNumPixelMasks = 12;
    std::array<std::uint32_t, kNumPixelMasks> pixelMask {};

    // Stable per-pixel gate value in [0, 1) for the pixel-density control —
    // see the comment at GridState::rebuild() in Composition.cpp.
    std::array<std::array<float, kMaxRows + 1>, kMaxBars> densityRank {};

    void rebuild() noexcept;
};

// Persistent state for the master controls, advanced in beat-time by computeDmx
// (analogous to ColorFadeState — the notes are gone after release, so the
// envelopes have to live across blocks).
//   • flash: the single zero-sustain bump. `level` snaps to 1 on each note
//     ONSET (keyed off `lastStart`, the note's start beat) and then decays to 0
//     over the Release length REGARDLESS of how long the note is held — the hold
//     length is irrelevant, so you only program the note start. `amount` is the
//     captured velocity (flash brightness). Target is white, or the primary hue
//     when one is held.
//   • blackLevel / blackVel: the to/from-black master fader. "To black" targets
//     1, "From black" 0; `blackLevel` glides toward it at the note's velocity.
//     Output is scaled by (1 - blackLevel). A from-black note at velocity 1 is a
//     "hold black" sentinel: it stays fully black while held instead of rising.
//   • xfade: the crossfade buffer — the displayed bar RGB (max-grid sized,
//     indexed with a kMaxRows stride so indices are grid-independent), slewed
//     toward the composed scene while the Crossfade note is held.
// Pass nullptr to disable the master controls (e.g. the offline render tool).
struct BumpState
{
    struct Env { float level = 0.0f; float amount = 0.0f; double lastStart = -1.0; };
    Env flash;
    float  blackLevel = 0.0f;       // 0 = scene, 1 = full black (to/from-black fader)
    float  blackVel   = 127.0f;     // captured to/from-black note velocity → fade rate
    double lastFromBlackStart = -1.0;  // start beat of the live from-black note (per-note onset)
    double lastToBlackStart   = -1.0;  // start beat of the live to-black note (per-note onset)

    // Crossfade: the displayed bar frame, slewed toward the scene (bars only).
    std::array<float, kMaxBars * kMaxRows * 3> xfade {};
    bool   xfadeHave = false;       // false = (re)initialise to the scene next block

    // Real reverse: the phase clock for the reversible movers (chases/breathes).
    // Integrates the beat delta forward, or backward while the Reverse note is
    // held, so they retrace from the current state; kept >= 0 (recipes wrap on
    // non-negative t). A clock, like animBeats — NOT cleared by reset().
    double recipePhase    = 0.0;
    double lastPhaseBeats = 0.0;
    bool   havePhase      = false;

    // Animation clock that PAUSES while freeze is held, so releasing freeze
    // continues exactly from the frozen frame (rather than jumping to where the
    // song moved on to). `animBeats` advances by the raw beat delta only on
    // non-frozen blocks; computeDmx runs the recipes + bump tails on it.
    double animBeats = 0.0;
    double lastRaw   = 0.0;     // last raw tBeats seen (for the freeze delta)
    bool   haveRaw   = false;

    double lastBeats = 0.0;     // last animBeats seen by the bump-tail delta
    bool   haveLast  = false;

    // Clears the transient flash tail + crossfade; the to/from-black level persists.
    void reset() noexcept { flash = Env {}; haveLast = false; xfadeHave = false; }

    // Re-lock the recipe clocks to the CURRENT playhead time: the next
    // computeDmx re-seeds animBeats (= tBeats) and recipePhase (= tBeats ×
    // speed) instead of integrating a delta. Called by the processor when the
    // host transport starts or jumps backwards (a loop wrap), so the movers
    // restart in step with the song — a snake begins at the bottom again —
    // rather than continuing from wherever their accumulated clocks were.
    void resyncClocks() noexcept { havePhase = false; haveRaw = false; }
};

// Compute per-channel DMX state at the given playhead time:
//
//   1. If the blackout pitch (0, C-2) is held, all channels are 0.
//   2. Compute primary and secondary palette colors from active
//      color notes (most recent wins; no crossfade — see comment in
//      Composition.cpp for the simplification).
//   3. Build bar / pixel / dynamic masks from active utility notes.
//      Each layer defaults to "all lit" if no note in that layer is
//      held. Lit pixels = intersection.
//   4. Per pixel, choose color route: pixel > bar (most-specific wins),
//      falling back to primary. If a self-coloured dynamic recipe (the
//      Multicolor bank, pitches 60..83) is held, its RGB replaces the
//      palette route entirely. Apply color × brightness to the three DMX
//      channels of that pixel.
//   5. Spots: warm-white tint if WW pitches held, plus secondary
//      colour overlay if their sec pitches held. RGBW channels set
//      directly (dimmer master).
//
// `outValues` is cleared at entry; bars and spots are fully written
// for every audio block (DMX channels never inherit stale state from
// a previous block). Channels outside the rig footprint are untouched.
//
// Master / global hits are the exceptions to "fully written each block":
//   • Freeze (123) returns BEFORE the clear, so `out` holds the previous
//     frame untouched while held (blackout still dominates freeze).
//   • Flip (125) mirrors recipe direction (mirrored sampling coords — instant
//     spatial flip); Reverse (124) runs the chases/breathes phase clock backward
//     so they retrace from the current state (Wild/Multicolor keep absolute
//     time); Spread (126) phase-offsets each bar's clock; Speed (127) scales it.
//   • Crossfade (122) slews the BAR frame toward the composed scene so look
//     changes glide (velocity = fade length); bars only.
//   • Bump (120) flashes the whole frame toward white, or the primary hue if
//     one is held (velocity = brightness). ZERO SUSTAIN: each note ONSET fires
//     an instant flash that decays back to the scene regardless of hold, so
//     only the note start matters; the Release note (121) velocity sets the
//     release length (1/16 note .. 1 bar). To-black (10, A#-2) fades the BARS
//     (spots excluded — only blackout C-2 takes the spots dark) from the scene
//     to black; from-black (9, A-2) snaps to black then rises — EXCEPT at
//     velocity 1, where it holds full black (a "stay black" sentinel). BOTH
//     reset to the scene when the note ends (per-note via start beat, so a note
//     on every beat restarts it), gliding at their OWN note's velocity (127 =
//     instant, 0 = one bar). See BumpState / section 9.
//
// `ledMasterDim` (0..1) scales every bar pixel's RGB output; the spot
// fixtures' master intensity (their dimmer channel) is scaled by
// `spotMasterDim`. Both default to 1.0 (no attenuation). Applying them
// here — rather than at the driver push — means the on-screen preview
// reflects the masters too. These map to host-automatable parameters so
// a MIDI controller knob can ride them live.
//
// Colour fades: the live model can't see future note end-times, so
// instead of the offline crossfade we ramp each palette's displayed
// colour linearly toward the current winner. When the winning colour
// note changes, a fresh fade starts from the colour on screen; the fade
// duration comes from that note's velocity (hard = instant, soft =
// slow), which makes a soft "black" palette note a slow fade-to-black.
// State persists across blocks in `fade` (owned by the processor) and is
// advanced by `dtSeconds` (this block's wall-clock duration). Pass
// fade == nullptr to keep the old snap behaviour.
struct ColorFadeState
{
    struct Channel
    {
        float  cur[3]    { 0.0f, 0.0f, 0.0f };  // displayed RGB (post-intensity)
        float  start[3]  { 0.0f, 0.0f, 0.0f };  // RGB when the current fade began
        double durSec    { 0.0 };               // length of the current fade
        double elapsed   { 0.0 };               // time into the current fade
        int    lastPitch { -1 };                // winning pitch last block (-1 = none)
    };

    Channel primary;
    Channel secondary;

    void reset() noexcept { primary = Channel {}; secondary = Channel {}; }
};

// `pixelDensity` (0..1, default 1.0) is the dark-room thinning control:
// below 1.0 it blanks a stable subset of the bar pixels so fewer LEDs are
// lit, while the pixels that stay on keep full brightness (it gates on/off,
// it does not dim). Pixels drop in a fixed random order (avalanche hash,
// rank-normalised per bar so every bar keeps the same lit fraction), so
// lowering density removes pixels without flicker and without diagonal
// banding. Spots are unaffected.
void computeDmx (const MidiState& state, double tBeats, DmxValues& outValues,
                 const GridState& grid,
                 float ledMasterDim = 1.0f, float spotMasterDim = 1.0f,
                 ColorFadeState* fade = nullptr, double dtSeconds = 0.0,
                 float pixelDensity = 1.0f, SelectionMask* selection = nullptr,
                 BumpState* bump = nullptr) noexcept;

}  // namespace hitnotedmx
