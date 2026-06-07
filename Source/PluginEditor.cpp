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

void DimKnobLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width,
                                           int height, float sliderPos,
                                           float rotaryStartAngle, float rotaryEndAngle,
                                           juce::Slider& s)
{
    auto bounds = juce::Rectangle<float> (static_cast<float> (x), static_cast<float> (y),
                                          static_cast<float> (width), static_cast<float> (height))
                      .reduced (4.0f);
    const auto centre = bounds.getCentre();
    const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) / 2.0f;
    const float lineW  = juce::jmax (3.0f, radius * 0.18f);
    const float arcR   = radius - lineW * 0.5f;
    const float angle  = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);
    const auto  accent = s.findColour (juce::Slider::rotarySliderFillColourId);

    // Background track.
    juce::Path track;
    track.addCentredArc (centre.x, centre.y, arcR, arcR, 0.0f,
                         rotaryStartAngle, rotaryEndAngle, true);
    g.setColour (juce::Colour (0xff3a3a3a));
    g.strokePath (track, juce::PathStrokeType (lineW, juce::PathStrokeType::curved,
                                               juce::PathStrokeType::rounded));

    // Filled value arc.
    if (sliderPos > 0.0f)
    {
        juce::Path value;
        value.addCentredArc (centre.x, centre.y, arcR, arcR, 0.0f,
                             rotaryStartAngle, angle, true);
        g.setColour (accent);
        g.strokePath (value, juce::PathStrokeType (lineW, juce::PathStrokeType::curved,
                                                   juce::PathStrokeType::rounded));
    }

    // Knob body with a subtle radial shade.
    const float bodyR = radius - lineW - 2.0f;
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff343434), centre.x, centre.y - bodyR,
                                             juce::Colour (0xff202020), centre.x, centre.y + bodyR,
                                             false));
    g.fillEllipse (centre.x - bodyR, centre.y - bodyR, bodyR * 2.0f, bodyR * 2.0f);
    g.setColour (accent.withAlpha (0.5f));
    g.drawEllipse (centre.x - bodyR, centre.y - bodyR, bodyR * 2.0f, bodyR * 2.0f, 1.0f);

    // Pointer.
    juce::Path pointer;
    const float thick = juce::jmax (2.0f, bodyR * 0.14f);
    const float len   = bodyR * 0.82f;
    pointer.addRoundedRectangle (-thick * 0.5f, -len, thick, len * 0.62f, thick * 0.5f);
    pointer.applyTransform (juce::AffineTransform::rotation (angle).translated (centre.x, centre.y));
    g.setColour (juce::Colours::white);
    g.fillPath (pointer);
}

HitNoteDmxAudioProcessorEditor::HitNoteDmxAudioProcessorEditor (HitNoteDmxAudioProcessor& p)
    : AudioProcessorEditor (&p), proc (p), dmxView (p.getDmxValues())
{
    setSize (1024, 640);

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

    // Master-dim knobs (shown 0..100%), attached to the automatable params.
    auto setUpDimSlider = [this] (juce::Slider& s, juce::Label& lab,
                                  const juce::String& text, juce::Colour accent)
    {
        s.setLookAndFeel (&knobLnf);
        s.setColour (juce::Slider::rotarySliderFillColourId, accent);
        s.setColour (juce::Slider::textBoxTextColourId, juce::Colours::white);
        s.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 64, 18);

        // Display as a percentage rather than 0.00..1.00.
        s.textFromValueFunction = [] (double v)
            { return juce::String (juce::roundToInt (v * 100.0)) + "%"; };
        s.valueFromTextFunction = [] (const juce::String& t)
            { return t.getDoubleValue() / 100.0; };
        addAndMakeVisible (s);

        lab.setText (text, juce::dontSendNotification);
        lab.setJustificationType (juce::Justification::centred);
        lab.setColour (juce::Label::textColourId, juce::Colours::white);
        lab.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        addAndMakeVisible (lab);
    };
    setUpDimSlider (ledDimSlider,  ledDimLabel,  "LED DIM",  juce::Colour (0xff39c6c0));
    setUpDimSlider (spotDimSlider, spotDimLabel, "SPOT DIM", juce::Colour (0xfff2a93b));
    setUpDimSlider (densitySlider, densityLabel, "DENSITY",  juce::Colour (0xffb079d6));

    ledDimAttach = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        proc.getParameters(), HitNoteDmxAudioProcessor::kLedMasterDimId, ledDimSlider);
    spotDimAttach = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        proc.getParameters(), HitNoteDmxAudioProcessor::kSpotMasterDimId, spotDimSlider);
    densityAttach = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        proc.getParameters(), HitNoteDmxAudioProcessor::kPixelDensityId, densitySlider);

    // Right pane: clickable trigger reference. Cells toggle and combine;
    // the latched set drives the live preview.
    addAndMakeVisible (triggerMenu);
    triggerMenu.onSelectionChanged = [this] (const std::vector<int>& pitches)
        { proc.setPreviewPitches (pitches); };

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
                        // 228 quantised channels change byte values.
}

HitNoteDmxAudioProcessorEditor::~HitNoteDmxAudioProcessorEditor()
{
    stopTimer();
    connectUsbButton.removeListener (this);
    disconnectButton.removeListener (this);
    blackoutButton.removeListener (this);
    ledDimSlider.setLookAndFeel (nullptr);
    spotDimSlider.setLookAndFeel (nullptr);
    densitySlider.setLookAndFeel (nullptr);
}

void HitNoteDmxAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff181818));

    // App title.
    g.setColour (juce::Colours::white);
    g.setFont (juce::FontOptions (16.0f, juce::Font::bold));
    g.drawText ("HitNoteDmx", 12, 6, getWidth() - 24, 22,
                juce::Justification::centredLeft);

    // Pane cards.
    auto card = [&g] (juce::Rectangle<int> r, const juce::String& title)
    {
        if (r.isEmpty()) return;
        g.setColour (juce::Colour (0xff262626));
        g.fillRoundedRectangle (r.toFloat(), 6.0f);
        g.setColour (juce::Colour (0xff7a7a7a));
        g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
        g.drawText (title.toUpperCase(), r.getX() + 10, r.getY() + 6, r.getWidth() - 20, 12,
                    juce::Justification::centredLeft);
    };
    card (leftPaneArea,  "Controls");
    card (rightPaneArea, "Triggers - click to toggle / combine");
    // Middle pane is the opaque visualiser; no card needed behind it.
}

void HitNoteDmxAudioProcessorEditor::resized()
{
    auto area = getLocalBounds().reduced (12);
    area.removeFromTop (24);  // app-title bar painted in paint()

    const int gap = 12;
    leftPaneArea  = area.removeFromLeft (240);
    area.removeFromLeft (gap);
    rightPaneArea = area.removeFromRight (430);
    area.removeFromRight (gap);
    midPaneArea   = area;

    // Content inset inside the left card (leave room for the card title).
    auto leftContent = leftPaneArea.reduced (10).withTrimmedTop (14);

    // ---- LEFT pane: knobs (top) + MIDI log (middle) + utility buttons (bottom) ----
    {
        auto knobRow = leftContent.removeFromTop (108);
        const int colW = knobRow.getWidth() / 3;
        auto place = [] (juce::Rectangle<int> col, juce::Label& lab, juce::Slider& s)
        {
            lab.setBounds (col.removeFromTop (16));
            s.setBounds (col);
        };
        place (knobRow.removeFromLeft (colW), ledDimLabel,  ledDimSlider);
        place (knobRow.removeFromLeft (colW), spotDimLabel, spotDimSlider);
        place (knobRow,                       densityLabel, densitySlider);
        leftContent.removeFromTop (8);

        // Utility controls pinned to the bottom: status line + the three
        // big buttons. The MIDI log takes whatever is left in between.
        auto controls = leftContent.removeFromBottom (150);
        connectUsbButton.setBounds (controls.removeFromTop (32));
        controls.removeFromTop (6);
        disconnectButton.setBounds (controls.removeFromTop (32));
        controls.removeFromTop (6);
        blackoutButton.setBounds (controls.removeFromTop (32));
        controls.removeFromTop (6);
        deviceStatusLabel.setBounds (controls);  // remaining (~36px, wraps to 2 lines)

        leftContent.removeFromBottom (8);
        midiLogView.setBounds (leftContent);
    }

    // ---- MIDDLE pane: the rig visualiser ----
    dmxView.setBounds (midPaneArea);

    // ---- RIGHT pane: the clickable trigger menu (3 columns, no scroll) ----
    {
        auto rightContent = rightPaneArea.reduced (8).withTrimmedTop (16);
        triggerMenu.layoutForWidth (rightContent.getWidth());
        triggerMenu.setBounds (rightContent.withHeight (triggerMenu.preferredHeight()));
    }
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

    // Mirror the strobe shutter on screen (the driver is the source of
    // truth, whether triggered live or via the preview menu).
    dmxView.setStrobeActive (proc.getDmx().getStrobeHz() > 0.0f);
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

    // Keep only the most recent 10 lines. The log is just for
    // confirming MIDI arrives — not a post-mortem record — so the
    // TextEditor's relayout cost stays trivial regardless of how
    // many notes have fired since the plugin loaded.
    constexpr int kMaxLines = 10;
    int newlineCount = 0;
    for (auto ch : logBuffer)
        if (ch == '\n')
            ++newlineCount;
    while (newlineCount > kMaxLines)
    {
        const int nl = logBuffer.indexOfChar ('\n');
        if (nl < 0) break;
        logBuffer = logBuffer.substring (nl + 1);
        --newlineCount;
    }

    midiLogView.setText (logBuffer, false);
    midiLogView.moveCaretToEnd();
    logDirty = false;
}

}  // namespace hitnotedmx
