#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Rig.h"

namespace hitnotedmx
{

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
    return layout;
}

HitNoteDmxAudioProcessor::HitNoteDmxAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      parameters (*this, nullptr, "DmxUniverse", createParameterLayout())
{
    for (int i = 1; i <= kDmxUniverseSize; ++i)
        parameters.addParameterListener (paramIdForChannel (i), this);
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
}

void HitNoteDmxAudioProcessor::releaseResources() {}

bool HitNoteDmxAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    // Same rule as HitDmx: mono or stereo on the main bus, input matches
    // output. We don't process the audio at all but we accept these
    // layouts so the host can place the plugin on an audio-style chain
    // (which is how DMXIS-style "drives DMX from MIDI" plugins live).
    const auto& mainOut = layouts.getMainOutputChannelSet();
    if (mainOut != juce::AudioChannelSet::mono() && mainOut != juce::AudioChannelSet::stereo())
        return false;
    return mainOut == layouts.getMainInputChannelSet();
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
    //    Hosts that aren't a DAW (or DAWs with transport stopped) may
    //    not supply BPM or PPQ; fall back to sensible defaults so static
    //    layers (bar selectors, pixel statics, spot triggers) still work.
    double blockStartBeat = 0.0;
    double bpm = 120.0;
    if (auto* ph = getPlayHead())
    {
        if (auto pos = ph->getPosition())
        {
            if (auto p = pos->getPpqPosition()) blockStartBeat = *p;
            if (auto b = pos->getBpm())         bpm = *b;
        }
    }
    const double beatsPerSample = (bpm / 60.0) / sampleRate_;

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

    // 3. Compose once per block using the playhead time at block start.
    //    Block durations are typically 5-10 ms so per-event time skew
    //    inside one block is negligible for the recipes' periodic shapes,
    //    and computing once keeps work bounded.
    computeDmx (midiState, blockStartBeat, dmxValues);

    // 4. Push every rig channel out to the ENTTEC widget. The driver
    //    holds a CriticalSection internally; setChannel is the same
    //    routine HitDmx calls from its parameter-change listener.
    const auto* const v = dmxValues.raw();
    for (int ch1 = 1; ch1 <= kRigChannels; ++ch1)
    {
        const float f = v[ch1 - 1];
        const int byte = juce::jlimit (0, 255, static_cast<int> (f * 255.0f + 0.5f));
        dmx.setChannel (ch1, static_cast<juce::uint8> (byte));
    }
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
