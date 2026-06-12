#include "Showcase.h"

#include <juce_audio_basics/juce_audio_basics.h>
#include <vector>

#include "BinaryData.h"
#include "Palette.h"
#include "Recipes.h"

namespace hitnotedmx
{
namespace Showcase
{

namespace
{
constexpr int kTpqn = 960;   // ticks per quarter note (= 1 beat)
constexpr int kVel  = 100;   // mid velocity (the user can edit in the DAW)

// One timed note inside a clip: pitch, start beat, length in beats.
struct Ev { int pitch; double startBeat; double lenBeats; };

Ev held (int pitch, double bars) { return { pitch, 0.0, bars * 4.0 }; }   // note for the whole clip

juce::String legal (const juce::String& s) { return juce::File::createLegalFileName (s); }

// Write a .mid from timed events. Idempotent — never overwrites an existing file.
void writeClip (const juce::File& file, const std::vector<Ev>& evs)
{
    if (file.existsAsFile())
        return;
    juce::MidiMessageSequence seq;
    for (const auto& e : evs)
    {
        seq.addEvent (juce::MidiMessage::noteOn  (1, e.pitch, static_cast<juce::uint8> (kVel)),
                      e.startBeat * kTpqn);
        seq.addEvent (juce::MidiMessage::noteOff (1, e.pitch),
                      (e.startBeat + e.lenBeats) * kTpqn);
    }
    seq.updateMatchedPairs();

    juce::MidiFile mf;
    mf.setTicksPerQuarterNote (kTpqn);
    mf.addTrack (seq);

    file.getParentDirectory().createDirectory();
    if (auto os = file.createOutputStream())
        mf.writeTo (*os);
}

// --- vocabulary pitches (single source of truth: the bank/palette constants)
const int kBar1 = 5, kBar2 = 6, kBar3 = 7, kBar4 = 8;
const int kSpotLww = 1, kSpotRww = 3;
const int kZone1 = 12;                                   // zones run 12..20
const int kRed     = kPrimaryPaletteStart + 1;
const int kYellow  = kPrimaryPaletteStart + 5;
const int kGreen   = kPrimaryPaletteStart + 7;
const int kCyan    = kPrimaryPaletteStart + 10;
const int kBlue    = kPrimaryPaletteStart + 12;
const int kMagenta = kPrimaryPaletteStart + 17;
const int kChaseUp = kChasesStart + 0, kChaseDn = kChasesStart + 1;
const int kSnake   = kChasesStart + 5;
const int kDiagUp  = kChasesStart + 3, kDiagDn = kChasesStart + 4;
const int kRipple  = kBreathesStart + 2;
const int kSparkle = kWildStart + 1;
const int kRainbow = kColorDynStart + 0, kFire = kColorDynStart + 4;
const int kOcean   = kColorDynStart + 9, kPolice = kColorDynStart + 20;

// Default Ableton User Library MIDI Effect Rack folder (where the rack shows
// up under MIDI Effects). Falls back to the showcase folder if Ableton isn't
// installed there.
juce::File rackTarget (const juce::File& root)
{
    auto ul = juce::File::getSpecialLocation (juce::File::userMusicDirectory)
                  .getChildFile ("Ableton").getChildFile ("User Library");
    if (ul.isDirectory())
        return ul.getChildFile ("Presets").getChildFile ("MIDI Effects")
                 .getChildFile ("MIDI Effect Rack").getChildFile ("Hitnotenames.adg");
    return root.getChildFile ("Ableton Rack").getChildFile ("Hitnotenames.adg");
}

void writeReadme (const juce::File& root, const juce::File& rack)
{
    auto readme = root.getChildFile ("README.txt");
    if (readme.existsAsFile())
        return;
    readme.replaceWithText (
        "HitNoteDmx Showcase\n"
        "===================\n\n"
        "MIDI Clips/Combos/\n"
        "  Layered example clips — drag any onto a track routed to HitNoteDmx.\n"
        "  Some hold a static look; others move within the bar (colour pulses,\n"
        "  bar runners, recipe medleys, builds).\n\n"
        "Trigger rack: " + rack.getFullPathName() + "\n"
        "  A MIDI Effect Rack with all 128 notes named to match HitNoteDmx.\n"
        "  Drop it BEFORE HitNoteDmx on a MIDI track to see note names.\n\n"
        "Files here are only created if missing, so your edits are never\n"
        "overwritten. Re-open from the plugin's \"Showcase\" button to restore.\n");
}

}  // namespace


juce::File defaultRoot()
{
    return juce::File::getSpecialLocation (juce::File::userMusicDirectory)
               .getChildFile ("HitNoteDmx Showcase");
}

juce::File installRack()
{
    const auto root = defaultRoot();
    const auto rack = rackTarget (root);
    // Always (re)write so "Init. names" refreshes an out-of-date rack to the
    // names embedded in this build, not just creates one when missing.
    rack.getParentDirectory().createDirectory();
    rack.replaceWithData (BinaryData::Hitnotenames_adg,
                          static_cast<size_t> (BinaryData::Hitnotenames_adgSize));
    // If it lives in the User Library, drop the old redundant copy the showcase
    // folder used to hold (it showed up twice in Ableton).
    if (! rack.isAChildOf (root))
        root.getChildFile ("Ableton Rack").deleteRecursively();
    return rack;
}

juce::File writeClips (const juce::File& root)
{
    root.createDirectory();
    writeReadme (root, rackTarget (root));

    auto clips = root.getChildFile ("MIDI Clips");

    // Migrate away from the old per-trigger single-note clips (now redundant —
    // only the layered combos are useful). Safe: these folders are ours.
    for (auto* name : { "Spots & bars", "Pixel zones", "Chases", "Breathes", "Wild", "Multicolor" })
        clips.getChildFile (name).deleteRecursively();

    auto dir = clips.getChildFile ("Combos");

    // ---- 2. Static (held) combos ----------------------------------------
    writeClip (dir.getChildFile ("Red bars + chase up.mid"),
               { held (kBar1, 4), held (kBar2, 4), held (kBar3, 4), held (kBar4, 4),
                 held (kRed, 4), held (kChaseUp, 4) });
    writeClip (dir.getChildFile ("Green chase + sparkle.mid"),
               { held (kBar1, 4), held (kBar2, 4), held (kBar3, 4), held (kBar4, 4),
                 held (kGreen, 4), held (kChaseUp, 4), held (kSparkle, 4) });
    writeClip (dir.getChildFile ("Rainbow snake.mid"),     { held (kSnake, 4), held (kRainbow, 4) });
    writeClip (dir.getChildFile ("Ocean ripple.mid"),      { held (kRipple, 4), held (kOcean, 4) });
    writeClip (dir.getChildFile ("Fire wall.mid"),         { held (kFire, 4) });
    writeClip (dir.getChildFile ("Police lights.mid"),     { held (kPolice, 4) });
    writeClip (dir.getChildFile ("Spots + strobe.mid"),    { held (kSpotLww, 4), held (kSpotRww, 4),
                                                             held (kStrobePitch, 4) });

    // ---- 3. Moving (within-the-bar) combos ------------------------------
    {   // bars stay lit; the colour swaps every beat
        std::vector<Ev> e { held (kBar1, 4), held (kBar2, 4), held (kBar3, 4), held (kBar4, 4) };
        const int cols[] { kRed, kGreen, kBlue, kYellow };
        for (int b = 0; b < 16; ++b) e.push_back ({ cols[b % 4], double (b), 1.0 });
        writeClip (dir.getChildFile ("Colour pulse bars.mid"), e);
    }
    {   // one bar lit per beat, marching across the rig
        std::vector<Ev> e { held (kCyan, 4) };
        const int bars[] { kBar1, kBar2, kBar3, kBar4 };
        for (int b = 0; b < 16; ++b) e.push_back ({ bars[b % 4], double (b), 1.0 });
        writeClip (dir.getChildFile ("Bar runner.mid"), e);
    }
    {   // pixel zones sweep up the bars over the clip
        std::vector<Ev> e { held (kGreen, 4) };
        for (int z = 0; z < 9; ++z) e.push_back ({ kZone1 + z, z * (16.0 / 9.0), 16.0 / 9.0 });
        writeClip (dir.getChildFile ("Zone sweep up.mid"), e);
    }
    {   // a different chase every beat
        std::vector<Ev> e { held (kMagenta, 4) };
        const int rec[] { kChaseUp, kChaseDn, kDiagUp, kDiagDn };
        for (int b = 0; b < 16; ++b) e.push_back ({ rec[b % 4], double (b), 1.0 });
        writeClip (dir.getChildFile ("Recipe medley.mid"), e);
    }
    {   // strobe punches on each beat over a steady red wash
        std::vector<Ev> e { held (kBar1, 4), held (kBar2, 4), held (kBar3, 4), held (kBar4, 4), held (kRed, 4) };
        for (int b = 0; b < 16; ++b) e.push_back ({ kStrobePitch, double (b), 0.5 });
        writeClip (dir.getChildFile ("Strobe bursts.mid"), e);
    }
    // a build: bars+blue, then add a chase, then sparkle, then strobe each bar
    writeClip (dir.getChildFile ("Build up.mid"),
               { held (kBar1, 4), held (kBar2, 4), held (kBar3, 4), held (kBar4, 4), held (kBlue, 4),
                 { kChaseUp, 4.0, 12.0 }, { kSparkle, 8.0, 8.0 }, { kStrobePitch, 12.0, 4.0 } });

    return clips;
}

}  // namespace Showcase
}  // namespace hitnotedmx
