// recipe-render — offline film-strip / contact-sheet previews of the dynamic
// recipes, so a recipe can be eyeballed and iterated without launching the
// plugin in a host.
//
// It drives the REAL composition path: it holds a single recipe note in a
// MidiState and calls computeDmx() at stepping playhead times, exactly as the
// audio thread does, then rasterises the resulting bar pixels into a PNG. So
// what you see is what the rig does — velocity semantics, the white default,
// self-coloured Multicolor recipes — not a re-implementation that could drift.
//
// The output is a single static PNG (frames tiled left→right over time) rather
// than a GIF on purpose: a film strip shows every frame at once, which is what
// you want when comparing the shape of an animation frame by frame.
//
//   recipe-render strip <out.png> <pitch> [bars] [subdiv] [levels]
//       One recipe as a VELOCITY LADDER — rows = velocity levels (low→high),
//       columns = time. Velocity is each recipe's main control (speed for Wild,
//       tail for Chases, density for Breathes), so one value is never the whole
//       story; the ladder shows the full range at once.
//
//   recipe-render sheet <out.png> <bank> [vel] [bars] [subdiv]
//       One row per recipe in a bank at a single velocity — a survey sheet.
//       bank ∈ { chases, breathes, wild, color, all }.
//
//   recipe-render stats <bank> [vel] [bars] [subdiv]
//       Coverage table (no image): per recipe, the mean light "fill" (mean LED
//       intensity), the lit-LED coverage, and its min/max over time — for
//       judging whether a recipe holds the stage without blasting it.
//
//   recipe-render clip <out.png> <in.mid> [subdiv] [bpm]
//       Render a whole COMBINATION CLIP as a film strip: parse the .mid, feed
//       its notes into a MidiState as their on/off beats pass, and compose every
//       frame through computeDmx — so a designed clip previews exactly as the rig
//       plays it. This is the inspection contract the design tool (hitdesigndmx)
//       calls; it authors the .mid, hitnotedmx renders it (no engine re-impl, no
//       drift). The driver-level strobe shutter is the one thing not reproduced.
//
//   recipe-render contact <out.png> <dir> [subdiv] [bpm]
//       Survey a whole folder of clips — one row per .mid (recursive), a coarse
//       grid — for eyeballing a generated library at a glance.
//
// Time is sampled musically: `bars` × `subdiv` steps-per-bar. Default is 4 bars
// at 1/16 (slow recipes need several bars to reveal their motion; many read
// best at low-mid velocity). Strip also defaults to 4 velocity levels; sheet
// and stats to vel 64. The beat-synced Wild recipes move fast, so judge them at
// a fine grid AND across velocity — a coarse, single-velocity sample can catch
// a fast mover mid-dark and read as empty when it's just running quick.

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <algorithm>
#include <cmath>
#include <iostream>

#include "Composition.h"
#include "MidiState.h"
#include "Recipes.h"
#include "Rig.h"
#include "TriggerVocabulary.h"

using namespace hitnotedmx;

namespace
{
// ---- screen geometry --------------------------------------------------------
constexpr int kPxW      = 9;   // one rig pixel, on screen
constexpr int kPxH      = 7;
constexpr int kBarGap   = 2;   // between bars within one frame
constexpr int kFrameGap = 7;   // between time frames
constexpr int kRowGap   = 12;  // between recipe rows (sheet)
constexpr int kLeftPad  = 104; // room for the recipe label
constexpr int kTopPad   = 18;  // room for the time labels
constexpr int kEdgePad  = 8;

// The offline tools render at the DEFAULT rig shape (the runtime grid is a
// live-plugin concern; previews judge the recipes, which are parametric).
constexpr int kFrameW = kDefaultBars * kPxW + (kDefaultBars - 1) * kBarGap;
constexpr int kFrameH = kDefaultRows * kPxH;

const GridState& defaultGrid()
{
    static const GridState grid = [] { GridState g; g.rebuild(); return g; }();
    return grid;
}

const juce::Colour kBg { 0xff141414 };

// Mirror DmxVisualizer's display curve so previews track PERCEIVED rig output
// (the fixtures' own dimming makes low DMX already fairly bright). Linear DMX
// in, gamma-lifted screen byte out.
juce::uint8 screenByte (float v) noexcept
{
    const float curved = v <= 0.0f ? 0.0f : std::pow (v, 0.45f);
    return static_cast<juce::uint8> (juce::jlimit (0, 255, static_cast<int> (curved * 255.0f + 0.5f)));
}

// Draw the default 4×18 rig at one instant into the box whose top-left is (x, y).
void drawFrame (juce::Graphics& g, const DmxValues& v, int x, int y)
{
    const Rig& rig = defaultGrid().rig;
    for (int barIdx = 0; barIdx < rig.cols; ++barIdx)
    {
        const int bx = x + barIdx * (kPxW + kBarGap);

        for (int pixel = 1; pixel <= rig.rows; ++pixel)
        {
            const auto ch = rig.channelsFor (barIdx, pixel);
            const int row = rig.rows - pixel;            // pixel 1 at the bottom
            const int py  = y + row * kPxH;
            g.setColour (juce::Colour::fromRGB (screenByte (v.get (ch[0])),
                                                screenByte (v.get (ch[1])),
                                                screenByte (v.get (ch[2]))));
            g.fillRect (bx, py, kPxW, kPxH - 1);          // 1px gutter between rows
        }
    }
}

// Encode an image to PNG, truncating any existing file.
int writePng (const juce::Image& img, const juce::File& out)
{
    juce::PNGImageFormat png;
    juce::FileOutputStream stream (out);
    if (! stream.openedOk())
    {
        std::cerr << "cannot write " << out.getFullPathName() << '\n';
        return 1;
    }
    stream.setPosition (0);
    stream.truncate();
    if (! png.writeImageToStream (img, stream))
    {
        std::cerr << "PNG encode failed\n";
        return 1;
    }
    std::cout << "wrote " << out.getFullPathName() << "  ("
              << img.getWidth() << "x" << img.getHeight() << ")\n";
    return 0;
}

struct Bank { const char* name; int start; int count; };

bool resolveBank (const juce::String& s, std::vector<int>& pitches)
{
    const Bank banks[] = {
        { "chases",   kChasesStart,   kNumChases   },
        { "breathes", kBreathesStart, kNumBreathes },
        { "wild",     kWildStart,     kNumWild     },
        { "color",    kColorDynStart, kNumColorDyn },
    };

    auto append = [&pitches] (const Bank& b)
    {
        for (int i = 0; i < b.count; ++i) pitches.push_back (b.start + i);
    };

    if (s == "all")
    {
        for (const auto& b : banks) append (b);
        return true;
    }
    for (const auto& b : banks)
        if (s == b.name) { append (b); return true; }
    return false;
}

// `levels` velocities spread low→high across the usable range. One level falls
// back to a mid value. The low end is 20 (many recipes read nicest slow) and
// the top is full 127 so the fast extreme is always shown.
std::vector<int> velLevels (int levels)
{
    std::vector<int> v;
    if (levels <= 1) { v.push_back (64); return v; }
    constexpr int lo = 20, hi = 127;
    for (int i = 0; i < levels; ++i)
        v.push_back (juce::roundToInt (lo + (hi - lo) * (double) i / (levels - 1)));
    return v;
}

// One rendered row: a recipe note held at a given velocity, with its labels.
struct Row { int pitch; int vel; juce::String label; juce::String sub; };

// Render rows (each a held recipe note) × frames (time columns) into a PNG.
// `title` is drawn top-left — the recipe name for a velocity ladder, the bank
// for a survey; pass empty to omit.
int renderRows (const juce::File& out, const std::vector<Row>& rows,
                int frames, double bpf, const juce::String& title)
{
    const int nRows  = static_cast<int> (rows.size());
    const int titleH = title.isEmpty() ? 0 : 20;
    const int top    = kTopPad + titleH;
    const int w = kLeftPad + frames * kFrameW + (frames - 1) * kFrameGap + kEdgePad;
    const int h = top + nRows * kFrameH + (nRows - 1) * kRowGap + kEdgePad;

    juce::Image img (juce::Image::RGB, w, h, true);
    juce::Graphics g (img);
    g.fillAll (kBg);

    if (titleH > 0)
    {
        g.setColour (juce::Colour (0xffd0d0d0));
        g.setFont (juce::FontOptions (14.0f, juce::Font::bold));
        g.drawText (title, 6, 2, w - 12, titleH - 2, juce::Justification::topLeft, false);
    }

    // Time labels along the top, once for all rows.
    g.setColour (juce::Colour (0xff707070));
    g.setFont (juce::FontOptions (10.5f));
    for (int f = 0; f < frames; ++f)
    {
        const int fx = kLeftPad + f * (kFrameW + kFrameGap);
        g.drawText (juce::String (f * bpf, 2) + "b",
                    fx, titleH + 2, kFrameW, 14, juce::Justification::centred, false);
    }

    for (int r = 0; r < nRows; ++r)
    {
        const auto& row  = rows[static_cast<size_t> (r)];
        const int   rowY = top + r * (kFrameH + kRowGap);

        // Hold this one recipe note and step the playhead, exactly like the
        // audio thread. A fresh MidiState per row keeps rows independent.
        MidiState state;
        state.noteOn (static_cast<std::uint8_t> (row.pitch), 1,
                      static_cast<std::uint8_t> (row.vel), 0.0);

        DmxValues vals;
        for (int f = 0; f < frames; ++f)
        {
            computeDmx (state, f * bpf, vals, defaultGrid());
            const int fx = kLeftPad + f * (kFrameW + kFrameGap);
            drawFrame (g, vals, fx, rowY);
        }

        g.setColour (juce::Colour (0xffb0b0b0));
        g.setFont (juce::FontOptions (12.0f));
        g.drawText (row.label, 6, rowY, kLeftPad - 12, 16,
                    juce::Justification::topLeft, false);
        if (row.sub.isNotEmpty())
        {
            g.setColour (juce::Colour (0xff606060));
            g.setFont (juce::FontOptions (10.0f));
            g.drawText (row.sub, 6, rowY + 16, kLeftPad - 12, 14,
                        juce::Justification::topLeft, false);
        }
    }

    juce::PNGImageFormat png;
    juce::FileOutputStream stream (out);
    if (! stream.openedOk())
    {
        std::cerr << "cannot write " << out.getFullPathName() << '\n';
        return 1;
    }
    stream.setPosition (0);
    stream.truncate();
    if (! png.writeImageToStream (img, stream))
    {
        std::cerr << "PNG encode failed\n";
        return 1;
    }
    std::cout << "wrote " << out.getFullPathName() << "  (" << w << "x" << h
              << ", " << nRows << " rows x " << frames << " frames)\n";
    return 0;
}

// ---- clip rendering ---------------------------------------------------------
// Parse a .mid combination clip and render it through the REAL computeDmx path,
// exactly like the audio thread: notes enter a MidiState as their note-on /
// note-off beats pass, and each frame is composed and rasterised. So a clip
// previews identically to how the rig plays it — white-default, velocity
// semantics, master bumps, freeze, colour fades. The one thing not reproduced
// offline is the strobe (a driver-level shutter): it shows as a held white frame.

struct TimedEv { double beat; int pitch; int vel; bool on; };

// Load every note on/off as a beat-stamped event, sorted (offs before ons at an
// equal beat so a same-pitch retrigger keeps the note on). `lastBeat` returns
// the final event time. Time is read in beats (ticks / PPQ), so a clip renders
// the same at any tempo; BPM only scales the colour-fade dt.
bool loadClip (const juce::File& f, std::vector<TimedEv>& evs, double& lastBeat)
{
    juce::FileInputStream in (f);
    if (! in.openedOk()) { std::cerr << "cannot open " << f.getFullPathName() << '\n'; return false; }
    juce::MidiFile mf;
    if (! mf.readFrom (in)) { std::cerr << "not a MIDI file: " << f.getFullPathName() << '\n'; return false; }

    const int tf = mf.getTimeFormat();
    if (tf <= 0) { std::cerr << "SMPTE time format unsupported: " << f.getFullPathName() << '\n'; return false; }
    const double ppq = tf;

    lastBeat = 0.0;
    for (int t = 0; t < mf.getNumTracks(); ++t)
    {
        const auto* seq = mf.getTrack (t);
        for (int i = 0; i < seq->getNumEvents(); ++i)
        {
            const auto& m = seq->getEventPointer (i)->message;
            const double beat = m.getTimeStamp() / ppq;
            if      (m.isNoteOn())  evs.push_back ({ beat, m.getNoteNumber(), m.getVelocity(), true });
            else if (m.isNoteOff()) evs.push_back ({ beat, m.getNoteNumber(), 0, false });
            else continue;
            lastBeat = std::max (lastBeat, beat);
        }
    }
    std::stable_sort (evs.begin(), evs.end(), [] (const TimedEv& a, const TimedEv& b)
    {
        if (a.beat != b.beat) return a.beat < b.beat;
        return a.on < b.on;   // process note-offs before note-ons at the same beat
    });
    return true;
}

struct ClipRow { juce::String label; std::vector<TimedEv> evs; };

// Render clip rows (one per .mid) × time frames into a PNG, stepping each row's
// own MidiState + colour-fade through computeDmx.
int renderClipGrid (const juce::File& out, std::vector<ClipRow>& rows,
                    int bars, int subdiv, double bpm, const juce::String& title, int labelW)
{
    const int    frames = juce::jmax (1, bars * subdiv);
    const double bpf    = 4.0 / subdiv;
    const double dtSec  = (60.0 / bpm) * bpf;
    const int    nRows  = static_cast<int> (rows.size());
    const int    titleH = title.isEmpty() ? 0 : 20;
    const int    top    = kTopPad + titleH;
    const int    w = labelW + frames * kFrameW + (frames - 1) * kFrameGap + kEdgePad;
    const int    h = top + nRows * kFrameH + (nRows - 1) * kRowGap + kEdgePad;

    juce::Image img (juce::Image::RGB, w, h, true);
    juce::Graphics g (img);
    g.fillAll (kBg);

    if (titleH > 0)
    {
        g.setColour (juce::Colour (0xffd0d0d0));
        g.setFont (juce::FontOptions (14.0f, juce::Font::bold));
        g.drawText (title, 6, 2, w - 12, titleH - 2, juce::Justification::topLeft, false);
    }

    // Bar numbers along the top (one per bar — frame grid can be dense).
    g.setColour (juce::Colour (0xff707070));
    g.setFont (juce::FontOptions (10.5f));
    for (int f = 0; f < frames; f += subdiv)
    {
        const int fx = labelW + f * (kFrameW + kFrameGap);
        g.drawText (juce::String (f / subdiv + 1),
                    fx, titleH + 2, kFrameW, 14, juce::Justification::centred, false);
    }

    for (int r = 0; r < nRows; ++r)
    {
        const auto& row  = rows[static_cast<size_t> (r)];
        const int   rowY = top + r * (kFrameH + kRowGap);

        MidiState      state;
        ColorFadeState fade {};
        DmxValues      vals;
        size_t         ei = 0;

        for (int f = 0; f < frames; ++f)
        {
            const double tb = f * bpf;
            while (ei < row.evs.size() && row.evs[ei].beat <= tb + 1e-6)
            {
                const auto& e = row.evs[ei++];
                if (e.on) state.noteOn  (static_cast<std::uint8_t> (e.pitch), 1,
                                         static_cast<std::uint8_t> (e.vel), e.beat);
                else      state.noteOff (static_cast<std::uint8_t> (e.pitch));
            }
            computeDmx (state, tb, vals, defaultGrid(), 1.0f, 1.0f, &fade, dtSec);
            drawFrame (g, vals, labelW + f * (kFrameW + kFrameGap), rowY);
        }

        if (labelW > 12)
        {
            g.setColour (juce::Colour (0xffc0c0c0));
            g.setFont (juce::FontOptions (11.0f));
            g.drawText (row.label, 6, rowY, labelW - 10, kFrameH,
                        juce::Justification::topLeft, true);
        }
    }

    return writePng (img, out);
}

// ---- coverage analysis ------------------------------------------------------
// The rig is 4 bars × 18 floods in a ring behind the band, pointing in, so what
// reads to the audience is the gross light FIELD, not fine spatial shapes. The
// useful measures over time are therefore: how much light is on stage (fill),
// how much of the rig is lit (coverage), and the extremes — does it ever go
// near-dark (stage drops out) or blast everything (flat, blinding).
//
// Intensity per LED = max(r,g,b) of the raw (linear) DMX value: a coloured
// flood at full still lights the stage even with two channels at zero. "Lit" =
// intensity above kLitThresh (the floods are bright, so a low bar is right).
constexpr float kLitThresh = 0.10f;

struct Stats { float meanFill, meanCov, minCov, maxCov; };

Stats analyse (int pitch, int vel, int frames, double bpf)
{
    MidiState state;
    state.noteOn (static_cast<std::uint8_t> (pitch), 1, static_cast<std::uint8_t> (vel), 0.0);

    const Rig& rig = defaultGrid().rig;
    const int kLeds = rig.cells();   // 72 at the default shape
    double fillSum = 0.0, covSum = 0.0;
    float  minCov = 1.0f, maxCov = 0.0f;
    DmxValues vals;

    for (int f = 0; f < frames; ++f)
    {
        computeDmx (state, f * bpf, vals, defaultGrid());
        int lit = 0; double frameFill = 0.0;
        for (int b = 0; b < rig.cols; ++b)
        {
            for (int p = 1; p <= rig.rows; ++p)
            {
                const auto ch = rig.channelsFor (b, p);
                const float inten = juce::jmax (vals.get (ch[0]), juce::jmax (vals.get (ch[1]), vals.get (ch[2])));
                frameFill += inten;
                if (inten > kLitThresh) ++lit;
            }
        }
        const float cov = static_cast<float> (lit) / kLeds;
        fillSum += frameFill / kLeds;
        covSum  += cov;
        minCov = juce::jmin (minCov, cov);
        maxCov = juce::jmax (maxCov, cov);
    }
    return { static_cast<float> (fillSum / frames), static_cast<float> (covSum / frames), minCov, maxCov };
}

int printStats (const juce::String& bank, int vel, int frames, double bpf)
{
    std::vector<int> pitches;
    if (! resolveBank (bank, pitches)) { std::cerr << "unknown bank: " << bank << '\n'; return 2; }

    auto pad = [] (juce::String s, int w) { return s.paddedRight (' ', w); };
    auto pct = [] (float v) { return juce::String (juce::roundToInt (v * 100.0f)) + "%"; };

    std::cout << "vel " << vel << ", " << (frames * bpf / 4.0) << " bars @ 1/" << juce::roundToInt (4.0 / bpf) << "\n"
              << pad ("recipe", 22) << pad ("note", 6) << pad ("fill", 7)
              << pad ("cov", 7) << pad ("min", 7) << pad ("max", 7) << "\n";
    for (int p : pitches)
    {
        const auto s = analyse (p, vel, frames, bpf);
        std::cout << pad (vocab::chainName (p), 22) << pad (juce::String (p), 6)
                  << pad (pct (s.meanFill), 7) << pad (pct (s.meanCov), 7)
                  << pad (pct (s.minCov), 7)   << pad (pct (s.maxCov), 7) << "\n";
    }
    return 0;
}

int usage()
{
    std::cerr <<
        "usage:\n"
        "  recipe-render strip   <out.png> <pitch> [bars] [subdiv] [levels]  velocity ladder\n"
        "  recipe-render sheet   <out.png> <bank>  [vel]  [bars]   [subdiv]  bank survey\n"
        "  recipe-render stats   <bank> [vel] [bars] [subdiv]                coverage table\n"
        "  recipe-render clip    <out.png> <in.mid> [subdiv] [bpm]           one combo clip\n"
        "  recipe-render contact <out.png> <dir>    [subdiv] [bpm]           survey a folder of clips\n"
        "      bank   = chases | breathes | wild | color | all\n"
        "      bars   = musical bars to span          (default 4)\n"
        "      subdiv = sampling steps per bar         (default 16; clip 8; contact 4)\n"
        "      levels = velocity rows, spread 20..127  (strip default 4)\n"
        "      vel    = single MIDI velocity 1..127    (sheet/stats default 64)\n"
        "      bpm    = tempo for colour-fade timing   (clip/contact default 120)\n";
    return 2;
}
}  // namespace

int main (int argc, char** argv)
{
    // Offline image + text rendering needs the GUI/font subsystem initialised,
    // but never opens a window.
    juce::ScopedJuceInitialiser_GUI gui;

    if (argc < 3) return usage();

    const juce::String mode = argv[1];
    constexpr double kBeatsPerBar = 4.0;   // 4/4

    if (mode == "stats")
    {
        // Coverage table for a bank — no output file, prints to stdout.
        const juce::String bank = argv[2];
        const int    vel    = argc > 3 ? juce::String (argv[3]).getIntValue()    : 64;
        const double bars   = argc > 4 ? juce::String (argv[4]).getDoubleValue() : 4.0;
        const int    subdiv = argc > 5 ? juce::String (argv[5]).getIntValue()    : 16;

        if (vel < 1 || vel > 127 || bars <= 0.0 || subdiv < 1)
        { std::cerr << "vel 1..127, bars > 0, subdiv >= 1\n"; return usage(); }

        return printStats (bank, vel, juce::jmax (1, juce::roundToInt (bars * subdiv)),
                           kBeatsPerBar / subdiv);
    }

    if (argc < 4) return usage();

    const juce::File   out    = juce::File::getCurrentWorkingDirectory()
                                    .getChildFile (juce::String (argv[2]));
    const juce::String target = argv[3];

    if (mode == "strip")
    {
        // One recipe as a velocity ladder — rows = velocity levels, cols = time.
        const int    pitch  = target.getIntValue();
        const double bars   = argc > 4 ? juce::String (argv[4]).getDoubleValue() : 4.0;
        const int    subdiv = argc > 5 ? juce::String (argv[5]).getIntValue()    : 16;
        const int    levels = argc > 6 ? juce::String (argv[6]).getIntValue()    : 4;

        if (pitch < 0 || pitch > 127) { std::cerr << "pitch 0..127\n"; return usage(); }
        if (bars <= 0.0 || subdiv < 1 || levels < 1)
        { std::cerr << "bars > 0, subdiv >= 1, levels >= 1\n"; return usage(); }

        const int    frames = juce::jmax (1, juce::roundToInt (bars * subdiv));
        const double bpf    = kBeatsPerBar / subdiv;

        std::vector<Row> rows;
        for (int v : velLevels (levels))
            rows.push_back ({ pitch, v, "vel " + juce::String (v), {} });

        const juce::String title = vocab::chainName (pitch) + "  (note " + juce::String (pitch) + ")";
        return renderRows (out, rows, frames, bpf, title);
    }

    if (mode == "sheet")
    {
        // Bank survey — one row per recipe at a single (mid-low) velocity.
        const int    vel    = argc > 4 ? juce::String (argv[4]).getIntValue()    : 64;
        const double bars   = argc > 5 ? juce::String (argv[5]).getDoubleValue() : 4.0;
        const int    subdiv = argc > 6 ? juce::String (argv[6]).getIntValue()    : 16;

        if (vel < 1 || vel > 127 || bars <= 0.0 || subdiv < 1)
        { std::cerr << "vel 1..127, bars > 0, subdiv >= 1\n"; return usage(); }

        std::vector<int> pitches;
        if (! resolveBank (target, pitches))
        { std::cerr << "unknown bank: " << target << '\n'; return usage(); }

        const int    frames = juce::jmax (1, juce::roundToInt (bars * subdiv));
        const double bpf    = kBeatsPerBar / subdiv;

        std::vector<Row> rows;
        for (int p : pitches)
            rows.push_back ({ p, vel, vocab::chainName (p), juce::String (p) });

        const juce::String title = target + "  @ vel " + juce::String (vel);
        return renderRows (out, rows, frames, bpf, title);
    }

    if (mode == "clip")
    {
        // One combination clip as a filmstrip (bars derived from the file).
        const juce::File in = juce::File::getCurrentWorkingDirectory().getChildFile (target);
        const int    subdiv = argc > 4 ? juce::String (argv[4]).getIntValue()    : 8;
        const double bpm    = argc > 5 ? juce::String (argv[5]).getDoubleValue() : 120.0;
        if (subdiv < 1 || bpm <= 0.0) { std::cerr << "subdiv >= 1, bpm > 0\n"; return usage(); }

        std::vector<ClipRow> rows (1);
        double lastBeat = 0.0;
        if (! loadClip (in, rows[0].evs, lastBeat)) return 1;
        const int bars = juce::jmax (1, static_cast<int> (std::ceil (lastBeat / 4.0)));
        return renderClipGrid (out, rows, bars, subdiv, bpm,
                               in.getFileNameWithoutExtension(), 8);
    }

    if (mode == "contact")
    {
        // Survey every .mid under a folder — one row per clip, coarse grid.
        const juce::File dir = juce::File::getCurrentWorkingDirectory().getChildFile (target);
        const int    subdiv = argc > 4 ? juce::String (argv[4]).getIntValue()    : 4;
        const double bpm    = argc > 5 ? juce::String (argv[5]).getDoubleValue() : 120.0;
        if (! dir.isDirectory()) { std::cerr << "not a directory: " << dir.getFullPathName() << '\n'; return usage(); }
        if (subdiv < 1 || bpm <= 0.0) { std::cerr << "subdiv >= 1, bpm > 0\n"; return usage(); }

        auto files = dir.findChildFiles (juce::File::findFiles, true, "*.mid");
        files.sort();
        if (files.isEmpty()) { std::cerr << "no .mid files under " << dir.getFullPathName() << '\n'; return 1; }

        std::vector<ClipRow> rows;
        int bars = 1;
        for (const auto& f : files)
        {
            ClipRow row;
            row.label = f.getRelativePathFrom (dir);
            double lastBeat = 0.0;
            if (! loadClip (f, row.evs, lastBeat)) continue;
            bars = juce::jmax (bars, static_cast<int> (std::ceil (lastBeat / 4.0)));
            rows.push_back (std::move (row));
        }
        const juce::String title = dir.getFileName() + "  (" + juce::String (rows.size()) + " clips)";
        return renderClipGrid (out, rows, bars, subdiv, bpm, title, 200);
    }

    return usage();
}
