#include "PluginEditor.h"
#include "Showcase.h"

#include <juce_audio_basics/juce_audio_basics.h>

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

// Write a one-bar held chord of `pitches` to `file` (overwriting). Mirrors the
// held() clips in Showcase.cpp — same TPQN / velocity / channel — so a dragged
// clip behaves identically to the showcase combos. Returns false on write fail.
bool writeHeldChord (const juce::File& file, const std::vector<int>& pitches)
{
    constexpr int        kTpqn       = 960;   // ticks per quarter note (= 1 beat)
    constexpr double     kBeatsPerBar = 4.0;
    constexpr juce::uint8 kVel        = 100;

    juce::MidiMessageSequence seq;
    for (const int p : pitches)
    {
        if (p < 0 || p > 127)
            continue;
        seq.addEvent (juce::MidiMessage::noteOn  (1, p, kVel), 0.0);
        seq.addEvent (juce::MidiMessage::noteOff (1, p), kBeatsPerBar * kTpqn);
    }
    seq.updateMatchedPairs();

    juce::MidiFile mf;
    mf.setTicksPerQuarterNote (kTpqn);
    mf.addTrack (seq);

    file.deleteFile();
    if (auto os = file.createOutputStream())
        return mf.writeTo (*os);
    return false;
}
}

MidiDragTile::MidiDragTile()
{
    setMouseCursor (juce::MouseCursor::DraggingHandCursor);
}

void MidiDragTile::paint (juce::Graphics& g)
{
    const int  count   = getPitches ? static_cast<int> (getPitches().size()) : 0;
    const bool enabled = count > 0;
    const bool hover   = isMouseOver();

    auto r = getLocalBounds().toFloat().reduced (1.0f);
    g.setColour (juce::Colour (enabled ? 0xff2e2e2e : 0xff242424));
    g.fillRoundedRectangle (r, 4.0f);
    g.setColour (enabled ? (hover ? juce::Colour (0xff39c6c0) : juce::Colour (0xff7a7a7a))
                         : juce::Colour (0xff3a3a3a));
    g.drawRoundedRectangle (r, 4.0f, 1.0f);

    g.setColour (enabled ? (hover ? juce::Colours::white : juce::Colour (0xff9fbedd))
                         : juce::Colour (0xff5a5a5a));
    g.setFont (juce::FontOptions (8.5f, juce::Font::bold));
    const juce::String txt = enabled ? "DRAG\nMIDI\n(" + juce::String (count) + ")"
                                      : "drag\n(pick\ntiles)";
    g.drawFittedText (txt, getLocalBounds().reduced (2),
                      juce::Justification::centred, 3, 0.8f);
}

void MidiDragTile::mouseDrag (const juce::MouseEvent& e)
{
    // One drag loop at a time; ignore tiny jitters so a stray click never
    // spawns a file drag.
    if (dragging || e.getDistanceFromDragStart() < 8 || ! getPitches)
        return;

    const auto pitches = getPitches();
    if (pitches.empty())
        return;

    // A stable, human-readable temp name so the clip lands in Ableton/Finder as
    // "HitNoteDmx clip" rather than a random temp filename. Rewritten per drag.
    const auto file = juce::File::getSpecialLocation (juce::File::tempDirectory)
                          .getChildFile ("HitNoteDmx clip.mid");
    if (! writeHeldChord (file, pitches))
        return;

    dragging = true;
    juce::DragAndDropContainer::performExternalDragDropOfFiles (
        { file.getFullPathName() }, false /* can't move our temp file */, this,
        [this] { dragging = false; });
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
    : AudioProcessorEditor (&p), proc (p), dmxView (p.getDmxValues(), p.getSelection())
{
    // Flat & wide. The trigger menu is a transposed 12-row piano-roll grid,
    // so the visualiser height sets the minimum window height. Width fits the
    // left controls + the rig + the trigger grid (ten columns) + a narrow
    // far-right utility pane (click-velocity + drag placeholder).
    setSize (1168, 338);

    for (auto* btn : { &connectUsbButton, &blackoutButton, &initNamesButton, &showClipsButton })
    {
        btn->addListener (this);
        addAndMakeVisible (*btn);
    }
    blackoutButton.setClickingTogglesState (true);
    initNamesButton.setTooltip ("Install the named trigger rack into your Ableton User Library (MIDI Effects)");
    showClipsButton.setTooltip ("Write the demo clips to ~/Music/HitNoteDmx Showcase and open it in Finder");

    deviceStatusLabel.setJustificationType (juce::Justification::centredLeft);
    deviceStatusLabel.setColour (juce::Label::textColourId, juce::Colours::white);
    deviceStatusLabel.setFont (juce::FontOptions (11.0f));
    deviceStatusLabel.setMinimumHorizontalScale (0.7f);  // keep it to one line
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
    {
        proc.setPreviewPitches (pitches);
        latchedPitches = pitches;        // mirror for the MIDI-drag tile
        midiDragTile.repaint();          // reflect the new count / enabled state
    };

    // Far-right pane: click-velocity slider — sets the velocity that previewed
    // (clicked) triggers are held at, so the velocity-mapped behaviours can be
    // auditioned without a keyboard.
    clickVelLabel.setText ("VEL", juce::dontSendNotification);
    clickVelLabel.setJustificationType (juce::Justification::centred);
    clickVelLabel.setColour (juce::Label::textColourId, juce::Colours::white);
    clickVelLabel.setFont (juce::FontOptions (10.0f, juce::Font::bold));
    addAndMakeVisible (clickVelLabel);

    clickVelSlider.setRange (1.0, 127.0, 1.0);
    clickVelSlider.setValue (proc.getPreviewVelocity(), juce::dontSendNotification);
    clickVelSlider.setColour (juce::Slider::textBoxTextColourId, juce::Colours::white);
    clickVelSlider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    clickVelSlider.setColour (juce::Slider::thumbColourId, juce::Colour (0xff39c6c0));
    clickVelSlider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 30, 15);
    clickVelSlider.onValueChange = [this]
        { proc.setPreviewVelocity (static_cast<int> (clickVelSlider.getValue())); };
    addAndMakeVisible (clickVelSlider);

    // Drag-MIDI tile: drag the currently-latched trigger set out to Finder /
    // Ableton as a one-bar .mid. Reads the latched set captured above.
    midiDragTile.getPitches = [this] { return latchedPitches; };
    midiDragTile.setTooltip ("Latch triggers in the menu, then drag this out to drop a one-bar MIDI clip");
    addAndMakeVisible (midiDragTile);

    midiLogView.setMultiLine (true, false);
    midiLogView.setReadOnly (true);
    midiLogView.setCaretVisible (false);
    midiLogView.setScrollbarsShown (true);
    midiLogView.setFont (juce::Font (juce::FontOptions ("Menlo", 12.0f, juce::Font::plain)));
    midiLogView.setColour (juce::TextEditor::backgroundColourId, juce::Colours::black);
    midiLogView.setColour (juce::TextEditor::textColourId,       juce::Colours::lightgreen);
    addAndMakeVisible (midiLogView);

    refreshDeviceStatus();
    updateConnectButton();   // reflect a session already running (reopened editor)
    lastLinkUp = proc.getDmx().isConnected();
    startTimerHz (15);  // visual confirmation only; combined with the
                        // visualiser's fingerprint diff, the actual
                        // GUI work is bounded by how often the rig's
                        // 228 quantised channels change byte values.
}

HitNoteDmxAudioProcessorEditor::~HitNoteDmxAudioProcessorEditor()
{
    stopTimer();
    connectUsbButton.removeListener (this);
    blackoutButton.removeListener (this);
    initNamesButton.removeListener (this);
    showClipsButton.removeListener (this);
    ledDimSlider.setLookAndFeel (nullptr);
    spotDimSlider.setLookAndFeel (nullptr);
    densitySlider.setLookAndFeel (nullptr);
}

void HitNoteDmxAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff181818));

    // App title — centred above the visualiser, flush with the top of the
    // side panes. A light, gently letter-spaced flamingo-pink wordmark.
    {
        juce::Font title (juce::FontOptions ("Helvetica Neue", 22.0f, juce::Font::plain));
        title.setExtraKerningFactor (0.12f);
        g.setFont (title);
        g.setColour (juce::Colour (0xfff48fb1));   // soft flamingo pink
        g.drawText ("Flamingo Hitmix at Night", titleArea,
                    juce::Justification::centred, false);
    }

    // Pane cards.
    auto card = [&g] (juce::Rectangle<int> r, const juce::String& title)
    {
        if (r.isEmpty()) return;
        g.setColour (juce::Colour (0xff262626));
        g.fillRoundedRectangle (r.toFloat(), 6.0f);
        if (title.isEmpty()) return;   // panel only, no header
        g.setColour (juce::Colour (0xff7a7a7a));
        g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
        g.drawText (title.toUpperCase(), r.getX() + 10, r.getY() + 6, r.getWidth() - 20, 12,
                    juce::Justification::centredLeft);
    };
    card (leftPaneArea,  {});   // controls: panel only, title removed (obvious)
    card (rightPaneArea, {});   // triggers: panel only, title removed
    card (extraPaneArea, {});   // far-right utility pane
    // Middle pane is the opaque visualiser; no card needed behind it.
}

void HitNoteDmxAudioProcessorEditor::resized()
{
    // Tighter bottom inset than top so the panes don't float above a fat
    // bottom margin (top keeps room for the title strip).
    auto area = getLocalBounds().reduced (12, 0).withTrimmedTop (12).withTrimmedBottom (6);

    const int gap = 12;
    leftPaneArea  = area.removeFromLeft (240);
    area.removeFromLeft (gap);
    extraPaneArea = area.removeFromRight (36);   // narrow far-right utility pane (slider)
    area.removeFromRight (gap);
    rightPaneArea = area.removeFromRight (500);  // trigger grid — columns ~20% narrower
    area.removeFromRight (gap);
    midPaneArea   = area;

    // Title strip above the visualiser, flush with the top of the side panes;
    // the visualiser drops below it.
    titleArea   = midPaneArea.removeFromTop (30);
    midPaneArea.removeFromTop (4);

    // Content inset inside the left card (no card title any more).
    auto leftContent = leftPaneArea.reduced (10).withTrimmedTop (4);

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

        // Utility controls pinned to the bottom: two rows of paired buttons
        // (Connect/Blackout, then Init names/Show clips) + a status line. The
        // MIDI log takes whatever is left in between.
        auto controls = leftContent.removeFromBottom (90);
        auto pairRow = [] (juce::Rectangle<int> row, juce::Button& left, juce::Button& right)
        {
            const int half = row.getWidth() / 2;
            left.setBounds  (row.removeFromLeft  (half - 3));
            right.setBounds (row.removeFromRight (half - 3));
        };
        pairRow (controls.removeFromTop (28), connectUsbButton, blackoutButton);
        controls.removeFromTop (5);
        pairRow (controls.removeFromTop (28), initNamesButton, showClipsButton);
        controls.removeFromTop (5);
        deviceStatusLabel.setBounds (controls);  // remaining (~18px, one line)

        leftContent.removeFromBottom (8);
        midiLogView.setBounds (leftContent);
    }

    // ---- MIDDLE pane: the rig visualiser ----
    dmxView.setBounds (midPaneArea);

    // ---- RIGHT pane: the transposed piano-roll trigger grid (no scroll) ----
    // The menu fills the whole card; its rows scale to the height.
    triggerMenu.setBounds (rightPaneArea.reduced (4));

    // ---- FAR-RIGHT pane: click-velocity slider (top) + drag placeholder ----
    {
        auto content = extraPaneArea.reduced (2, 6);
        auto top = content.removeFromTop (content.getHeight() * 6 / 10);
        clickVelLabel.setBounds (top.removeFromTop (16));
        clickVelSlider.setBounds (top.reduced (0, 2));
        content.removeFromTop (8);
        midiDragTile.setBounds (content);
    }
}

void HitNoteDmxAudioProcessorEditor::buttonClicked (juce::Button* b)
{
    if (b == &connectUsbButton)
    {
        auto& dmx = proc.getDmx();
        if (dmx.isRunning())
        {
            // Release the port so another instance can take it (or just stop).
            dmx.disconnect();
            appendLog ("[usb] disconnected");
        }
        else
        {
            const bool ok = dmx.connect();
            appendLog (ok ? "[usb] connected" : "[usb] connect failed");
        }
        refreshDeviceStatus();
        updateConnectButton();
        lastLinkUp = dmx.isConnected();   // don't double-log via the timer
    }
    else if (b == &blackoutButton)
    {
        const bool on = blackoutButton.getToggleState();
        proc.blackout = on;
        proc.getDmx().setBlackout (on);
        appendLog (on ? "[blackout] ON" : "[blackout] OFF");
    }
    else if (b == &initNamesButton)
    {
        const auto rack = Showcase::installRack();
        appendLog ("[names] " + rack.getFullPathName());
    }
    else if (b == &showClipsButton)
    {
        const auto clips = Showcase::writeClips (Showcase::defaultRoot());
        appendLog ("[clips] " + clips.getFullPathName());
        clips.revealToUser();
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

    // Surface auto-reconnect activity: the driver keeps the link alive on its
    // own thread now, so poll its state and log the edges (drop / recovery) and
    // refresh the status line — otherwise a mid-show hiccup would be silent.
    const bool linkUp = proc.getDmx().isConnected();
    if (linkUp != lastLinkUp)
    {
        appendLog (linkUp ? "[usb] link restored" : "[usb] link lost - reconnecting");
        refreshDeviceStatus();
        lastLinkUp = linkUp;
    }

    // Light up trigger tiles whose notes are currently sounding (MIDI + preview).
    proc.getHeldPitches (heldScratch);
    triggerMenu.setLiveNotes (heldScratch);

    // Refresh the live DMX preview only when something actually changed.
    dmxView.repaintIfChanged();

    // Mirror the strobe shutter on screen (the driver is the source of
    // truth, whether triggered live or via the preview menu). The rate
    // carries through so the preview's black gap tracks velocity.
    dmxView.setStrobe (proc.getDmx().getStrobeHz());
}

void HitNoteDmxAudioProcessorEditor::refreshDeviceStatus()
{
    deviceStatusLabel.setText (proc.getDmx().getStatusText(),
                               juce::dontSendNotification);
}

void HitNoteDmxAudioProcessorEditor::updateConnectButton()
{
    // "Disconnect" whenever this instance owns the port (incl. across an
    // auto-reconnect gap); "Connect USB" otherwise. Driven by the session
    // flag, not the momentary link, so it survives the editor being reopened.
    connectUsbButton.setButtonText (proc.getDmx().isRunning() ? "Disconnect"
                                                              : "Connect USB");
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
