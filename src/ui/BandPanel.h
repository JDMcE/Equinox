#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include "../PluginProcessor.h"

namespace eeq
{

// Bottom strip showing the controls of the currently selected band.
class BandPanel : public juce::Component,
                  private juce::Timer
{
public:
    explicit BandPanel (EeqAudioProcessor& p);

    std::function<void (int)> onDelete; // asks the owner to delete a band

    void setBand (int bandIndex); // -1 = none
    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    void timerCallback() override;
    void updateDynVisibility();

    // Solos the band while the mouse button is held down.
    struct SoloButton : juce::TextButton
    {
        using juce::TextButton::TextButton;
        std::function<void (bool)> onHold;

        void mouseDown (const juce::MouseEvent& e) override
        {
            if (onHold) onHold (true);
            juce::TextButton::mouseDown (e);
        }

        void mouseUp (const juce::MouseEvent& e) override
        {
            if (onHold) onHold (false);
            juce::TextButton::mouseUp (e);
        }
    };

    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboAttachment  = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    EeqAudioProcessor& proc;
    int band = -1;

    juce::ToggleButton enableButton { "On" };
    SoloButton soloButton { "Solo" };
    juce::TextButton deleteButton { juce::CharPointer_UTF8 ("\xe2\x9c\x95") }; // ✕
    juce::ComboBox typeBox, slopeBox, chanBox;
    juce::Slider freqSlider, gainSlider, qSlider;
    juce::Label freqLabel { {}, "Freq" }, gainLabel { {}, "Gain" }, qLabel { {}, "Q" },
                titleLabel, noteLabel,
                hintLabel { {}, "Double-click the display to add an EQ band" };

    // Dynamics row.
    juce::ToggleButton dynButton { "Dyn" }, extButton { "Ext" }, scfButton { "SC Flt" };
    juce::Slider rangeSlider, threshSlider, attSlider, relSlider;
    juce::Label rangeLabel { {}, "Range" }, threshLabel { {}, "Thresh" },
                attLabel { {}, "Att" }, relLabel { {}, "Rel" };

    std::unique_ptr<ButtonAttachment> enableAtt, dynAtt, extAtt, scfAtt;
    std::unique_ptr<ComboAttachment> typeAtt, slopeAtt, chanAtt;
    std::unique_ptr<SliderAttachment> freqAtt, gainAtt, qAtt,
                                      rangeAtt, threshAtt, attAtt2, relAtt;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BandPanel)
};

} // namespace eeq
