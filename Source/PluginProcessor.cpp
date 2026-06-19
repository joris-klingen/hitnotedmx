#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Palette.h"
#include "Recipes.h"
#include "Rig.h"

namespace hitnotedmx
{

namespace
{
}

juce::String HitNoteDmxAudioProcessor::paramIdForChannel (int channel1to512)
{
    return "ch" + juce::String (channel1to512);
}

juce::AudioProcessorValueTreeState::ParameterLayout
HitNoteDmxAudioProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    for (int i = 1; i <= kDmxUniverseSize; ++i)
    {
        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID (paramIdForChannel (i), 1),
            "Channel " + juce::String (i),
            juce::NormalisableRange<float> (0.0f, 255.0f, 1.0f),
            0.0f));
    }

    // Master dims — automatable so a host/MIDI-mapped knob can ride them
    // live. 0..1, default 1.0 (no attenuation). These are NOT "chN"
    // channel params, so parameterChanged() ignores them; processBlock
    // polls them each block and feeds them into computeDmx(). The
    // value<->text functions format as a percentage in both the host and
    // the attached on-screen knob.
    const auto pctAttributes = juce::AudioParameterFloatAttributes()
        .withStringFromValueFunction ([] (float v, int) {
            return juce::String (juce::roundToInt (v * 100.0f)) + "%"; })
        .withValueFromStringFunction ([] (const juce::String& t) {
            return t.removeCharacters ("% ").getFloatValue() / 100.0f; });

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID (kLedMasterDimId, 1),
        "LED Master Dim",
        juce::NormalisableRange<float> (0.0f, 1.0f), 1.0f, pctAttributes));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID (kSpotMasterDimId, 1),
        "Spot Master Dim",
        juce::NormalisableRange<float> (0.0f, 1.0f), 1.0f, pctAttributes));
    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID (kPixelDensityId, 1),
        "Pixel Density",
        juce::NormalisableRange<float> (0.0f, 1.0f), 1.0f, pctAttributes));

    return layout;
}

HitNoteDmxAudioProcessor::HitNoteDmxAudioProcessor()
    // Instrument shape: an audio OUTPUT bus only (we emit silence). No input
    // bus — the plugin is driven entirely by MIDI, and an instrument with an
    // audio input is unusual and confuses some hosts' track routing.
    : AudioProcessor (BusesProperties()
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      parameters (*this, nullptr, "DmxUniverse", createParameterLayout())
{
    for (int i = 1; i <= kDmxUniverseSize; ++i)
        parameters.addParameterListener (paramIdForChannel (i), this);

    ledMasterDimParam  = parameters.getRawParameterValue (kLedMasterDimId);
    spotMasterDimParam = parameters.getRawParameterValue (kSpotMasterDimId);
    pixelDensityParam  = parameters.getRawParameterValue (kPixelDensityId);

    for (auto& p : previewPitch)
        p.store (-1);
    appliedPreview.fill (-1);
}

HitNoteDmxAudioProcessor::~HitNoteDmxAudioProcessor()
{
    for (int i = 1; i <= kDmxUniverseSize; ++i)
        parameters.removeParameterListener (paramIdForChannel (i), this);

    dmx.disconnect();
}

void HitNoteDmxAudioProcessor::prepareToPlay (double sampleRate, int /*samplesPerBlock*/)
{
    sampleRate_ = sampleRate > 0 ? sampleRate : 48000.0;
    midiState.clear();
    colorFade.reset();
    freeRunBeats = 0.0;
}

void HitNoteDmxAudioProcessor::releaseResources() {}

bool HitNoteDmxAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    // Instrument: a single mono/stereo OUTPUT bus, no input. We emit silence —
    // the audio bus exists only so the host loads us as an instrument on a
    // MIDI track; the real output is DMX over USB.
    const auto& mainOut = layouts.getMainOutputChannelSet();
    return mainOut == juce::AudioChannelSet::mono()
        || mainOut == juce::AudioChannelSet::stereo();
}

void HitNoteDmxAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                             juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();

    // 1. Get the host's musical timeline so we can stamp incoming notes
    //    and feed the composition with a beat-time `t`.
    //
    //    JUCE 8's AudioPlayHead::getPosition() returns Optional<PositionInfo>.
    //    When the host transport is *playing* we follow its PPQ so recipes
    //    stay locked to the song. Otherwise (standalone, or transport
    //    stopped) we advance a free-running beat clock so chases/breathes
    //    still animate — essential for previewing without a DAW rolling.
    double bpm = 120.0;
    bool   playing = false;
    double hostPpq = 0.0;
    bool   haveHostPpq = false;
    if (auto* ph = getPlayHead())
    {
        if (auto pos = ph->getPosition())
        {
            playing = pos->getIsPlaying();
            if (auto p = pos->getPpqPosition()) { hostPpq = *p; haveHostPpq = true; }
            if (auto b = pos->getBpm())         { bpm = *b; }
        }
    }
    const double beatsPerSample = (bpm / 60.0) / sampleRate_;
    const double dtBeats = beatsPerSample * static_cast<double> (buffer.getNumSamples());

    double blockStartBeat;
    if (playing && haveHostPpq)
    {
        // Locked to the host; keep the free-run clock in step so it
        // continues smoothly from here when the transport stops.
        blockStartBeat = hostPpq;
        freeRunBeats   = hostPpq;
    }
    else
    {
        blockStartBeat = freeRunBeats;
        freeRunBeats  += dtBeats;
    }

    // 2. Update MidiState (and the GUI's MidiLog) from every event the
    //    host gave us this block.
    for (const auto meta : midi)
    {
        const auto msg = meta.getMessage();
        const double eventBeat = blockStartBeat
                               + static_cast<double> (meta.samplePosition) * beatsPerSample;

        if (msg.isNoteOn())
        {
            midiState.noteOn (static_cast<std::uint8_t> (msg.getNoteNumber()),
                              static_cast<std::uint8_t> (msg.getChannel()),
                              static_cast<std::uint8_t> (msg.getVelocity()),
                              eventBeat);
            midiLog.push ({ MidiLogEntry::Kind::NoteOn,
                            static_cast<juce::uint8> (msg.getChannel()),
                            static_cast<juce::uint8> (msg.getNoteNumber()),
                            static_cast<juce::uint8> (msg.getVelocity()),
                            static_cast<double> (meta.samplePosition) });
        }
        else if (msg.isNoteOff())
        {
            midiState.noteOff (static_cast<std::uint8_t> (msg.getNoteNumber()));
            midiLog.push ({ MidiLogEntry::Kind::NoteOff,
                            static_cast<juce::uint8> (msg.getChannel()),
                            static_cast<juce::uint8> (msg.getNoteNumber()),
                            static_cast<juce::uint8> (msg.getVelocity()),
                            static_cast<double> (meta.samplePosition) });
        }
    }

    // 2b. Fold in any live preview the GUI requested (held until cleared).
    applyPreview (blockStartBeat);

    // 3. Compose once per block using the playhead time at block start.
    //    Block durations are typically 5-10 ms so per-event time skew
    //    inside one block is negligible for the recipes' periodic shapes,
    //    and computing once keeps work bounded.
    const float ledDim  = ledMasterDimParam  != nullptr ? ledMasterDimParam->load()  : 1.0f;
    const float spotDim = spotMasterDimParam != nullptr ? spotMasterDimParam->load() : 1.0f;
    const float density = pixelDensityParam  != nullptr ? pixelDensityParam->load()  : 1.0f;
    const double dtSeconds = static_cast<double> (buffer.getNumSamples()) / sampleRate_;
    computeDmx (midiState, blockStartBeat, dmxValues, ledDim, spotDim, &colorFade, dtSeconds, density, &selection);

    // Strobe is a global shutter applied in the DMX driver's send loop (so
    // it stays locked to the output clock and free of audio-block jitter).
    // Velocity sets the repeat rate (1..20 Hz); the driver keeps the flash a
    // single send frame and only stretches the black gap. 0 Hz = off.
    const auto strobePitch = static_cast<std::uint8_t> (kStrobePitch);
    float strobeRate = 0.0f;
    if (midiState.isActive (strobePitch))
    {
        const float vel = static_cast<float> (midiState.get (strobePitch).velocity) / 127.0f;
        strobeRate = static_cast<float> (kStrobeMinHz + (kStrobeMaxHz - kStrobeMinHz) * vel);
    }
    dmx.setStrobeHz (strobeRate);

    // 4. Push every rig channel out to the ENTTEC widget. The driver
    //    holds a CriticalSection internally.
    const auto* const v = dmxValues.raw();
    for (int ch1 = 1; ch1 <= kRigChannels; ++ch1)
    {
        const float f = v[ch1 - 1];
        const int byte = juce::jlimit (0, 255, static_cast<int> (f * 255.0f + 0.5f));
        dmx.setChannel (ch1, static_cast<juce::uint8> (byte));
    }
}

void HitNoteDmxAudioProcessor::getHeldPitches (std::vector<int>& out) const
{
    out.clear();
    for (int p = 0; p < MidiState::kNumPitches; ++p)
        if (midiState.isActive (static_cast<std::uint8_t> (p)))
            out.push_back (p);
}

juce::AudioProcessorEditor* HitNoteDmxAudioProcessor::createEditor()
{
    return new HitNoteDmxAudioProcessorEditor (*this);
}

void HitNoteDmxAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = parameters.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void HitNoteDmxAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (parameters.state.getType()))
            parameters.replaceState (juce::ValueTree::fromXml (*xml));
}

void HitNoteDmxAudioProcessor::setPreviewPitches (const std::vector<int>& pitches)
{
    // Just hold exactly the latched pitches. When a structural/recipe trigger
    // is held with no palette colour, computeDmx defaults to full white — so
    // clicking a tile and playing the same note over MIDI behave identically.
    std::array<int, kMaxPreview> next;
    next.fill (-1);
    int n = 0;

    for (int p : pitches)
        if (n < kMaxPreview) next[static_cast<size_t> (n++)] = p;

    for (int i = 0; i < kMaxPreview; ++i)
        previewPitch[static_cast<size_t> (i)].store (next[static_cast<size_t> (i)]);
}

void HitNoteDmxAudioProcessor::applyPreview (double atBeat) noexcept
{
    std::array<int, kMaxPreview> want;
    for (int i = 0; i < kMaxPreview; ++i)
        want[static_cast<size_t> (i)] = previewPitch[static_cast<size_t> (i)].load();

    auto contains = [] (const std::array<int, kMaxPreview>& set, int v)
    {
        if (v < 0) return false;
        for (int s : set) if (s == v) return true;
        return false;
    };

    const auto vel = static_cast<std::uint8_t> (juce::jlimit (1, 127, previewVelocity.load()));

    // If the click velocity changed, drop everything currently held so it
    // re-triggers at the new velocity — lets the slider audition live.
    if (static_cast<int> (vel) != appliedPreviewVel)
    {
        for (int a : appliedPreview)
            if (a >= 0)
                midiState.noteOff (static_cast<std::uint8_t> (a));
        appliedPreview.fill (-1);
        appliedPreviewVel = vel;
    }

    // Release previously-held preview notes no longer wanted.
    for (int a : appliedPreview)
        if (a >= 0 && ! contains (want, a))
            midiState.noteOff (static_cast<std::uint8_t> (a));

    // Hold newly-requested preview notes at the editor-controlled velocity.
    for (int wv : want)
        if (wv >= 0 && ! contains (appliedPreview, wv))
            midiState.noteOn (static_cast<std::uint8_t> (wv), 1, vel, atBeat);

    appliedPreview = want;
}

void HitNoteDmxAudioProcessor::parameterChanged (const juce::String& parameterID, float newValue)
{
    if (! parameterID.startsWith ("ch"))
        return;
    const int channel = parameterID.substring (2).getIntValue();
    dmx.setChannel (channel, (juce::uint8) juce::jlimit (0, 255, (int) newValue));
}

}  // namespace hitnotedmx

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new hitnotedmx::HitNoteDmxAudioProcessor();
}
