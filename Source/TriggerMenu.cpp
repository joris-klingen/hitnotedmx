#include "TriggerMenu.h"

#include "Palette.h"
#include "Recipes.h"

#include <algorithm>

namespace hitnotedmx
{

namespace
{
constexpr int kHeaderH    = 32;   // title + octave-number strip across the top
constexpr int kRowH       = 22;   // one chromatic note
constexpr int kGutterW    = 20;   // left note-letter gutter (piano-roll keys)
constexpr int kRightMargin = 12;  // striped slack right of the last column
constexpr int kNumRows    = 12;   // C..B
constexpr int kPalColW    = 22;   // narrow colour-chip column
constexpr int kCellPad    = 2;    // gap around each tile (reveals the grid)
constexpr int kOctaveH    = 12;   // octave-number band at the bottom of the header

// Vocabulary octave starts not exported from Composition.cpp — mirror here.
constexpr int kSpotBarOctave    = 0;    // spots 0..3, bars 4..7
constexpr int kPixelStaticStart = 12;

const char* const kNoteLetters[12] =
    { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };

constexpr bool isBlackKey (int offset) noexcept
{
    return offset == 1 || offset == 3 || offset == 6 || offset == 8 || offset == 10;
}

// Note name, C3 = note 60 (Ableton convention). The octave hyphen is dropped
// (e.g. "C-2" → "C2") so short tiles never truncate at the "#".
juce::String noteName (int pitch)
{
    const int octave = pitch / 12 - 2;  // 60 -> C3, 0 -> C-2
    return juce::String (kNoteLetters[pitch % 12]) + juce::String (octave).removeCharacters ("-");
}

juce::Colour paletteColour (int index)
{
    const auto& c = kPalette[index];
    return juce::Colour::fromFloatRGBA (c.r, c.g, c.b, 1.0f);
}
}  // namespace


TriggerMenu::TriggerMenu()
{
    buildModel();
}

void TriggerMenu::buildModel()
{
    columns.clear();

    // A trigger column: labels occupy note offsets 0,1,2,… from `octaveStart`.
    auto trigCol = [] (juce::String title, int octaveStart,
                       std::vector<juce::String> labels)
    {
        Column c;
        c.title  = std::move (title);
        c.octave = octaveStart / 12 - 2;
        for (int i = 0; i < static_cast<int> (labels.size()) && i < kNumRows; ++i)
            c.cells[static_cast<size_t> (i)] =
                { octaveStart + i, std::move (labels[static_cast<size_t> (i)]), {}, false, true };
        return c;
    };

    // A palette column: 12 colour swatches, one full octave. `paletteBase` is
    // the palette index at this octave's C (0 for the low octave, 12 the high).
    auto palCol = [] (juce::String group, int octaveStart, int paletteBase)
    {
        Column c;
        c.group   = std::move (group);
        c.octave  = octaveStart / 12 - 2;
        c.palette = true;
        for (int o = 0; o < kNumRows; ++o)
            c.cells[static_cast<size_t> (o)] =
                { octaveStart + o, noteName (octaveStart + o), paletteColour (paletteBase + o), true, true };
        return c;
    };

    // Octave -2: total blackout (C-2) sits at the bottom, spots + bars above.
    columns.push_back (trigCol ("Spots & bars", kSpotBarOctave,
        { "Blackout", "Spot L WW", "Spot L col", "Spot R WW", "Spot R col",
          "Bar 1", "Bar 2", "Bar 3", "Bar 4" }));

    columns.push_back (trigCol ("Pixel zones", kPixelStaticStart,
        { "Zone 1", "Zone 2", "Zone 3", "Zone 4", "Zone 5", "Zone 6",
          "Zone 7", "Zone 8", "Zone 9", "Even", "Odd", "Thirds" }));

    columns.push_back (trigCol ("Chases", kChasesStart,
        { "Chase up", "Chase dn", "Ping-pong", "Snake", "Spiral", "Converge",
          "Diag up", "Diag dn", "Waves", "Expand", "Contract", "Snake H" }));

    columns.push_back (trigCol ("Breathes", kBreathesStart,
        { "Sine", "Breathe", "Ripple", "Halo", "Moon rise", "Soft ball", "Aurora",
          "Ripple H", "Bloom", "Shimmer", "Sway", "Drift" }));

    columns.push_back (trigCol ("Wild", kWildStart,
        { "Sparkle", "Strobe", "Alt swap", "Rain", "Lightning", "Static",
          "Glitch", "Bounce", "Zigzag", "Sparkle few", "Fast ball", "Pong" }));

    // Multicolor spans two octaves (two columns of self-coloured recipes).
    columns.push_back (trigCol ("Multicolor", kColorDynStart,
        { "Rainbow", "Comet", "VU meter", "Fire", "Desert", "VU smooth",
          "Night sky", "Police", "Embers", "Plasma", "Ocean", "Nebula" }));

    columns.push_back (trigCol ("Multicolor", kColorDynStart + 12,
        { "Sunset", "Forest", "Lava", "Borealis", "Candy", "Magma",
          "Storm", "Galaxy", "Blocks", "Disco", "Twilight", "Heatmap" }));

    // Palettes (shifted up an octave): Primary keeps two octaves, Secondary is
    // one octave (colours 0..11). Grouped under PRIM / SEC headers.
    columns.push_back (palCol ("Prim", kPrimaryPaletteStart,      0));
    columns.push_back (palCol ("Prim", kPrimaryPaletteStart + 12, 12));
    columns.push_back (palCol ("Sec",  kSecondaryPaletteStart,    0));
}

void TriggerMenu::resized()
{
    const int n = static_cast<int> (columns.size());
    colX.assign (static_cast<size_t> (n), 0);
    colWid.assign (static_cast<size_t> (n), 0);

    int nPal = 0, nTrig = 0;
    for (const auto& c : columns) (c.palette ? nPal : nTrig) += 1;

    // Palette columns are fixed-narrow; trigger columns share the remainder
    // (leaving a striped right margin past the last column).
    const int trigTotal = juce::jmax (0, (getWidth() - kGutterW - kRightMargin) - nPal * kPalColW);
    const int trigW     = nTrig > 0 ? juce::jmax (40, trigTotal / nTrig) : 0;

    int x = kGutterW;
    for (int c = 0; c < n; ++c)
    {
        const int cw = columns[static_cast<size_t> (c)].palette ? kPalColW : trigW;
        colX[static_cast<size_t> (c)]   = x;
        colWid[static_cast<size_t> (c)] = cw;
        x += cw;
    }

    // Rows fill whatever height we're given, so the grid maxes out the pane.
    rowH = juce::jmax (14, (getHeight() - kHeaderH - 2) / kNumRows);
}

int TriggerMenu::pitchAt (juce::Point<int> p) const noexcept
{
    const int n = static_cast<int> (columns.size());
    if (n == 0 || colX.empty() || p.x < kGutterW || p.y < kHeaderH)
        return -1;

    int col = -1;
    for (int c = 0; c < n; ++c)
        if (p.x >= colX[static_cast<size_t> (c)]
            && p.x < colX[static_cast<size_t> (c)] + colWid[static_cast<size_t> (c)])
        { col = c; break; }
    if (col < 0)
        return -1;

    const int screenRow = (p.y - kHeaderH) / rowH;
    if (screenRow < 0 || screenRow >= kNumRows)
        return -1;

    const int offset = (kNumRows - 1) - screenRow;   // bottom row = C (offset 0)
    const auto& cell = columns[static_cast<size_t> (col)].cells[static_cast<size_t> (offset)];
    return cell.present ? cell.pitch : -1;
}

bool TriggerMenu::isActive (int pitch) const noexcept
{
    return std::find (active.begin(),     active.end(),     pitch) != active.end()
        || std::find (liveActive.begin(), liveActive.end(), pitch) != liveActive.end();
}

void TriggerMenu::setLiveNotes (const std::vector<int>& heldPitches)
{
    if (heldPitches == liveActive)
        return;
    liveActive = heldPitches;
    repaint();
}

void TriggerMenu::toggle (int pitch)
{
    if (pitch < 0)
        return;
    auto it = std::find (active.begin(), active.end(), pitch);
    if (it != active.end()) active.erase (it);
    else                    active.push_back (pitch);

    repaint();
    if (onSelectionChanged)
        onSelectionChanged (active);
}

void TriggerMenu::mouseDown (const juce::MouseEvent& e)
{
    toggle (pitchAt (e.getPosition()));
}

void TriggerMenu::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff262626));

    const int nCols = static_cast<int> (columns.size());
    if (nCols == 0 || colX.empty())
        return;

    const int fullW = getWidth();

    // ---- Row striping (black keys) + note-letter gutter -----------------
    // The striping spans the WHOLE pane — behind the note-letter gutter and
    // past the last column — so the piano-roll reference is everywhere.
    for (int screenRow = 0; screenRow < kNumRows; ++screenRow)
    {
        const int offset = (kNumRows - 1) - screenRow;
        const int y = kHeaderH + screenRow * rowH;

        if (isBlackKey (offset))
        {
            g.setColour (juce::Colour (0xff1b1b1b));
            g.fillRect (0, y, fullW, rowH);
        }

        // Row note-letter — high contrast, matching the knob-label styling.
        g.setColour (juce::Colours::white);
        g.setFont (juce::FontOptions (9.5f, juce::Font::bold));
        g.drawText (kNoteLetters[offset], 0, y, kGutterW - 2, rowH,
                    juce::Justification::centredRight);
    }

    // ---- Headers: title/group (top) + octave number (bottom) ------------
    const int titleH = kHeaderH - kOctaveH;

    // Grouped palette labels (PRIM / SEC) span their consecutive columns.
    for (int c = 0; c < nCols; )
    {
        const auto& grp = columns[static_cast<size_t> (c)].group;
        if (grp.isEmpty()) { ++c; continue; }
        int c2 = c;
        while (c2 + 1 < nCols && columns[static_cast<size_t> (c2 + 1)].group == grp) ++c2;
        const int x0 = colX[static_cast<size_t> (c)];
        const int x1 = colX[static_cast<size_t> (c2)] + colWid[static_cast<size_t> (c2)];
        g.setColour (juce::Colours::white);
        g.setFont (juce::FontOptions (8.5f, juce::Font::bold));
        g.drawText (grp.toUpperCase(), x0, 0, x1 - x0, titleH, juce::Justification::centred);
        c = c2 + 1;
    }

    for (int c = 0; c < nCols; ++c)
    {
        const auto& col = columns[static_cast<size_t> (c)];
        const int x  = colX[static_cast<size_t> (c)];
        const int cw = colWid[static_cast<size_t> (c)];

        if (! col.palette)   // category name (palette groups drawn above)
        {
            g.setColour (juce::Colours::white);
            g.setFont (juce::FontOptions (9.5f, juce::Font::bold));
            g.drawFittedText (col.title.toUpperCase(), x + 1, 1, cw - 2, titleH - 2,
                              juce::Justification::centred, 2);
        }

        // Octave number band — the column's octave, no "C" prefix.
        g.setColour (juce::Colour (0xffb0b0b0));
        g.setFont (juce::FontOptions (9.0f, juce::Font::bold));
        g.drawText (juce::String (col.octave), x, titleH, cw, kOctaveH,
                    juce::Justification::centred);
    }

    // ---- Cells ----------------------------------------------------------
    for (int c = 0; c < nCols; ++c)
    {
        const auto& col = columns[static_cast<size_t> (c)];
        const int x  = colX[static_cast<size_t> (c)];
        const int cw = colWid[static_cast<size_t> (c)];

        for (int offset = 0; offset < kNumRows; ++offset)
        {
            const auto& cell = col.cells[static_cast<size_t> (offset)];
            if (! cell.present)
                continue;   // empty slot — striped background shows through

            const int screenRow = (kNumRows - 1) - offset;
            const int y = kHeaderH + screenRow * rowH;
            const auto cellR = juce::Rectangle<int> (x, y, cw, rowH);
            const bool on = isActive (cell.pitch);

            if (cell.swatch)
            {
                // Colour chips are squares (note name overlaid for reference).
                const auto r = cellR.reduced (kCellPad);
                const int sq = juce::jmin (r.getWidth(), r.getHeight());
                const auto sqRect = juce::Rectangle<int> (0, 0, sq, sq)
                                        .withCentre (r.getCentre());
                g.setColour (cell.colour);
                g.fillRect (sqRect);
                g.setColour (on ? juce::Colours::white : juce::Colour (0x40ffffff));
                g.drawRect (sqRect, on ? 2 : 1);

                const bool light = cell.colour.getPerceivedBrightness() > 0.5f;
                g.setColour (light ? juce::Colours::black.withAlpha (0.65f)
                                   : juce::Colours::white.withAlpha (0.8f));
                g.setFont (juce::FontOptions (6.5f));
                g.drawText (cell.label, sqRect, juce::Justification::centred);
            }
            else
            {
                // Trigger tiles are ~20% narrower than their column so the
                // piano-roll striping shows through the gaps. No per-tile note
                // — the gutter letter + column octave already give it.
                const int hInset = juce::roundToInt (static_cast<float> (cw) * 0.1f);
                const auto tile = cellR.reduced (hInset, kCellPad);
                g.setColour (on ? juce::Colour (0xff3a6ea5) : juce::Colour (0xff333333));
                g.fillRoundedRectangle (tile.toFloat(), 3.0f);
                g.setColour (on ? juce::Colours::white : juce::Colour (0xff9fbedd));
                g.setFont (juce::FontOptions (8.5f));
                g.drawFittedText (cell.label, tile.reduced (4, 0),
                                  juce::Justification::centredLeft, 1, 0.78f);
            }
        }
    }
}

}  // namespace hitnotedmx
