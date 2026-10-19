#pragma once

#include <juce_opengl/juce_opengl.h>
#include "PluginProcessor.h"
#include "ui/LookAndFeel.h"
#include "ui/EqDisplay.h"
#include "ui/BandPanel.h"
#include "ui/LevelMeter.h"
#include "ui/PresetBrowser.h"

class EeqAudioProcessorEditor : public juce::AudioProcessorEditor,
                                private juce::Timer
{
public:
    explicit EeqAudioProcessorEditor (EeqAudioProcessor&);
    ~EeqAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    void timerCallback() override; // latency readout, preset name, A/B labels
    EeqAudioProcessor& proc;

    eeq::LookAndFeel lookAndFeel;

    eeq::EqDisplay display;
    eeq::BandPanel bandPanel;
    eeq::LevelMeter meter;

    // Header.
    juce::Rectangle<int> logoArea;   // product mark, painted in paint()
    juce::Label titleLabel { {}, "EQUINOX" };
    juce::TextButton sketchButton { "Sketch" };
    juce::TextButton abButton { "A" }, copyAbButton { "A>B" };
    juce::TextButton prevPresetButton, presetNameButton, nextPresetButton;
    juce::TextButton phaseButton { juce::CharPointer_UTF8 ("\xc3\x98") }; // polarity
    juce::Slider outGainSlider;
    juce::Label outGainLabel { {}, "Output" }, latencyLabel;

    void updateAbButtons();

    // Control strip above the display, in labelled groups. The captions and
    // panels are painted in paint() from these rects.
    struct ControlGroup
    {
        juce::String caption;
        juce::Rectangle<int> area;
    };
    std::vector<ControlGroup> controlGroups;

    // ENGINE group.
    juce::ComboBox phaseModeBox, lpQualityBox, characterBox;
    // LEVEL group.
    juce::TextButton autoGainButton { "AUTO GAIN" };
    juce::Slider gainScaleSlider;
    juce::Label gainScaleLabel { {}, "Scale" };
    // SPECTRAL group.
    juce::TextButton specButton { "ON" };
    juce::Slider specAmountSlider, specSensSlider;
    juce::Label specAmountLabel { {}, "Amt" }, specSensLabel { {}, "Sens" };
    // ANALYZER group.
    juce::TextButton analyzerButton { "Analyzer" };
    juce::ComboBox rangeBox;
    juce::TextButton matchButton { "Match" };

    juce::TextButton midiButton { "MIDI" };
    void showMidiMenu();

    // Settings menu: GPU rendering, help hints, preset folder. Drawn gear
    // icon — the U+2699 glyph is at the mercy of system font fallback.
    juce::ShapeButton settingsButton { "settings", juce::Colours::grey,
                                       juce::Colours::white, juce::Colours::white };
    void showSettingsMenu();

    // GPU-accelerated rendering (persisted in the plugin state).
    juce::OpenGLContext glContext;
    bool glAttached = false;
    void setGpuEnabled (bool enabled, bool save);

    // Help hints: hover tooltips on every control (persisted, on by default).
    bool hintsEnabled = false;
    std::unique_ptr<juce::TooltipWindow> tooltipWindow;
    void setHintsEnabled (bool enabled, bool save);

    // Preset browser overlay (shared design with Meridian).
    eeq::PresetBrowser presetBrowser { proc.presets };

    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>
        outGainAtt, gainScaleAtt, specAmountAtt, specSensAtt;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>
        phaseAtt, autoGainAtt, specOnAtt;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment>
        phaseModeAtt, lpQualityAtt, characterAtt;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EeqAudioProcessorEditor)
};
