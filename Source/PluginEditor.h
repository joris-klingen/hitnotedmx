#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "DmxVisualizer.h"
#include "PluginProcessor.h"
#include "TriggerMenu.h"

namespace hitnotedmx
{

// Custom rotary look for the master-dim knobs: a dark body with a
// coloured value arc and a white pointer. The fill colour is read per
// slider from rotarySliderFillColourId, so each knob can be tinted
// differently (LEDs vs spots).
class DimKnobLookAndFeel : public juce::LookAndFeel_V4
{
public:
    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height,
                           float sliderPos, float rotaryStartAngle,
                           float rotaryEndAngle, juce::Slider&) override;
};

// Master-grid tiles, styled to match the trigger-menu cells: a rounded tile
// (blue when latched, grey idle) with the trigger TITLE left-aligned and its
// MIDI NOTE name (stashed in the "note" property, e.g. "D#-8") right-aligned.
class MasterTileLookAndFeel : public juce::LookAndFeel_V4
{
public:
    void drawButtonBackground (juce::Graphics& g, juce::Button& b,
                               const juce::Colour&, bool over, bool down) override
    {
        const bool active = b.getToggleState() || down;   // latched OR pressed (momentary)
        auto c = ! b.isEnabled() ? juce::Colour (0xff262626)
                 : active        ? juce::Colour (0xff3a6ea5)
                                 : juce::Colour (0xff333333);
        if (b.isEnabled() && over && ! active) c = c.brighter (0.08f);
        g.setColour (c);
        g.fillRoundedRectangle (b.getLocalBounds().toFloat().reduced (0.5f), 3.0f);
    }

    void drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool down) override
    {
        const bool on = b.getToggleState() || down;
        const float a  = b.isEnabled() ? 1.0f : 0.4f;
        auto r = b.getLocalBounds().reduced (4, 0);

        g.setFont (juce::FontOptions (8.5f));
        const auto note = b.getProperties()["note"].toString();
        if (note.isNotEmpty())
        {
            auto noteArea = r.removeFromRight (24);   // reserve the right slice
            g.setColour ((on ? juce::Colour (0xffd7e3f0) : juce::Colour (0xff7790a6))
                           .withMultipliedAlpha (a));
            g.drawText (note, noteArea, juce::Justification::centredRight);
        }
        g.setColour ((on ? juce::Colours::white : juce::Colour (0xff9fbedd)).withMultipliedAlpha (a));
        g.drawFittedText (b.getButtonText(), r, juce::Justification::centredLeft, 1, 0.7f);
    }
};

// Draws a folder glyph centred on a button instead of its text — for the
// narrow far-right "Show clips" button, where the label gets truncated to
// nonsense. The button's tooltip still carries the meaning.
class FolderIconLookAndFeel : public juce::LookAndFeel_V4
{
public:
    void drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool) override
    {
        const auto area = b.getLocalBounds().toFloat();
        const float w = juce::jmin (area.getWidth(), area.getHeight()) * 0.6f;
        const float h = w * 0.8f;
        const float x = area.getCentreX() - w * 0.5f;
        const float y = area.getCentreY() - h * 0.5f;
        const float tabW = w * 0.42f;
        const float tabH = h * 0.28f;

        g.setColour (b.findColour (juce::TextButton::textColourOffId)
                       .withMultipliedAlpha (b.isEnabled() ? 1.0f : 0.4f));
        g.fillRoundedRectangle (x, y, tabW, tabH + 2.0f, 1.5f);   // top-left tab
        g.fillRoundedRectangle (x, y + tabH, w, h - tabH, 2.0f);  // folder body
    }
};

// A master-grid tile that can be momentary: when `momentary`, it fires
// onMomentary(true) on press and onMomentary(false) on release (so a bump is
// held while the mouse is down and decays on release), instead of latching.
class MasterTileButton : public juce::TextButton
{
public:
    bool momentary = false;
    std::function<void (bool down)> onMomentary;

    void mouseDown (const juce::MouseEvent& e) override
    {
        juce::TextButton::mouseDown (e);
        if (momentary && isEnabled() && onMomentary) onMomentary (true);
    }
    void mouseUp (const juce::MouseEvent& e) override
    {
        juce::TextButton::mouseUp (e);
        if (momentary && isEnabled() && onMomentary) onMomentary (false);
    }
};

// Far-right "drag MIDI" tile. Latch a trigger set in the menu, then drag this
// out to Finder / Ableton to drop a one-bar .mid holding the selected notes —
// the same held chord the live preview plays. Inert while nothing is latched.
class MidiDragTile : public juce::Component,
                     public juce::SettableTooltipClient
{
public:
    MidiDragTile();

    // Supplies the currently-latched pitches (read at drag/paint time).
    std::function<std::vector<int>()> getPitches;

    void paint (juce::Graphics&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseEnter (const juce::MouseEvent&) override { repaint(); }
    void mouseExit  (const juce::MouseEvent&) override { repaint(); }

private:
    bool dragging { false };   // guards re-entry while a drag loop is running
};

// A tiny per-bar LED-brightness cell (1..10, 10 = full). Drag vertically like a
// fader to change; double-click to type a number. Shows the integer over a fill
// that tracks the value. The editor maps 1..10 → a 0.1..1.0 relative dim.
class BarDimBox : public juce::Label
{
public:
    BarDimBox()
    {
        setEditable (false, true, false);   // double-click opens a type-in editor
        setJustificationType (juce::Justification::centred);
        // Opaque editor so the fader fill / number don't show through behind it.
        setColour (juce::Label::backgroundWhenEditingColourId, juce::Colour (0xff000000));
        setColour (juce::Label::textWhenEditingColourId,       juce::Colours::white);
        setColour (juce::Label::outlineWhenEditingColourId,    juce::Colour (0xff39c6c0));
        onTextChange = [this] { setValue (getText().getIntValue(), true); };
        setValue (10);
    }

    std::function<void (int)> onChange;   // fired with 1..10 on a user edit

    void setValue (int v, bool notify = false)
    {
        v = juce::jlimit (1, 10, v);
        const bool changed = v != value;
        value = v;
        setText (juce::String (value), juce::dontSendNotification);
        repaint();
        if (changed && notify && onChange) onChange (value);
    }
    int getValue() const noexcept { return value; }

    void mouseDown (const juce::MouseEvent& e) override
    {
        juce::Label::mouseDown (e);
        dragStart = value;
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        setValue (dragStart - e.getDistanceFromDragStartY() / 7, true);   // up = brighter
    }

    void paint (juce::Graphics& g) override
    {
        if (isBeingEdited())   // the type-in editor draws itself; stay out of its way
            return;
        auto r = getLocalBounds().toFloat().reduced (1.0f);
        g.setColour (juce::Colour (0xff1e1e1e));
        g.fillRoundedRectangle (r, 3.0f);
        const float frac = static_cast<float> (value) / 10.0f;
        auto fill = r.withTrimmedTop (r.getHeight() * (1.0f - frac));
        g.setColour (juce::Colour (0xff39c6c0).withAlpha (0.28f + 0.34f * frac));
        g.fillRoundedRectangle (fill, 3.0f);
        g.setColour (juce::Colour (0xff3a3a3a));
        g.drawRoundedRectangle (r, 3.0f, 1.0f);
        g.setColour (juce::Colours::white);
        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        g.drawText (juce::String (value), getLocalBounds(), juce::Justification::centred);
    }

private:
    int value { 10 };
    int dragStart { 10 };
};

// Stripped-down editor for the skeleton commit. Shows:
//   - the ENTTEC USB Pro connect / disconnect / blackout buttons
//   - a scrolling text log of the most recent MIDI activity (drained
//     from the lock-free MidiLog every timer tick)
//
// A per-channel slider grid is omitted on purpose — once recipes drive
// the channels there are too many to monitor visually, and Live can
// browse the parameter list directly via "Configure" anyway.

class HitNoteDmxAudioProcessorEditor  : public juce::AudioProcessorEditor,
                                        private juce::Button::Listener,
                                        private juce::Timer
{
public:
    explicit HitNoteDmxAudioProcessorEditor (HitNoteDmxAudioProcessor&);
    ~HitNoteDmxAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void buttonClicked (juce::Button*) override;
    void timerCallback() override;

    void logDeviceStatus();                     // append the ENTTEC status to the log
    void updateConnectButton();                 // label + green tint track the connection
    void appendLog (const juce::String& line);  // queue; doesn't touch the editor
    void flushLogIfDirty();                     // one setText per tick

    HitNoteDmxAudioProcessor& proc;

    juce::TextButton connectUsbButton  { "Connect USB" };
    juce::TextButton blackoutButton    { "Blackout" };
    juce::TextButton initNamesButton   { "Init. names" };   // install the named rack
    juce::TextButton showClipsButton   { "Show clips" };    // open the demo-clips folder

    // Grid-shape section (cols × rows + "Set grid"), one small row above the
    // utility buttons. Applies via proc.setGridShape — a structural setting,
    // deliberately not a host parameter.
    juce::TextEditor gridColsEdit, gridRowsEdit;
    juce::Label      gridXLabel;
    juce::TextButton setGridButton { "Set grid" };
    void applyGridFromFields();
    void showGridFields (Rig r);   // mirror a shape into the two fields

    // Per-bar LED-dim row, one short row below the grid section: one 1..10 cell
    // per bar (4..8 shown, tracking the grid's column count). Relative dims,
    // rescaled by the master LED dim; "Set grid" resets them all to 10.
    std::array<BarDimBox, kMaxBars>  barDimBoxes;
    int                              shownBarCols { 0 };   // last laid-out bar count
    void refreshBarDimBoxes();   // pull values from proc, then relayout

    // Left-pane master-note grid (2 rows × 4 cols). The wired tiles latch the
    // "Master" notes (bump white / bump color / freeze) into the live preview —
    // the same notes as before, relocated out of the trigger menu; the rest are
    // placeholders for the planned speed / crossfade / extra master notes.
    static constexpr int kMasterCols  = 4;
    static constexpr int kMasterRows  = 2;
    static constexpr int kMasterTiles = kMasterCols * kMasterRows;
    std::array<MasterTileButton, kMasterTiles> masterTiles;
    MasterTileLookAndFeel masterTileLnf;
    void setMasterNote (int pitch, bool on);   // add/remove from the preview set
    FolderIconLookAndFeel folderLnf;   // icon for the narrow "Show clips" button
    juce::TextEditor midiLogView;
    DmxVisualizer    dmxView;

    // Right pane: clickable trigger reference (toggle + combine).
    TriggerMenu triggerMenu;

    // Far-right utility pane: click-velocity slider (top) + a placeholder for
    // the upcoming MIDI-drag options (bottom).
    juce::Slider clickVelSlider { juce::Slider::LinearVertical, juce::Slider::TextBoxBelow };
    juce::Label  clickVelLabel;
    MidiDragTile midiDragTile;
    std::vector<int> latchedPitches;   // combined latched set (menu + master), for the drag
    std::vector<int> menuLatched;      // latched from the trigger menu
    std::vector<int> masterLatched;    // latched from the left-pane master tiles
    void pushPreview();                // push menuLatched ∪ masterLatched to the preview

    // Master-dim knobs, attached to the host-automatable parameters so
    // the on-screen control, host automation, and any MIDI-mapped knob
    // all stay in sync.
    juce::Slider ledDimSlider  { juce::Slider::RotaryHorizontalVerticalDrag,
                                 juce::Slider::TextBoxBelow };
    juce::Slider spotDimSlider { juce::Slider::RotaryHorizontalVerticalDrag,
                                 juce::Slider::TextBoxBelow };
    juce::Slider densitySlider { juce::Slider::RotaryHorizontalVerticalDrag,
                                 juce::Slider::TextBoxBelow };
    juce::Label  ledDimLabel;
    juce::Label  spotDimLabel;
    juce::Label  densityLabel;
    DimKnobLookAndFeel knobLnf;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> ledDimAttach;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> spotDimAttach;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> densityAttach;

    // Backing store for the log text. setText() on the editor is the
    // expensive operation (full relayout); we batch into this buffer
    // and flush at most once per timer tick.
    juce::String logBuffer;
    bool         logDirty { false };

    std::vector<int> heldScratch;   // reused each tick for the live-MIDI highlight

    bool lastLinkUp = false;   // tracks DMX link state to log drops / auto-recoveries

    // Pane card backgrounds, set in resized(), painted in paint().
    // titleArea is the strip above the visualiser carrying the app title.
    juce::Rectangle<int> leftPaneArea, midPaneArea, rightPaneArea, extraPaneArea, titleArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HitNoteDmxAudioProcessorEditor)
};

}  // namespace hitnotedmx
