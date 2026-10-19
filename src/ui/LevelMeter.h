#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../PluginProcessor.h"
#include "LookAndFeel.h"

namespace eeq
{

// Vertical stereo peak meter with a click-to-reset peak readout.
class LevelMeter : public juce::Component,
                   private juce::Timer
{
public:
    explicit LevelMeter (EeqAudioProcessor& p) : proc (p)
    {
        setOpaque (true);
        startTimerHz (30);
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (colors::panel);

        auto area = getLocalBounds().reduced (4);
        auto readout = area.removeFromTop (18);

        g.setColour (peakHoldDb > 0.0f ? colors::warn : colors::textDim);
        g.setFont (juce::Font (juce::FontOptions (11.0f)));
        const auto text = peakHoldDb <= -80.0f
                            ? juce::String ("-inf")
                            : juce::String (peakHoldDb, 1);
        g.drawText (text, readout, juce::Justification::centred);

        area.removeFromTop (2);
        const int barW = (area.getWidth() - 2) / 2;
        drawBar (g, area.removeFromLeft (barW), levelDb[0]);
        area.removeFromLeft (2);
        drawBar (g, area.removeFromLeft (barW), levelDb[1]);
    }

    void mouseDown (const juce::MouseEvent&) override { peakHoldDb = -100.0f; }

private:
    static constexpr float minDb = -60.0f, maxDb = 6.0f;

    void drawBar (juce::Graphics& g, juce::Rectangle<int> r, float db) const
    {
        g.setColour (colors::panelRaised);
        g.fillRect (r);

        const float norm = juce::jlimit (0.0f, 1.0f, (db - minDb) / (maxDb - minDb));
        auto fill = r.withTop (r.getBottom() - (int) (norm * (float) r.getHeight()));

        // Family metering: level green, warn red above 0 dBFS (matches
        // Meridian's meters; the hot zone brightens on the way up).
        g.setColour (db > 0.0f ? colors::warn
                               : db > -12.0f ? colors::level.brighter (0.25f)
                                             : colors::level);
        g.fillRect (fill);
    }

    void timerCallback() override
    {
        const float newPeaks[2] = { proc.peakLeft.exchange (0.0f),
                                    proc.peakRight.exchange (0.0f) };

        for (int ch = 0; ch < 2; ++ch)
        {
            const float db = juce::Decibels::gainToDecibels (newPeaks[ch], -100.0f);
            levelDb[ch] = db > levelDb[ch] ? db : juce::jmax (-100.0f, levelDb[ch] - 2.0f);
            peakHoldDb = juce::jmax (peakHoldDb, db);
        }
        repaint();
    }

    EeqAudioProcessor& proc;
    float levelDb[2] = { -100.0f, -100.0f };
    float peakHoldDb = -100.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LevelMeter)
};

} // namespace eeq
