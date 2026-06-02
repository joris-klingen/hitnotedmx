#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "Composition.h"
#include "EnttecProDmx.h"
#include "MidiLog.h"
#include "MidiState.h"

namespace hitnotedmx
{

// The MIDI-driven sibling of HitDmx. Accepts MIDI input, exposes the
// same 512 host-automatable DMX-channel parameters that HitDmx does
// (so manual control + Live automation still work), and — in later
// commits — runs note-triggered chase/breathe/sparkle/etc. recipes
// that *write into those same parameters* so the GUI and Live see the
// computed values.
//
// In this initial skeleton commit, processBlock only logs the MIDI
// notes it receives into a lock-free MidiLog ring so the GUI can
// confirm we're getting MIDI from the host. No recipes yet.

class HitNoteDmxAudioProcessor  : public juce::AudioProcessor,
                                  private juce::AudioProcessorValueTreeState::Listener
{
public:
    HitNoteDmxAudioProcessor();
    ~HitNoteDmxAudioProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override                                   { return true; }

    const juce::String getName() const override                       { return JucePlugin_Name; }
    bool acceptsMidi()  const override                                { return true; }
    bool producesMidi() const override                                { return false; }
    bool isMidiEffect() const override                                { return false; }
    double getTailLengthSeconds() const override                      { return 0.0; }

    int getNumPrograms() override                                     { return 1; }
    int getCurrentProgram() override                                  { return 0; }
    void setCurrentProgram (int) override                             {}
    const juce::String getProgramName (int) override                  { return {}; }
    void changeProgramName (int, const juce::String&) override        {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState& getParameters() noexcept      { return parameters; }
    EnttecProDmx& getDmx() noexcept                                   { return dmx; }
    MidiLog& getMidiLog() noexcept                                    { return midiLog; }

    // Read-only view of the composition's per-channel output, refreshed
    // every audio block. Read from the GUI thread for the on-screen DMX
    // visualizer. Per-cell tearing under a concurrent audio-thread write
    // is acceptable for a 30 Hz visualisation.
    const DmxValues& getDmxValues() const noexcept                    { return dmxValues; }

    // UI state. Not stored in the parameter tree because they are not
    // host-automatable.
    bool blackout = false;

    static juce::String paramIdForChannel (int channel1to512);

    // Automatable master-dim parameter IDs (0..1). Exposed so the editor
    // can attach on-screen knobs to the same parameters the host sees.
    static constexpr const char* kLedMasterDimId  = "ledMasterDim";
    static constexpr const char* kSpotMasterDimId = "spotMasterDim";

private:
    void parameterChanged (const juce::String& parameterID, float newValue) override;

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    juce::AudioProcessorValueTreeState parameters;

    // Cached atomic views of the master-dim params, polled each block in
    // processBlock and fed to computeDmx. Owned by `parameters`.
    std::atomic<float>* ledMasterDimParam  { nullptr };
    std::atomic<float>* spotMasterDimParam { nullptr };

    EnttecProDmx dmx;
    MidiLog    midiLog;
    MidiState  midiState;
    DmxValues  dmxValues;
    ColorFadeState colorFade;  // persists colour-fade state across blocks

    double sampleRate_ { 48000.0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HitNoteDmxAudioProcessor)
};

}  // namespace hitnotedmx
