#include "BandPanel.h"
#include "EqDisplay.h"
#include "LookAndFeel.h"
#include "../util/NoteNames.h"

namespace eeq
{

BandPanel::BandPanel (EeqAudioProcessor& p) : proc (p)
{
    auto initCombo = [this] (juce::ComboBox& box, const juce::StringArray& items)
    {
        box.addItemList (items, 1);
        addAndMakeVisible (box);
    };

    initCombo (typeBox, typeChoices);
    initCombo (slopeBox, slopeChoices);
    initCombo (chanBox, channelChoices);

    auto initSlider = [this] (juce::Slider& s, juce::Label& l, const juce::String& suffix)
    {
        s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 74, 18);
        s.setTextValueSuffix (suffix);
        addAndMakeVisible (s);

        l.setJustificationType (juce::Justification::centred);
        l.setFont (juce::Font (juce::FontOptions (12.0f)));
        l.attachToComponent (&s, false);
        addAndMakeVisible (l);
    };

    initSlider (freqSlider, freqLabel, " Hz");
    initSlider (gainSlider, gainLabel, " dB");
    initSlider (qSlider, qLabel, "");

    freqSlider.setNumDecimalPlacesToDisplay (1);
    gainSlider.setNumDecimalPlacesToDisplay (1);
    qSlider.setNumDecimalPlacesToDisplay (2);

    initSlider (rangeSlider, rangeLabel, " dB");
    initSlider (threshSlider, threshLabel, " dB");
    initSlider (attSlider, attLabel, " ms");
    initSlider (relSlider, relLabel, " ms");

    addAndMakeVisible (dynButton);
    addAndMakeVisible (extButton);
    addAndMakeVisible (scfButton);

    dynButton.onClick = [this] { updateDynVisibility(); };

    enableButton.setTooltip ("Enable the band; when off it stays in the display, greyed out");
    addAndMakeVisible (enableButton);

    deleteButton.setTooltip ("Delete this band");
    deleteButton.setColour (juce::TextButton::textColourOffId, colors::textDim);
    deleteButton.onClick = [this]
    {
        if (onDelete && band >= 0)
            onDelete (band);
    };
    addAndMakeVisible (deleteButton);

    soloButton.setColour (juce::TextButton::buttonOnColourId, colors::warn.darker (0.2f));
    soloButton.onHold = [this] (bool held)
    {
        proc.soloBand.store (held && band >= 0 ? band : -1);
        soloButton.setToggleState (held, juce::dontSendNotification);
    };
    addAndMakeVisible (soloButton);

    titleLabel.setFont (juce::Font (juce::FontOptions (14.0f, juce::Font::bold)));
    titleLabel.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (titleLabel);

    noteLabel.setFont (juce::Font (juce::FontOptions (11.0f)));
    noteLabel.setJustificationType (juce::Justification::centredLeft);
    noteLabel.setColour (juce::Label::textColourId, colors::textDim.withAlpha (0.8f));
    addAndMakeVisible (noteLabel);

    startTimerHz (10);

    hintLabel.setJustificationType (juce::Justification::centred);
    hintLabel.setColour (juce::Label::textColourId, colors::textDim.withAlpha (0.8f));
    addAndMakeVisible (hintLabel);

    setBand (-1);
}

void BandPanel::setBand (int bandIndex)
{
    band = bandIndex;

    enableAtt.reset();
    typeAtt.reset();
    slopeAtt.reset();
    chanAtt.reset();
    freqAtt.reset();
    gainAtt.reset();
    qAtt.reset();
    dynAtt.reset();
    extAtt.reset();
    scfAtt.reset();
    rangeAtt.reset();
    threshAtt.reset();
    attAtt2.reset();
    relAtt.reset();

    const bool hasBand = band >= 0;

    for (auto* c : { (juce::Component*) &enableButton, (juce::Component*) &soloButton,
                     (juce::Component*) &typeBox,
                     (juce::Component*) &slopeBox, (juce::Component*) &chanBox,
                     (juce::Component*) &freqSlider, (juce::Component*) &gainSlider,
                     (juce::Component*) &qSlider, (juce::Component*) &titleLabel,
                     (juce::Component*) &noteLabel,
                     (juce::Component*) &freqLabel, (juce::Component*) &gainLabel,
                     (juce::Component*) &qLabel,
                     (juce::Component*) &dynButton, (juce::Component*) &deleteButton })
        c->setVisible (hasBand);

    hintLabel.setVisible (! hasBand);
    updateDynVisibility(); // hides the dynamics controls when no band is selected

    if (! hasBand)
        return;

    auto& s = proc.apvts;
    enableAtt = std::make_unique<ButtonAttachment> (s, bandParamId (band, "on"), enableButton);
    typeAtt   = std::make_unique<ComboAttachment>  (s, bandParamId (band, "type"), typeBox);
    slopeAtt  = std::make_unique<ComboAttachment>  (s, bandParamId (band, "slope"), slopeBox);
    chanAtt   = std::make_unique<ComboAttachment>  (s, bandParamId (band, "chan"), chanBox);
    freqAtt   = std::make_unique<SliderAttachment> (s, bandParamId (band, "freq"), freqSlider);
    gainAtt   = std::make_unique<SliderAttachment> (s, bandParamId (band, "gain"), gainSlider);
    qAtt      = std::make_unique<SliderAttachment> (s, bandParamId (band, "q"), qSlider);
    dynAtt    = std::make_unique<ButtonAttachment> (s, bandParamId (band, "dyn"), dynButton);
    extAtt    = std::make_unique<ButtonAttachment> (s, bandParamId (band, "ext"), extButton);
    scfAtt    = std::make_unique<ButtonAttachment> (s, bandParamId (band, "scf"), scfButton);
    rangeAtt  = std::make_unique<SliderAttachment> (s, bandParamId (band, "dynrange"), rangeSlider);
    threshAtt = std::make_unique<SliderAttachment> (s, bandParamId (band, "thresh"), threshSlider);
    attAtt2   = std::make_unique<SliderAttachment> (s, bandParamId (band, "att"), attSlider);
    relAtt    = std::make_unique<SliderAttachment> (s, bandParamId (band, "rel"), relSlider);

    titleLabel.setText ("Band " + juce::String (band + 1), juce::dontSendNotification);
    titleLabel.setColour (juce::Label::textColourId, EqDisplay::bandColour (band));

    updateDynVisibility();
}

// The dynamics controls only appear while Dyn is ticked (and the shape
// supports dynamics).
void BandPanel::updateDynVisibility()
{
    const auto type = band >= 0
        ? (FilterType) (int) proc.apvts.getRawParameterValue (bandParamId (band, "type"))->load()
        : FilterType::bell;

    const bool show = band >= 0 && dynButton.getToggleState() && typeSupportsDynamics (type);

    for (auto* c : { (juce::Component*) &rangeSlider, (juce::Component*) &threshSlider,
                     (juce::Component*) &attSlider, (juce::Component*) &relSlider,
                     (juce::Component*) &rangeLabel, (juce::Component*) &threshLabel,
                     (juce::Component*) &attLabel, (juce::Component*) &relLabel,
                     (juce::Component*) &extButton, (juce::Component*) &scfButton })
        c->setVisible (show);
}

void BandPanel::timerCallback()
{
    if (band < 0)
        return;

    const auto freq = proc.apvts.getRawParameterValue (bandParamId (band, "freq"))->load();
    noteLabel.setText (freqToNoteName (freq), juce::dontSendNotification);

    // Dynamics only apply to Bell and Shelf shapes.
    const auto type = (FilterType) (int) proc.apvts
        .getRawParameterValue (bandParamId (band, "type"))->load();
    dynButton.setEnabled (typeSupportsDynamics (type));
    extButton.setEnabled (proc.sidechainConnected());

    // Follow type/dyn changes coming from automation, undo or paste.
    updateDynVisibility();
}

void BandPanel::paint (juce::Graphics& g)
{
    g.fillAll (colors::panel);
    g.setColour (colors::outline);
    g.drawHorizontalLine (0, 0.0f, (float) getWidth());
}

void BandPanel::resized()
{
    auto area = getLocalBounds().reduced (12, 8);
    hintLabel.setBounds (getLocalBounds());

    auto titleCol = area.removeFromLeft (76);
    auto titleRow = titleCol.removeFromTop (26);
    deleteButton.setBounds (titleRow.removeFromRight (20).withSizeKeepingCentre (18, 18));
    titleLabel.setBounds (titleRow);
    noteLabel.setBounds (titleCol.removeFromTop (18));

    auto buttonCol = area.removeFromLeft (56);
    enableButton.setBounds (buttonCol.removeFromTop (26).withSizeKeepingCentre (52, 24));
    soloButton.setBounds (buttonCol.removeFromTop (22).withSizeKeepingCentre (48, 20));
    dynButton.setBounds (buttonCol.removeFromTop (24));

    auto comboColumn = area.removeFromLeft (150).reduced (4, 0);
    typeBox.setBounds (comboColumn.removeFromTop (24).reduced (0, 1));
    slopeBox.setBounds (comboColumn.removeFromTop (24).reduced (0, 1));
    chanBox.setBounds (comboColumn.removeFromTop (24).reduced (0, 1));

    area.removeFromLeft (10);
    area.removeFromTop (16); // room for the attached labels

    // One row: Freq / Gain / Q, then (when visible) Range / Thresh / Att / Rel
    // and the Ext + SC Flt toggles.
    const int knobW = juce::jmin (100, (area.getWidth() - 64) / 7);

    freqSlider.setBounds (area.removeFromLeft (knobW).reduced (4, 0));
    gainSlider.setBounds (area.removeFromLeft (knobW).reduced (4, 0));
    qSlider.setBounds (area.removeFromLeft (knobW).reduced (4, 0));

    area.removeFromLeft (8);
    rangeSlider.setBounds (area.removeFromLeft (knobW).reduced (4, 0));
    threshSlider.setBounds (area.removeFromLeft (knobW).reduced (4, 0));
    attSlider.setBounds (area.removeFromLeft (knobW).reduced (4, 0));
    relSlider.setBounds (area.removeFromLeft (knobW).reduced (4, 0));

    auto toggles = area.removeFromLeft (64);
    extButton.setBounds (toggles.removeFromTop (24));
    scfButton.setBounds (toggles.removeFromTop (24));
}

} // namespace eeq
