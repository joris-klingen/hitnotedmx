#include "PluginEditor.h"

namespace hitnotedmx
{

namespace
{
juce::String midiNoteName (juce::uint8 pitch)
{
    return juce::MidiMessage::getMidiNoteName (pitch,
                                               true /* sharps */,
                                               true /* with octave */,
                                               3    /* MIDI 60 = C3 (Live convention) */);
}
}

HitNoteDmxAudioProcessorEditor::HitNoteDmxAudioProcessorEditor (HitNoteDmxAudioProcessor& p)
    : AudioProcessorEditor (&p), proc (p), dmxView (p.getDmxValues())
{
    setSize (720, 440);

    addAndMakeVisible (connectUsbButton);
    addAndMakeVisible (disconnectButton);
    addAndMakeVisible (blackoutButton);
    connectUsbButton.addListener (this);
    disconnectButton.addListener (this);
    blackoutButton.addListener (this);
    blackoutButton.setClickingTogglesState (true);

    deviceStatusLabel.setJustificationType (juce::Justification::centredLeft);
    deviceStatusLabel.setColour (juce::Label::textColourId, juce::Colours::white);
    addAndMakeVisible (deviceStatusLabel);

    addAndMakeVisible (dmxView);

    midiLogView.setMultiLine (true, false);
    midiLogView.setReadOnly (true);
    midiLogView.setCaretVisible (false);
    midiLogView.setScrollbarsShown (true);
    midiLogView.setFont (juce::Font (juce::FontOptions ("Menlo", 12.0f, juce::Font::plain)));
    midiLogView.setColour (juce::TextEditor::backgroundColourId, juce::Colours::black);
    midiLogView.setColour (juce::TextEditor::textColourId,       juce::Colours::lightgreen);
    addAndMakeVisible (midiLogView);

    refreshDeviceStatus();
    startTimerHz (15);  // visual confirmation only; combined with the
                        // visualiser's fingerprint diff, the actual
                        // GUI work is bounded by how often the rig's
                        // 120 quantised channels change byte values.
}

HitNoteDmxAudioProcessorEditor::~HitNoteDmxAudioProcessorEditor()
{
    stopTimer();
    connectUsbButton.removeListener (this);
    disconnectButton.removeListener (this);
    blackoutButton.removeListener (this);
}

void HitNoteDmxAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff202020));
    g.setColour (juce::Colours::white);
    g.setFont (juce::FontOptions (16.0f, juce::Font::bold));
    g.drawText ("HitNoteDmx", 12, 8, getWidth() - 24, 24,
                juce::Justification::centredLeft);
}

void HitNoteDmxAudioProcessorEditor::resized()
{
    auto area = getLocalBounds().reduced (12);
    area.removeFromTop (28);  // title bar painted in paint()

    auto buttonRow = area.removeFromTop (32);
    auto buttonWidth = (buttonRow.getWidth() - 16) / 3;
    connectUsbButton.setBounds (buttonRow.removeFromLeft (buttonWidth));
    buttonRow.removeFromLeft (8);
    disconnectButton.setBounds (buttonRow.removeFromLeft (buttonWidth));
    buttonRow.removeFromLeft (8);
    blackoutButton.setBounds (buttonRow.removeFromLeft (buttonWidth));

    area.removeFromTop (8);
    deviceStatusLabel.setBounds (area.removeFromTop (22));
    area.removeFromTop (8);

    // Bottom half = MIDI log (compact). Top half = the DMX visualiser.
    auto logArea = area.removeFromBottom (90);
    dmxView.setBounds (area);
    area.removeFromBottom (8);
    midiLogView.setBounds (logArea);
}

void HitNoteDmxAudioProcessorEditor::buttonClicked (juce::Button* b)
{
    if (b == &connectUsbButton)
    {
        connectAttempt = true;
        const bool ok = proc.getDmx().connect();
        if (! ok)
            appendLog ("[usb] connect failed");
        else
            appendLog ("[usb] connected");
        refreshDeviceStatus();
    }
    else if (b == &disconnectButton)
    {
        proc.getDmx().disconnect();
        appendLog ("[usb] disconnected");
        refreshDeviceStatus();
    }
    else if (b == &blackoutButton)
    {
        const bool on = blackoutButton.getToggleState();
        proc.blackout = on;
        proc.getDmx().setBlackout (on);
        appendLog (on ? "[blackout] ON" : "[blackout] OFF");
    }
}

void HitNoteDmxAudioProcessorEditor::timerCallback()
{
    // Drain whatever the audio thread left for us. Cap iterations so a
    // burst of MIDI doesn't lock the timer thread. appendLog() only
    // mutates the in-memory backing buffer; the TextEditor is touched
    // once at the end of the tick by flushLogIfDirty().
    auto& log = proc.getMidiLog();
    int drained = 0;
    while (drained < 64)
    {
        const auto entry = log.pop();
        if (! entry)
            break;
        const auto kind = entry->kind == MidiLogEntry::Kind::NoteOn ? "on " : "off";
        // Include the raw MIDI pitch number alongside the note name so
        // the palette mapping (pitch − 36 → palette[…]) is easy to verify.
        appendLog (juce::String::formatted ("[%s] ch%-2d %-4s (%3d) vel=%d",
                                            kind,
                                            entry->channel,
                                            midiNoteName (entry->pitch).toRawUTF8(),
                                            (int) entry->pitch,
                                            entry->velocity));
        ++drained;
    }
    flushLogIfDirty();

    // Refresh the live DMX preview only when something actually changed.
    dmxView.repaintIfChanged();
}

void HitNoteDmxAudioProcessorEditor::refreshDeviceStatus()
{
    deviceStatusLabel.setText (proc.getDmx().getStatusText(),
                               juce::dontSendNotification);
}

void HitNoteDmxAudioProcessorEditor::appendLog (const juce::String& line)
{
    // O(1) backing-store append. The TextEditor is NOT touched here —
    // doing setText() per MIDI event used to retrigger a full
    // TextEditor relayout, which scaled with text length and made the
    // GUI visibly glitchy after a few loop iterations once chases were
    // firing notes.
    logBuffer += line;
    logBuffer += '\n';
    logDirty = true;
}

void HitNoteDmxAudioProcessorEditor::flushLogIfDirty()
{
    if (! logDirty)
        return;

    // Bound the buffer so the editor's relayout cost stays roughly
    // constant. Trim only when we've grown past the cap; drop entire
    // leading lines, not arbitrary character ranges.
    constexpr int kSoftCap = 8000;   // ~200 lines × ~40 chars
    constexpr int kHardCap = 12000;  // grow window before another trim
    if (logBuffer.length() > kHardCap)
    {
        const int chop = logBuffer.length() - kSoftCap;
        const int nl   = logBuffer.indexOfChar (chop, '\n');
        if (nl > 0)
            logBuffer = logBuffer.substring (nl + 1);
    }

    midiLogView.setText (logBuffer, false);
    midiLogView.moveCaretToEnd();
    logDirty = false;
}

}  // namespace hitnotedmx
