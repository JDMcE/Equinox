#include "PluginEditor.h"

namespace
{
    constexpr int headerHeight = 44;
    constexpr int controlRowHeight = 64; // tall enough for rotary knobs

    // Eight-tooth gear for the settings button.
    juce::Path makeGearIcon()
    {
        juce::Path icon;
        juce::Path circle;
        circle.addEllipse (-5.0f, -5.0f, 10.0f, 10.0f);
        juce::PathStrokeType (2.8f).createStrokedPath (icon, circle);

        for (int i = 0; i < 8; ++i)
        {
            juce::Path tooth;
            tooth.addRoundedRectangle (-1.5f, -8.2f, 3.0f, 4.4f, 1.0f);
            tooth.applyTransform (juce::AffineTransform::rotation (
                juce::MathConstants<float>::twoPi * (float) i / 8.0f));
            icon.addPath (tooth);
        }
        return icon;
    }
}

EeqAudioProcessorEditor::EeqAudioProcessorEditor (EeqAudioProcessor& p)
    : AudioProcessorEditor (p), proc (p), display (p), bandPanel (p), meter (p)
{
    // The shared family LookAndFeel (ported from Meridian) restyles every
    // stock control; children pick it up through the component hierarchy.
    setLookAndFeel (&lookAndFeel);

    addAndMakeVisible (display);
    addAndMakeVisible (bandPanel);
    addAndMakeVisible (meter);

    display.onSelectionChanged = [this] (int band)
    {
        bandPanel.setBand (band);
        if (band >= 0)
            proc.activeBandUI.store (band); // MIDI Learn "selected band" targets
    };

    bandPanel.onDelete = [this] (int band) { display.deleteBand (band); };

    titleLabel.setFont (juce::Font (juce::FontOptions (19.0f, juce::Font::bold)));
    titleLabel.setColour (juce::Label::textColourId, eeq::colors::accent);
    addAndMakeVisible (titleLabel);

    sketchButton.setClickingTogglesState (true);
    sketchButton.setTooltip ("Draw an EQ curve; bands are fitted on release");
    sketchButton.onClick = [this] { display.setSketchMode (sketchButton.getToggleState()); };
    display.onSketchFinished = [this] { sketchButton.setToggleState (false, juce::dontSendNotification); };
    addAndMakeVisible (sketchButton);

    abButton.setTooltip ("Switch between A and B states");
    abButton.onClick = [this] { proc.toggleAB(); updateAbButtons(); };
    addAndMakeVisible (abButton);

    copyAbButton.setTooltip ("Copy the current state to the other slot");
    copyAbButton.onClick = [this] { proc.copyCurrentToOtherSlot(); };
    addAndMakeVisible (copyAbButton);

    // Preset navigation (same header pattern as Meridian).
    prevPresetButton.setButtonText (juce::String::fromUTF8 ("\xe2\x80\xb9"));
    nextPresetButton.setButtonText (juce::String::fromUTF8 ("\xe2\x80\xba"));
    prevPresetButton.setTitle ("Previous preset");
    nextPresetButton.setTitle ("Next preset");
    presetNameButton.setTitle ("Preset browser");
    prevPresetButton.setTooltip ("Load the previous preset ( [ )");
    nextPresetButton.setTooltip ("Load the next preset ( ] )");
    presetNameButton.setTooltip ("Open the preset browser (P): search, tags, "
                                 "categories, favorites, save and share presets");
    prevPresetButton.onClick = [this] { proc.presets.loadAdjacent (false); };
    nextPresetButton.onClick = [this] { proc.presets.loadAdjacent (true); };
    presetNameButton.onClick = [this]
    {
        if (presetBrowser.isVisible())
            presetBrowser.closeBrowser();
        else
            presetBrowser.openBrowser();
    };
    addAndMakeVisible (prevPresetButton);
    addAndMakeVisible (presetNameButton);
    addAndMakeVisible (nextPresetButton);

    presetBrowser.onVisibilityChange = [this] { repaint(); };
    addChildComponent (presetBrowser);

    const juce::String pm (juce::CharPointer_UTF8 ("\xc2\xb1"));
    rangeBox.addItemList ({ pm + "3 dB", pm + "6 dB", pm + "12 dB", pm + "30 dB" }, 1);
    const float storedRange = display.getDisplayRange();
    rangeBox.setSelectedId (storedRange <= 3.0f ? 1 : storedRange <= 6.0f ? 2
                          : storedRange <= 12.0f ? 3 : 4, juce::dontSendNotification);
    rangeBox.onChange = [this]
    {
        constexpr float ranges[] = { 3.0f, 6.0f, 12.0f, 30.0f };
        const int id = rangeBox.getSelectedId();
        if (id >= 1 && id <= 4)
            display.setDisplayRange (ranges[id - 1]);
    };
    addAndMakeVisible (rangeBox);
    updateAbButtons();

    phaseButton.setClickingTogglesState (true);
    phaseButton.setTooltip ("Phase invert");
    addAndMakeVisible (phaseButton);
    phaseAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        proc.apvts, eeq::phaseInvId, phaseButton);

    outGainSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    outGainSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 68, 20);
    outGainSlider.setTextValueSuffix (" dB");
    outGainSlider.setNumDecimalPlacesToDisplay (1);
    addAndMakeVisible (outGainSlider);
    outGainAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        proc.apvts, eeq::outGainId, outGainSlider);

    outGainLabel.setJustificationType (juce::Justification::centredRight);
    outGainLabel.setColour (juce::Label::textColourId, eeq::colors::textDim);
    addAndMakeVisible (outGainLabel);

    // Control strip groups: ENGINE, LEVEL, SPECTRAL, ANALYZER.
    auto initCombo = [this] (juce::ComboBox& box, const juce::StringArray& items,
                             const juce::String& paramId, auto& attachment)
    {
        box.addItemList (items, 1);
        addAndMakeVisible (box);
        attachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
            proc.apvts, paramId, box);
    };

    initCombo (phaseModeBox, eeq::phaseModeChoices, eeq::phaseModeId, phaseModeAtt);
    initCombo (lpQualityBox, eeq::lpQualityChoices, eeq::lpQualityId, lpQualityAtt);
    initCombo (characterBox, eeq::characterChoices, eeq::characterId, characterAtt);

    autoGainButton.setClickingTogglesState (true);
    addAndMakeVisible (autoGainButton);
    autoGainAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        proc.apvts, eeq::autoGainId, autoGainButton);

    // The strip's value controls are the same rotary knobs used everywhere
    // else (vertical drag) — small linear tracks were too fiddly.
    gainScaleSlider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    gainScaleSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 46, 18);
    addAndMakeVisible (gainScaleSlider);
    gainScaleAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        proc.apvts, eeq::gainScaleId, gainScaleSlider);

    gainScaleLabel.setColour (juce::Label::textColourId, eeq::colors::textDim);
    gainScaleLabel.setFont (juce::Font (juce::FontOptions (12.0f)));
    gainScaleLabel.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (gainScaleLabel);

    latencyLabel.setColour (juce::Label::textColourId, eeq::colors::textDim.withAlpha (0.85f));
    latencyLabel.setFont (juce::Font (juce::FontOptions (11.0f)));
    latencyLabel.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (latencyLabel);

    // Spectral dynamics controls.
    specButton.setClickingTogglesState (true);
    specButton.setTooltip ("Spectral dynamics: tame bins that stick out above "
                           "their spectral neighbourhood");
    addAndMakeVisible (specButton);
    specOnAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        proc.apvts, eeq::specOnId, specButton);

    auto initSmallSlider = [this] (juce::Slider& s, const juce::String& tooltip)
    {
        s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        s.setTextBoxStyle (juce::Slider::TextBoxRight, false, 42, 18);
        s.setTooltip (tooltip);
        addAndMakeVisible (s);
    };
    initSmallSlider (specAmountSlider, "Spectral amount: maximum per-bin reduction (dB)");
    initSmallSlider (specSensSlider, "Spectral sensitivity: how far a bin must exceed "
                                     "its neighbourhood before reduction (dB)");

    for (auto* l : { &specAmountLabel, &specSensLabel })
    {
        l->setColour (juce::Label::textColourId, eeq::colors::textDim);
        l->setFont (juce::Font (juce::FontOptions (12.0f)));
        l->setJustificationType (juce::Justification::centredRight);
        addAndMakeVisible (*l);
    }

    analyzerButton.setTooltip ("Analyzer options: pre/post/external views, "
                               "collisions, freeze, speed");
    analyzerButton.onClick = [this] { display.showAnalyzerMenu (analyzerButton); };
    addAndMakeVisible (analyzerButton);

    midiButton.setTooltip ("MIDI Learn: map hardware CCs to parameters, "
                           "including the currently selected band");
    midiButton.onClick = [this] { showMidiMenu(); };
    addAndMakeVisible (midiButton);

    matchButton.setTooltip ("EQ Match: capture the input and a reference "
                            "(sidechain or pasted spectrum), then fit bands "
                            "to the difference");
    matchButton.onClick = [this] { display.showMatchMenu (matchButton); };
    addAndMakeVisible (matchButton);

    settingsButton.setShape (makeGearIcon(), false, true, false);
    settingsButton.setBorderSize (juce::BorderSize<int> (7));
    settingsButton.setColours (eeq::colors::textDim, eeq::colors::text, eeq::colors::accent);
    settingsButton.setTitle ("Settings");
    settingsButton.setTooltip ("Settings: GPU rendering, help hints, preset folder");
    settingsButton.onClick = [this] { showSettingsMenu(); };
    addAndMakeVisible (settingsButton);

    // Hints/tooltips for the remaining header controls.
    rangeBox.setTooltip ("Display range: 3/6 dB for mastering, 12/30 dB for mixing");
    phaseModeBox.setTooltip ("Zero Latency: minimum-phase IIR. Natural Phase: "
                             "2x oversampled, uncramped near Nyquist. Linear Phase: "
                             "FIR, no phase shift, adds latency");
    lpQualityBox.setTooltip ("Linear phase FIR length: higher = better low-frequency "
                             "accuracy, more latency");
    characterBox.setTooltip ("Character: Gentle and Warm add analog-style saturation");
    autoGainButton.setTooltip ("Compensate the average EQ gain so loudness stays "
                               "comparable when toggling the EQ");
    gainScaleSlider.setTooltip ("Scale the gain of every band, 0-200%");
    outGainSlider.setTooltip ("Output gain, applied after all processing");

    setHintsEnabled ((bool) proc.apvts.state.getProperty ("ui_hints", true), false);
    setGpuEnabled ((bool) proc.apvts.state.getProperty ("ui_gpu", false), false);
    specAmountAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        proc.apvts, eeq::specAmountId, specAmountSlider);
    specSensAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        proc.apvts, eeq::specSensId, specSensSlider);

    startTimerHz (4);

    setWantsKeyboardFocus (true);
    setResizable (true, true);
    setResizeLimits (1000, 560, 2400, 1600);
    setSize (1080, 680);

    // Sliders cache their text-box colours when created; re-broadcast the
    // look-and-feel now that the whole tree is attached.
    sendLookAndFeelChange();
}

EeqAudioProcessorEditor::~EeqAudioProcessorEditor()
{
    setLookAndFeel (nullptr);
    if (glAttached)
        glContext.detach();
}

void EeqAudioProcessorEditor::setGpuEnabled (bool enabled, bool save)
{
    if (enabled != glAttached)
    {
        if (enabled)
            glContext.attachTo (*this);
        else
            glContext.detach();
        glAttached = enabled;
    }

    if (save)
        proc.apvts.state.setProperty ("ui_gpu", enabled, nullptr);
}

void EeqAudioProcessorEditor::setHintsEnabled (bool enabled, bool save)
{
    if (enabled && tooltipWindow == nullptr)
        tooltipWindow = std::make_unique<juce::TooltipWindow> (this, 600);
    else if (! enabled)
        tooltipWindow.reset();

    hintsEnabled = enabled;
    if (save)
        proc.apvts.state.setProperty ("ui_hints", enabled, nullptr);
}

void EeqAudioProcessorEditor::showSettingsMenu()
{
    juce::PopupMenu menu;
    menu.addItem (1, "GPU rendering (OpenGL)", true, glAttached);
    menu.addItem (2, "Help hints on hover", true, hintsEnabled);
    menu.addSeparator();
    menu.addItem (3, "Open user preset folder...");

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (settingsButton),
        [this] (int result)
        {
            if (result == 1)
                setGpuEnabled (! glAttached, true);
            else if (result == 2)
                setHintsEnabled (! hintsEnabled, true);
            else if (result == 3)
                proc.presets.userPresetDirectory().revealToUser();
        });
}

void EeqAudioProcessorEditor::timerCallback()
{
    const int samples = proc.getLatencySamples();
    const double sr = proc.getSampleRate();

    juce::String text = "latency: ";
    if (samples <= 0)
        text << "0 ms";
    else
        text << juce::String (sr > 0 ? samples * 1000.0 / sr : 0.0, 1) << " ms ("
             << samples << " smp)";

    latencyLabel.setText (text, juce::dontSendNotification);

    // Preset name follows loads from the browser, arrows and shortcuts.
    const auto presetName = proc.presets.getCurrentPresetName();
    const auto buttonText = presetName.isEmpty() ? juce::String ("PRESETS") : presetName;
    if (presetNameButton.getButtonText() != buttonText)
        presetNameButton.setButtonText (buttonText);
}

void EeqAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (eeq::colors::background);

    // Product mark: a disc crossed by its equator — day and night in
    // balance. Meridian draws the same disc with a vertical meridian line;
    // one quiet glyph per plugin in the family.
    if (! logoArea.isEmpty())
    {
        auto disc = logoArea.toFloat().withSizeKeepingCentre (15.0f, 15.0f);
        g.setColour (eeq::colors::accent);
        g.drawEllipse (disc, 1.2f);
        g.drawEllipse (disc.reduced (0.0f, disc.getHeight() * 0.325f), 1.0f);
    }

    // Header divider.
    g.setColour (eeq::colors::outline);
    g.drawHorizontalLine (headerHeight, 0.0f, (float) getWidth());

    // Control groups: quiet panels with caps captions, matching Meridian's
    // section styling.
    for (const auto& group : controlGroups)
    {
        auto r = group.area.toFloat().reduced (0.5f, 1.0f);
        g.setColour (eeq::colors::panel.withAlpha (0.55f));
        g.fillRoundedRectangle (r, 4.0f);
        g.setColour (eeq::colors::outline);
        g.drawRoundedRectangle (r, 4.0f, 1.0f);

        g.setColour (eeq::colors::textDim);
        g.setFont (juce::Font (juce::FontOptions (9.5f, juce::Font::bold)));
        g.drawText (group.caption, group.area.reduced (8, 3).removeFromTop (10),
                    juce::Justification::centredLeft);
    }
}

void EeqAudioProcessorEditor::resized()
{
    auto area = getLocalBounds();

    auto header = area.removeFromTop (headerHeight);
    header.removeFromLeft (10);
    logoArea = header.removeFromLeft (24);
    titleLabel.setBounds (header.removeFromLeft (96).reduced (0, 4));

    sketchButton.setBounds (header.removeFromLeft (62).reduced (2, 8));
    abButton.setBounds (header.removeFromLeft (36).reduced (2, 8));
    copyAbButton.setBounds (header.removeFromLeft (46).reduced (2, 8));
    header.removeFromLeft (8);
    prevPresetButton.setBounds (header.removeFromLeft (22).reduced (1, 8));
    presetNameButton.setBounds (header.removeFromLeft (150).reduced (1, 8));
    nextPresetButton.setBounds (header.removeFromLeft (22).reduced (1, 8));

    header.removeFromRight (10);
    settingsButton.setBounds (header.removeFromRight (30).reduced (2, 8));
    midiButton.setBounds (header.removeFromRight (48).reduced (2, 8));
    header.removeFromRight (6);
    outGainSlider.setBounds (header.removeFromRight (210).reduced (0, 8));
    outGainLabel.setBounds (header.removeFromRight (52).reduced (0, 6));
    phaseButton.setBounds (header.removeFromRight (34).reduced (4, 8));
    latencyLabel.setBounds (header.reduced (8, 6));

    // Grouped control strip above the display.
    auto row = area.removeFromTop (controlRowHeight);
    row.removeFromLeft (12);
    row.removeFromRight (12);
    row.removeFromTop (4);
    row.removeFromBottom (4);

    controlGroups.clear();
    auto beginGroup = [&row, this] (const juce::String& caption, int width)
    {
        auto r = row.removeFromLeft (width);
        controlGroups.push_back ({ caption, r });
        row.removeFromLeft (8);
        r.removeFromTop (14); // caption strip
        return r.reduced (6, 1);
    };

    // Knobs span the full content height; combos, buttons and labels sit
    // vertically centred beside them.
    auto engine = beginGroup ("ENGINE", 244);
    phaseModeBox.setBounds (engine.removeFromLeft (94).reduced (2, 8));
    lpQualityBox.setBounds (engine.removeFromLeft (64).reduced (2, 8));
    characterBox.setBounds (engine.removeFromLeft (70).reduced (2, 8));

    auto level = beginGroup ("LEVEL", 224);
    autoGainButton.setBounds (level.removeFromLeft (78).reduced (2, 9));
    gainScaleLabel.setBounds (level.removeFromLeft (38).reduced (2, 8));
    gainScaleSlider.setBounds (level.reduced (2, 0));

    auto spectral = beginGroup ("SPECTRAL", 286);
    specButton.setBounds (spectral.removeFromLeft (34).reduced (2, 9));
    specAmountLabel.setBounds (spectral.removeFromLeft (30).reduced (2, 8));
    specAmountSlider.setBounds (spectral.removeFromLeft (88).reduced (2, 0));
    specSensLabel.setBounds (spectral.removeFromLeft (34).reduced (2, 8));
    specSensSlider.setBounds (spectral.reduced (2, 0));

    auto analyzer = beginGroup ("ANALYZER", 196);
    analyzerButton.setBounds (analyzer.removeFromLeft (62).reduced (2, 8));
    rangeBox.setBounds (analyzer.removeFromLeft (76).reduced (2, 8));
    matchButton.setBounds (analyzer.reduced (2, 8));

    bandPanel.setBounds (area.removeFromBottom (112));
    meter.setBounds (area.removeFromRight (46));
    display.setBounds (area);

    presetBrowser.setBounds (getLocalBounds());
}

void EeqAudioProcessorEditor::showMidiMenu()
{
    juce::PopupMenu menu;
    const bool armed = proc.midiLearnTarget.load() >= 0;

    juce::PopupMenu learn;
    for (int t = 0; t < eeq::numMidiTargets; ++t)
        learn.addItem (1000 + t, "Learn: " + eeq::midiTargetNames[t],
                       true, proc.midiLearnTarget.load() == t);
    menu.addSubMenu ("Learn (move a CC after choosing)", learn);

    if (armed)
        menu.addItem (2, "Cancel learn");

    // Existing mappings; click one to remove it.
    bool anyMapping = false;
    juce::PopupMenu mappings;
    for (int cc = 0; cc < 128; ++cc)
    {
        const int target = proc.getCCMapping (cc);
        if (target >= 0 && target < eeq::numMidiTargets)
        {
            mappings.addItem (3000 + cc, "CC " + juce::String (cc) + "  ->  "
                                             + eeq::midiTargetNames[target]);
            anyMapping = true;
        }
    }
    if (anyMapping)
    {
        menu.addSeparator();
        menu.addSubMenu ("Remove mapping", mappings);
        menu.addItem (3, "Clear all mappings");
    }

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (midiButton),
        [this] (int result)
        {
            if (result >= 3000 && result < 3128)
                proc.setCCMapping (result - 3000, -1);
            else if (result >= 1000 && result < 1000 + eeq::numMidiTargets)
                proc.midiLearnTarget.store (result - 1000);
            else if (result == 2)
                proc.midiLearnTarget.store (-1);
            else if (result == 3)
                proc.clearCCMappings();
        });
}

void EeqAudioProcessorEditor::updateAbButtons()
{
    const bool isB = proc.isSlotB();
    abButton.setButtonText (isB ? "B" : "A");
    copyAbButton.setButtonText (isB ? "B>A" : "A>B");
}

bool EeqAudioProcessorEditor::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress ('z', juce::ModifierKeys::ctrlModifier, 0))
    {
        proc.undoManager.undo();
        return true;
    }

    if (key == juce::KeyPress ('z', juce::ModifierKeys::ctrlModifier
                                        | juce::ModifierKeys::shiftModifier, 0)
        || key == juce::KeyPress ('y', juce::ModifierKeys::ctrlModifier, 0))
    {
        proc.undoManager.redo();
        return true;
    }

    if (key == juce::KeyPress::escapeKey && presetBrowser.isVisible())
    {
        presetBrowser.closeBrowser();
        return true;
    }

    // Preset shortcuts, matching Meridian: P toggles the browser, [ and ]
    // step through presets.
    switch (key.getTextCharacter())
    {
        case 'p': case 'P':
            if (presetBrowser.isVisible()) presetBrowser.closeBrowser();
            else                           presetBrowser.openBrowser();
            return true;
        case '[': proc.presets.loadAdjacent (false); return true;
        case ']': proc.presets.loadAdjacent (true); return true;
        default: break;
    }

    return false;
}
