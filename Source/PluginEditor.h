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

    void refreshDeviceStatus();
    void appendLog (const juce::String& line);  // queue; doesn't touch the editor
    void flushLogIfDirty();                     // one setText per tick

    HitNoteDmxAudioProcessor& proc;

    juce::TextButton connectUsbButton  { "Connect USB" };
    juce::TextButton blackoutButton    { "Blackout" };
    juce::TextButton initNamesButton   { "Init. names" };   // install the named rack
    juce::TextButton showClipsButton   { "Show clips" };    // open the demo-clips folder
    juce::Label      deviceStatusLabel;
    juce::TextEditor midiLogView;
    DmxVisualizer    dmxView;

    // Right pane: clickable trigger reference (toggle + combine).
    TriggerMenu triggerMenu;

    // Far-right utility pane: click-velocity slider (top) + a placeholder for
    // the upcoming MIDI-drag options (bottom).
    juce::Slider clickVelSlider { juce::Slider::LinearVertical, juce::Slider::TextBoxBelow };
    juce::Label  clickVelLabel;
    juce::Label  dragPlaceholder;

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

    bool connectAttempt = false;

    // Pane card backgrounds, set in resized(), painted in paint().
    // titleArea is the strip above the visualiser carrying the app title.
    juce::Rectangle<int> leftPaneArea, midPaneArea, rightPaneArea, extraPaneArea, titleArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HitNoteDmxAudioProcessorEditor)
};

}  // namespace hitnotedmx
