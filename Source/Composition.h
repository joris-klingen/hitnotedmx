#pragma once

#include <array>
#include <cstdint>

#include "EnttecProDmx.h"   // kDmxUniverseSize
#include "MidiState.h"
#include "Rig.h"            // kNumBars, kPixelsPerBar

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
    // [bar][pixel], pixel 1-based (index 0 unused) to match the rig.
    std::array<std::array<bool, kPixelsPerBar + 1>, kNumBars> cell {};

    void clear() noexcept { for (auto& b : cell) b.fill (false); }
};

// Persistent state for the master controls, advanced in beat-time by computeDmx
// (analogous to ColorFadeState — the notes are gone after release, so the
// envelopes have to live across blocks).
//   • white / colour: momentary flash bumps. `level` snaps to 1 on note-on
//     (instant attack) and decays to 0 on release over the "Release" time;
//     `amount` is the captured velocity (flash brightness).
//   • blackLevel / blackTarget: the to/from-black master fader. "To black"
//     sets the target to 1, "From black" to 0; `blackLevel` glides toward it
//     at the same Release rate. Output is scaled by (1 - blackLevel).
// Pass nullptr to disable the master controls (e.g. the offline render tool).
struct BumpState
{
    struct Env { float level = 0.0f; float amount = 0.0f; };
    Env white, colour;
    float blackLevel    = 0.0f;     // 0 = scene, 1 = full black (to/from-black fader)
    float blackVel      = 127.0f;   // captured to/from-black note velocity → fade rate
    bool  prevFromBlack = false;    // from-black note-onset edge (snap to instant black)

    // Animation clock that PAUSES while freeze is held, so releasing freeze
    // continues exactly from the frozen frame (rather than jumping to where the
    // song moved on to). `animBeats` advances by the raw beat delta only on
    // non-frozen blocks; computeDmx runs the recipes + bump tails on it.
    double animBeats = 0.0;
    double lastRaw   = 0.0;     // last raw tBeats seen (for the freeze delta)
    bool   haveRaw   = false;

    double lastBeats = 0.0;     // last animBeats seen by the bump-tail delta
    bool   haveLast  = false;

    // Clears the transient flash tails; the to/from-black fade level persists.
    void reset() noexcept { white = colour = Env {}; haveLast = false; }
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
// Master / global hits at the top of the keyboard are the exceptions to
// "fully written each block":
//   • Freeze (124) returns BEFORE the clear, so `out` holds the previous
//     frame untouched while held (blackout still dominates freeze).
//   • Bump-white (120) / bump-colour (121) crossfade the whole frame toward
//     white / the current primary hue (velocity = brightness): instant attack,
//     release tail back to the scene. To-black (122) fades the whole frame
//     (Multicolor included) to black while held and back to the scene on
//     release; from-black (123) snaps to instant black on its onset then fades
//     up to the scene. To/from-black glide at their OWN note's velocity; the
//     Release note (125) velocity sets the bump-tail rate (both 127 = instant,
//     0 = one bar). See BumpState / section 9.
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
                 float ledMasterDim = 1.0f, float spotMasterDim = 1.0f,
                 ColorFadeState* fade = nullptr, double dtSeconds = 0.0,
                 float pixelDensity = 1.0f, SelectionMask* selection = nullptr,
                 BumpState* bump = nullptr) noexcept;

}  // namespace hitnotedmx
