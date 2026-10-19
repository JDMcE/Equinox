#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../PluginProcessor.h"
#include "../util/NoteNames.h"
#include "LookAndFeel.h"

namespace eeq
{

// Small freq/gain/Q text-entry form shown in a CallOutBox when a band
// handle is double-clicked. Enter applies and closes; Esc closes.
class BandValueEditor : public juce::Component
{
public:
    BandValueEditor (EeqAudioProcessor& p, int bandIndex) : proc (p), band (bandIndex)
    {
        auto initRow = [this] (juce::Label& label, const juce::String& name,
                               juce::TextEditor& editor, const juce::String& value)
        {
            label.setText (name, juce::dontSendNotification);
            label.setFont (juce::Font (juce::FontOptions (12.0f)));
            label.setColour (juce::Label::textColourId, colors::textDim);
            addAndMakeVisible (label);

            editor.setText (value, juce::dontSendNotification);
            editor.setJustification (juce::Justification::centredLeft);
            editor.setSelectAllWhenFocused (true);
            editor.onReturnKey = [this] { applyAndClose(); };
            editor.onEscapeKey = [this] { close(); };
            addAndMakeVisible (editor);
        };

        const float freq = value ("freq"), gain = value ("gain"), q = value ("q");

        initRow (freqLabel, "Freq", freqEditor, juce::String (freq, freq < 100.0f ? 1 : 0));
        initRow (gainLabel, "Gain", gainEditor, juce::String (gain, 1));
        initRow (qLabel, "Q", qEditor, juce::String (q, 2));

        noteLabel.setText (freqToNoteName (freq), juce::dontSendNotification);
        noteLabel.setFont (juce::Font (juce::FontOptions (11.0f)));
        noteLabel.setColour (juce::Label::textColourId, colors::textDim.withAlpha (0.8f));
        addAndMakeVisible (noteLabel);

        setSize (210, 96);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (6, 4);
        const int rowH = area.getHeight() / 3;

        auto layoutRow = [&] (juce::Label& l, juce::TextEditor& e, juce::Label* extra)
        {
            auto row = area.removeFromTop (rowH).reduced (0, 3);
            l.setBounds (row.removeFromLeft (40));
            if (extra != nullptr)
                extra->setBounds (row.removeFromRight (62));
            e.setBounds (row);
        };

        layoutRow (freqLabel, freqEditor, &noteLabel);
        layoutRow (gainLabel, gainEditor, nullptr);
        layoutRow (qLabel, qEditor, nullptr);
    }

    void visibilityChanged() override
    {
        if (isVisible())
            freqEditor.grabKeyboardFocus();
    }

private:
    float value (const char* suffix) const
    {
        return proc.apvts.getRawParameterValue (bandParamId (band, suffix))->load();
    }

    void apply (const char* suffix, float plainValue) const
    {
        auto* p = proc.apvts.getParameter (bandParamId (band, suffix));
        p->setValueNotifyingHost (p->convertTo0to1 (plainValue));
    }

    void applyAndClose()
    {
        const double freq = parseFreqText (freqEditor.getText());
        if (freq > 0.0)
            apply ("freq", juce::jlimit (minFreq, maxFreq, (float) freq));

        if (gainEditor.getText().trim().isNotEmpty())
            apply ("gain", juce::jlimit (-maxBandGain, maxBandGain,
                                         (float) gainEditor.getText().getDoubleValue()));

        const double q = qEditor.getText().getDoubleValue();
        if (q > 0.0)
            apply ("q", juce::jlimit (minQ, maxQ, (float) q));

        close();
    }

    void close()
    {
        if (auto* box = findParentComponentOfClass<juce::CallOutBox>())
            box->dismiss();
    }

    EeqAudioProcessor& proc;
    int band;

    juce::Label freqLabel, gainLabel, qLabel, noteLabel;
    juce::TextEditor freqEditor, gainEditor, qEditor;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BandValueEditor)
};

} // namespace eeq
