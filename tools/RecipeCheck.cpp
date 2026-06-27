// recipe-check — a fast numeric regression net for the recipe engine.
//
// The recipes + computeDmx are the most intricate, show-critical, audio-thread
// code in the project, yet (before this) the only automated guard was
// `mapping-frozen` (the note→name map). This tool exercises the REAL engine —
// the same recipe functions and the same computeDmx() the audio thread runs —
// and fails (non-zero exit) if anything produces a non-finite (NaN/Inf) or
// out-of-range value. It is NOT a golden-image diff (that's the bigger render
// net in TODO #2); it catches the cheap, high-value class of bug: a recipe or
// composite that blows up its output for some velocity / beat / density.
//
//   recipe-check          run all checks; exit 0 = clean, 1 = a failure found
//
// What it covers:
//   1. Every brightness recipe (Chases / Breathes / Wild) returns a finite
//      value in [0,1] across a beat sweep, every bar/pixel, several tails.
//   2. Every Multicolor recipe returns finite RGB in [0,1], same sweep.
//   3. The dispatch tables: getDynamicRecipe / getColorRecipe are non-null
//      exactly on their banks (and null for the strobe slot + out of range).
//   4. computeDmx() output stays finite + in [0,1] on every DMX channel for a
//      spread of held-note sets (each bank, palettes, spots, master controls,
//      a kitchen sink) × velocities × pixel-density values, over a beat sweep.

#include <juce_events/juce_events.h>

#include <cmath>
#include <iostream>
#include <vector>

#include "Composition.h"
#include "MidiState.h"
#include "Recipes.h"
#include "Rig.h"

using namespace hitnotedmx;

namespace
{
int  gFailures = 0;
int  gPrinted  = 0;
constexpr int kMaxPrint = 25;   // cap the noise; the count is what matters

void fail (const juce::String& msg)
{
    ++gFailures;
    if (gPrinted++ < kMaxPrint)
        std::cerr << "FAIL: " << msg << '\n';
    else if (gPrinted == kMaxPrint + 1)
        std::cerr << "  … (further failures suppressed)\n";
}

// Allow a hair of float slop at the [0,1] edges (recipes accumulate sums).
bool inRange01 (float v) noexcept { return std::isfinite (v) && v >= -1.0e-4f && v <= 1.0f + 1.0e-4f; }

// Musical beat sweep. A coarse, off-grid step (0.3) hits many distinct
// recipe phases without fine over-sampling — NaN / range bugs don't hide
// between adjacent beats — plus a few larger offsets to exercise wrap / fmod
// (well short of the int-cast overflow only a multi-year run could reach).
std::vector<double> beatSweep()
{
    std::vector<double> ts;
    for (double t = 0.0; t <= 12.0; t += 0.3) ts.push_back (t);
    for (double t : { 257.3, 1024.0, 4096.7 }) ts.push_back (t);
    return ts;
}

const std::vector<float> kArgs { 0.0f, 0.5f, 1.0f };   // tail / param: ends + middle

// ---- 1 + 2: direct recipe sweeps -------------------------------------------
void checkBrightnessBank (const char* bank, int start, int count)
{
    const auto ts = beatSweep();
    for (int pitch = start; pitch < start + count; ++pitch)
    {
        auto fn = getDynamicRecipe (pitch);
        if (fn == nullptr)
            continue;   // e.g. the null strobe slot — covered by the dispatch check
        for (double t : ts)
            for (int b = 0; b < kNumBars; ++b)
                for (int p = 1; p <= kPixelsPerBar; ++p)
                    for (float arg : kArgs)
                    {
                        const float v = fn (t, b, p, kPixelsPerBar, kNumBars, arg);
                        if (! inRange01 (v))
                        {
                            fail (juce::String (bank) + " pitch " + juce::String (pitch)
                                  + " → " + juce::String (v) + " (t=" + juce::String (t)
                                  + " bar=" + juce::String (b) + " px=" + juce::String (p)
                                  + " arg=" + juce::String (arg) + ")");
                            return;   // one report per recipe is enough
                        }
                    }
    }
}

void checkColorBank()
{
    const auto ts = beatSweep();
    for (int pitch = kColorDynStart; pitch < kColorDynStart + kNumColorDyn; ++pitch)
    {
        auto fn = getColorRecipe (pitch);
        if (fn == nullptr) { fail ("color dispatch null at pitch " + juce::String (pitch)); continue; }
        for (double t : ts)
            for (int b = 0; b < kNumBars; ++b)
                for (int p = 1; p <= kPixelsPerBar; ++p)
                    for (float arg : kArgs)
                    {
                        const auto c = fn (t, b, p, kPixelsPerBar, kNumBars, arg);
                        if (! inRange01 (c.r) || ! inRange01 (c.g) || ! inRange01 (c.b))
                        {
                            fail ("Multicolor pitch " + juce::String (pitch) + " → ("
                                  + juce::String (c.r) + "," + juce::String (c.g) + ","
                                  + juce::String (c.b) + ") (t=" + juce::String (t)
                                  + " bar=" + juce::String (b) + " px=" + juce::String (p)
                                  + " arg=" + juce::String (arg) + ")");
                            goto next;   // one report per recipe
                        }
                    }
        next: ;
    }
}

// ---- 3: dispatch boundaries -------------------------------------------------
void checkDispatch()
{
    // Brightness banks: every pitch resolves, except the strobe slot (null by
    // design — it's a driver-level shutter, not a per-pixel recipe) and the two
    // empty Chase slots (25 old Chase dn, 28 old Diag dn — reverse is the global
    // Reverse note now).
    for (int p = kChasesStart; p < kChasesStart + kNumChases; ++p)
        if (p != kChasesStart + 1 && p != kChasesStart + 4 && getDynamicRecipe (p) == nullptr)
            fail ("Chases dispatch null at " + juce::String (p));
    for (int p = kBreathesStart; p < kBreathesStart + kNumBreathes; ++p)
        if (getDynamicRecipe (p) == nullptr) fail ("Breathes dispatch null at " + juce::String (p));
    for (int p = kWildStart; p < kWildStart + kNumWild; ++p)
        if (p == kStrobePitch) { if (getDynamicRecipe (p) != nullptr) fail ("strobe slot should be null"); }
        else if (getDynamicRecipe (p) == nullptr) fail ("Wild dispatch null at " + juce::String (p));

    // Outside the banks → null.
    for (int p : { kChasesStart - 1, kColorDynStart, kColorDynStart + kNumColorDyn, 0, 127 })
        if (getDynamicRecipe (p) != nullptr) fail ("getDynamicRecipe non-null out of range at " + juce::String (p));
    for (int p : { kColorDynStart - 1, kColorDynStart + kNumColorDyn, 0, 127 })
        if (getColorRecipe (p) != nullptr) fail ("getColorRecipe non-null out of range at " + juce::String (p));
}

// ---- 4: end-to-end computeDmx ----------------------------------------------
MidiState held (const std::vector<int>& pitches, std::uint8_t vel)
{
    MidiState s;
    for (int p : pitches)
        if (p >= 0 && p < 128) s.noteOn (static_cast<std::uint8_t> (p), 1, vel, 0.0);
    return s;
}

void checkComposite (const char* label, const std::vector<int>& pitches)
{
    const auto ts = beatSweep();
    for (std::uint8_t vel : { std::uint8_t (1), std::uint8_t (64), std::uint8_t (127) })
        for (float density : { 0.0f, 0.5f, 1.0f })   // gate edges + middle
        {
            // Fresh stateful helpers per config; advanced across the sweep so
            // the fade / bump envelopes are exercised over time, as in the host.
            ColorFadeState fade;
            BumpState      bump;
            SelectionMask  sel;
            DmxValues      out;
            const MidiState state = held (pitches, vel);

            for (double t : ts)
            {
                computeDmx (state, t, out, 1.0f, 1.0f, &fade, 0.02, density, &sel, &bump);
                const float* v = out.raw();
                for (int i = 0; i < DmxValues::kSize; ++i)
                    if (! inRange01 (v[i]))
                    {
                        fail (juce::String ("computeDmx [") + label + "] ch " + juce::String (i + 1)
                              + " → " + juce::String (v[i]) + " (vel=" + juce::String ((int) vel)
                              + " density=" + juce::String (density) + " t=" + juce::String (t) + ")");
                        return;   // one report per config set
                    }
            }
        }
}
}  // namespace

int main()
{
    checkBrightnessBank ("Chases",   kChasesStart,   kNumChases);
    checkBrightnessBank ("Breathes", kBreathesStart, kNumBreathes);
    checkBrightnessBank ("Wild",     kWildStart,     kNumWild);
    checkColorBank();
    checkDispatch();

    // End-to-end: the compositor math is per-LAYER, not per-recipe (every
    // recipe's own range is already covered by the direct sweeps above), so a
    // representative of each bank is enough — breathe exercises the density
    // mask, sparkle the free-running Wild path, a beat-synced Wild the division,
    // VU meter its special case, two colours the per-channel max.
    checkComposite ("empty",        {});
    checkComposite ("blackout",     { 0 });
    checkComposite ("chase",        { kChasesStart });
    checkComposite ("breathe",      { kBreathesStart });
    checkComposite ("wild sparkle", { kSparklePitch });
    checkComposite ("wild beat",    { kWildStart + 3 });
    checkComposite ("color",        { kColorDynStart });
    checkComposite ("vu meter",     { kVuMeterPitch });
    checkComposite ("primary palette",   { 84 });
    checkComposite ("secondary palette", { 108 });
    checkComposite ("bars",   { 5, 6, 7, 8 });
    checkComposite ("zones",  { 12, 15, 20, 21, 22, 23 });
    checkComposite ("spots",  { 1, 2, 3, 4 });
    checkComposite ("bump",        { 120 });
    checkComposite ("bump rel",    { 121, 120 });
    checkComposite ("crossfade",   { 122, 24 });
    checkComposite ("to black",    { 10, 84 });
    checkComposite ("from black",  { 9, 84 });
    checkComposite ("freeze",      { 123, 24 });
    checkComposite ("reverse",     { 124, 24 });      // real reverse + a chase
    checkComposite ("reverse breathe", { 124, 36 });  // + a breathe (also reversible)
    checkComposite ("flip",        { 125, 24 });
    checkComposite ("spread",      { 126, 24 });
    checkComposite ("speed",       { 127, 24, 48 });
    checkComposite ("two colors",  { 60, 72 });
    checkComposite ("kitchen sink",
        { 5, 6, 7, 8, 15, 24, 48, 62, 84, 108, 1, 3, 9, 10, 120, 121, 122, 123, 124, 125, 126, 127 });

    if (gFailures == 0)
    {
        std::cout << "recipe-check: all checks passed (recipes + dispatch + computeDmx finite/in-range)\n";
        return 0;
    }
    std::cerr << "recipe-check: " << gFailures << " failure(s)\n";
    return 1;
}
