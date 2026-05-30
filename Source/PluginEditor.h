#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "DmxVisualizer.h"
#include "PluginProcessor.h"

namespace hitnotedmx
{

// Stripped-down editor for the skeleton commit. Shows:
//   - the ENTTEC USB Pro connect / disconnect / blackout buttons
//   - a scrolling text log of the most recent MIDI activity (drained
//     from the lock-free MidiLog every timer tick)
//
// The 512-slider per-channel grid that HitDmx ships with is omitted on
// purpose — once recipes start driving the channels there will be too
// many of them to monitor visually, and Live can browse the parameter
// list directly via "Configure" anyway.

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
    void appendLog (const juce::String& line);

    HitNoteDmxAudioProcessor& proc;

    juce::TextButton connectUsbButton  { "Connect USB" };
    juce::TextButton disconnectButton  { "Disconnect" };
    juce::TextButton blackoutButton    { "Blackout" };
    juce::Label      deviceStatusLabel;
    juce::TextEditor midiLogView;
    DmxVisualizer    dmxView;

    bool connectAttempt = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HitNoteDmxAudioProcessorEditor)
};

}  // namespace hitnotedmx
