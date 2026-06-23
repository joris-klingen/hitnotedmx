#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <atomic>
#include <vector>

#include "Composition.h"
#include "EnttecProDmx.h"
#include "MidiLog.h"
#include "MidiState.h"

namespace hitnotedmx
{

// MIDI-driven DMX lighting controller. Accepts MIDI input and runs
// note-triggered chase/breathe/sparkle/etc. recipes whose per-block output is
// pushed to the ENTTEC driver and mirrored in the on-screen visualiser. The
// only host-automatable parameters are the three master controls (LED dim,
// spot dim, pixel density) — the per-channel DMX output is owned entirely by
// the recipe engine, so it is not exposed as automatable channel parameters.

class HitNoteDmxAudioProcessor  : public juce::AudioProcessor
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

    // Snapshot of which vocabulary pitches are currently held (live MIDI +
    // preview). Read from the GUI thread for the trigger-menu activity
    // display; a racy per-pitch bool read is fine for a ~15 Hz indicator.
    void getHeldPitches (std::vector<int>& out) const;

    // Read-only view of the composition's per-channel output, refreshed
    // every audio block. Read from the GUI thread for the on-screen DMX
    // visualizer. Per-cell tearing under a concurrent audio-thread write
    // is acceptable for a 30 Hz visualisation.
    const DmxValues& getDmxValues() const noexcept                    { return dmxValues; }

    // Parallel "armed but unlit" preview mask, written by computeDmx alongside
    // dmxValues and read by the visualiser to draw grey selection outlines.
    const SelectionMask& getSelection() const noexcept                { return selection; }

    // UI state. Not stored in the parameter tree because they are not
    // host-automatable.
    bool blackout = false;

    // Live preview (GUI thread → audio thread). Holds the given SET of
    // trigger pitches (toggle menu) until replaced. Injected into MidiState
    // each block via lock-free atomics. (A structural/recipe trigger with no
    // palette colour renders full white — see computeDmx.)
    void setPreviewPitches (const std::vector<int>& pitches);

    // Velocity used for previewed (clicked) triggers — driven by the editor's
    // click-velocity slider so the velocity-mapped behaviours (chase tail,
    // wild speed, breathe density, colour speed) can be auditioned by clicking.
    void setPreviewVelocity (int v) noexcept { previewVelocity.store (juce::jlimit (1, 127, v)); }
    int  getPreviewVelocity () const noexcept { return previewVelocity.load(); }

    // Automatable master-dim parameter IDs (0..1). Exposed so the editor
    // can attach on-screen knobs to the same parameters the host sees.
    static constexpr const char* kLedMasterDimId  = "ledMasterDim";
    static constexpr const char* kSpotMasterDimId = "spotMasterDim";
    // Dark-room pixel-density control. 0..1, default 1.0 = every LED lit;
    // lower thins a stable per-bar subset of pixels (see computeDmx).
    static constexpr const char* kPixelDensityId  = "pixelDensity";

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    // Apply the GUI's preview request to midiState (audio thread).
    void applyPreview (double atBeat) noexcept;

    juce::AudioProcessorValueTreeState parameters;

    // Cached atomic views of the master-dim params, polled each block in
    // processBlock and fed to computeDmx. Owned by `parameters`.
    std::atomic<float>* ledMasterDimParam  { nullptr };
    std::atomic<float>* spotMasterDimParam { nullptr };
    std::atomic<float>* pixelDensityParam  { nullptr };

    EnttecProDmx dmx;
    MidiLog    midiLog;
    MidiState  liveMidi;     // live MIDI input notes (written by the audio thread)
    MidiState  previewMidi;  // click-preview notes from the GUI (audio thread)
    MidiState  midiState;    // liveMidi ∪ previewMidi, rebuilt per block; read by computeDmx + GUI
    DmxValues  dmxValues;
    SelectionMask selection;   // "armed but unlit" cells, for the visualiser
    ColorFadeState colorFade;  // persists colour-fade state across blocks
    BumpState  bumpState;      // persists bump release-tail envelopes across blocks

    // Preview injection. previewPitch[] is written by the GUI thread and
    // read on the audio thread; -1 = empty slot. appliedPreview is
    // audio-thread-only bookkeeping so we noteOn/noteOff only on change.
    static constexpr int kMaxPreview = 48;
    std::array<std::atomic<int>, kMaxPreview> previewPitch;
    std::array<int, kMaxPreview>              appliedPreview;
    std::atomic<int>                          previewVelocity { 110 };
    int                                       appliedPreviewVel { 110 };  // audio-thread bookkeeping

    double sampleRate_ { 48000.0 };
    double freeRunBeats { 0.0 };  // beat clock used when transport isn't playing

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HitNoteDmxAudioProcessor)
};

}  // namespace hitnotedmx
