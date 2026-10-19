#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace eeq
{

constexpr int numBands = 24;

// Filter shapes. Order must match the choice-parameter string list below.
enum class FilterType
{
    bell = 0,
    lowShelf,
    highShelf,
    lowCut,
    highCut,
    notch,
    bandPass,
    tiltShelf,
    flatTilt,
    allPass
};

// Per-band channel routing.
enum class ChannelMode
{
    stereo = 0,
    left,
    right,
    mid,
    side
};

// Slope choices for cut filters, in dB/oct. Index maps to filter order via slopeOrders.
inline const juce::StringArray slopeChoices { "6 dB/oct", "12 dB/oct", "18 dB/oct", "24 dB/oct",
                                              "36 dB/oct", "48 dB/oct", "72 dB/oct", "96 dB/oct",
                                              "Brickwall" };
constexpr int slopeOrders[] = { 1, 2, 3, 4, 6, 8, 12, 16, 32 };
constexpr int defaultSlopeIndex = 1; // 12 dB/oct
constexpr int maxStagesPerBand = 16; // brickwall order 32 -> 16 biquads

// Processing modes.
enum class PhaseMode { zeroLatency = 0, naturalPhase, linearPhase };
enum class CharacterMode { clean = 0, gentle, warm };

inline const juce::StringArray phaseModeChoices { "Zero Latency", "Natural Phase", "Linear Phase" };
inline const juce::StringArray lpQualityChoices { "Low", "Medium", "High", "Very High", "Maximum" };
constexpr int lpFirLengths[] = { 1024, 2048, 4096, 8192, 16384 };
constexpr int maxFirLength = 16384;
inline const juce::StringArray characterChoices { "Clean", "Gentle", "Warm" };

inline const juce::StringArray typeChoices { "Bell", "Low Shelf", "High Shelf", "Low Cut", "High Cut",
                                             "Notch", "Band Pass", "Tilt Shelf", "Flat Tilt", "All Pass" };

inline const juce::StringArray channelChoices { "Stereo", "Left", "Right", "Mid", "Side" };

constexpr float minFreq = 10.0f, maxFreq = 30000.0f;
constexpr float minQ = 0.025f, maxQ = 40.0f;
constexpr float maxBandGain = 30.0f;

// Parameter IDs -----------------------------------------------------------

inline juce::String bandParamId (int band, const char* suffix)
{
    return "b" + juce::String (band) + "_" + suffix;
}

// Whether a band exists at all, as opposed to "on" which bypasses a band that
// stays visible (greyed out). Not automatable, so it lives in the state tree
// as a property rather than a parameter.
inline bool isBandActive (const juce::ValueTree& state, int band)
{
    return (bool) state.getProperty (bandParamId (band, "active"), false);
}

inline void setBandActive (juce::ValueTree& state, int band, bool shouldBeActive,
                           juce::UndoManager* um)
{
    state.setProperty (bandParamId (band, "active"), shouldBeActive, um);
}

// States saved before the active flag existed encode existence in "on":
// give any band without an active property one derived from its on value.
inline void migrateBandActiveFlags (juce::ValueTree& state)
{
    for (int i = 0; i < numBands; ++i)
    {
        if (state.hasProperty (bandParamId (i, "active")))
            continue;

        const auto param = state.getChildWithProperty ("id", bandParamId (i, "on"));
        const bool on = param.isValid()
                     && (double) param.getProperty ("value", 0.0) >= 0.5;
        state.setProperty (bandParamId (i, "active"), on, nullptr);
    }
}

inline const juce::String outGainId    = "out_gain";
inline const juce::String phaseInvId   = "phase_inv";
inline const juce::String phaseModeId  = "phase_mode";
inline const juce::String lpQualityId  = "lp_quality";
inline const juce::String characterId  = "character";
inline const juce::String autoGainId   = "auto_gain";
inline const juce::String gainScaleId  = "gain_scale";
inline const juce::String specOnId     = "spec_on";
inline const juce::String specAmountId = "spec_amount";
inline const juce::String specSensId   = "spec_sens";

// MIDI Learn targets. "Selected band" targets follow the band that is active
// in the editor, so one hardware knob can drive whichever band is selected.
inline const juce::StringArray midiTargetNames { "Selected Band Freq", "Selected Band Gain",
                                                 "Selected Band Q", "Output Gain", "Gain Scale" };
constexpr int numMidiTargets = 5;
enum class MidiTarget { selFreq = 0, selGain, selQ, outGain, gainScale };

// True for shapes whose gain parameter is meaningful.
inline bool typeHasGain (FilterType t)
{
    return t == FilterType::bell || t == FilterType::lowShelf || t == FilterType::highShelf
        || t == FilterType::tiltShelf || t == FilterType::flatTilt;
}

// Dynamic EQ is available for Bell and Shelf shapes.
inline bool typeSupportsDynamics (FilterType t)
{
    return t == FilterType::bell || t == FilterType::lowShelf || t == FilterType::highShelf;
}

inline bool typeHasSlope (FilterType t)
{
    return t == FilterType::lowCut || t == FilterType::highCut;
}

inline juce::String freqToText (float v, int)
{
    return v < 100.0f ? juce::String (v, 1) : juce::String ((int) std::lround (v));
}

inline juce::String dbToText (float v, int) { return juce::String (v, 1); }
inline juce::String qToText (float v, int)  { return juce::String (v, v < 0.995f ? 3 : 2); }

inline juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;

    auto freqAttr = AudioParameterFloatAttributes().withLabel ("Hz")
                        .withStringFromValueFunction (freqToText);
    auto dbAttr = AudioParameterFloatAttributes().withLabel ("dB")
                      .withStringFromValueFunction (dbToText);
    auto qAttr = AudioParameterFloatAttributes().withStringFromValueFunction (qToText);

    auto freqRange = NormalisableRange<float> (minFreq, maxFreq);
    freqRange.setSkewForCentre (1000.0f);

    auto qRange = NormalisableRange<float> (minQ, maxQ);
    qRange.setSkewForCentre (1.0f);

    for (int i = 0; i < numBands; ++i)
    {
        auto num = String (i + 1);

        layout.add (std::make_unique<AudioParameterBool> (
            ParameterID { bandParamId (i, "on"), 1 }, "Band " + num + " Enable", false));

        layout.add (std::make_unique<AudioParameterChoice> (
            ParameterID { bandParamId (i, "type"), 1 }, "Band " + num + " Type", typeChoices, 0));

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { bandParamId (i, "freq"), 1 }, "Band " + num + " Freq", freqRange, 1000.0f,
            freqAttr));

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { bandParamId (i, "gain"), 1 }, "Band " + num + " Gain",
            NormalisableRange<float> (-maxBandGain, maxBandGain, 0.0f), 0.0f,
            dbAttr));

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { bandParamId (i, "q"), 1 }, "Band " + num + " Q", qRange, 1.0f, qAttr));

        layout.add (std::make_unique<AudioParameterChoice> (
            ParameterID { bandParamId (i, "slope"), 1 }, "Band " + num + " Slope",
            slopeChoices, defaultSlopeIndex));

        layout.add (std::make_unique<AudioParameterChoice> (
            ParameterID { bandParamId (i, "chan"), 1 }, "Band " + num + " Channel",
            channelChoices, 0));

        // Dynamic EQ.
        layout.add (std::make_unique<AudioParameterBool> (
            ParameterID { bandParamId (i, "dyn"), 1 }, "Band " + num + " Dynamic", false));

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { bandParamId (i, "dynrange"), 1 }, "Band " + num + " Dyn Range",
            NormalisableRange<float> (-maxBandGain, maxBandGain, 0.0f), 0.0f, dbAttr));

        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { bandParamId (i, "thresh"), 1 }, "Band " + num + " Threshold",
            NormalisableRange<float> (-60.0f, 0.0f, 0.0f), -24.0f, dbAttr));

        auto attRange = NormalisableRange<float> (0.1f, 200.0f);
        attRange.setSkewForCentre (15.0f);
        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { bandParamId (i, "att"), 1 }, "Band " + num + " Attack",
            attRange, 15.0f, AudioParameterFloatAttributes().withLabel ("ms")
                                 .withStringFromValueFunction (dbToText)));

        auto relRange = NormalisableRange<float> (5.0f, 2000.0f);
        relRange.setSkewForCentre (150.0f);
        layout.add (std::make_unique<AudioParameterFloat> (
            ParameterID { bandParamId (i, "rel"), 1 }, "Band " + num + " Release",
            relRange, 150.0f, AudioParameterFloatAttributes().withLabel ("ms")
                                  .withStringFromValueFunction (dbToText)));

        layout.add (std::make_unique<AudioParameterBool> (
            ParameterID { bandParamId (i, "ext"), 1 }, "Band " + num + " Ext Trigger", false));

        layout.add (std::make_unique<AudioParameterBool> (
            ParameterID { bandParamId (i, "scf"), 1 }, "Band " + num + " SC Filter", true));
    }

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { outGainId, 1 }, "Output Gain",
        NormalisableRange<float> (-36.0f, 36.0f, 0.0f), 0.0f,
        dbAttr));

    layout.add (std::make_unique<AudioParameterBool> (
        ParameterID { phaseInvId, 1 }, "Phase Invert", false));

    layout.add (std::make_unique<AudioParameterChoice> (
        ParameterID { phaseModeId, 1 }, "Phase Mode", phaseModeChoices, 0));

    layout.add (std::make_unique<AudioParameterChoice> (
        ParameterID { lpQualityId, 1 }, "Linear Phase Quality", lpQualityChoices, 2));

    layout.add (std::make_unique<AudioParameterChoice> (
        ParameterID { characterId, 1 }, "Character", characterChoices, 0));

    layout.add (std::make_unique<AudioParameterBool> (
        ParameterID { autoGainId, 1 }, "Auto Gain", false));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { gainScaleId, 1 }, "Gain Scale",
        NormalisableRange<float> (0.0f, 2.0f, 0.0f), 1.0f,
        AudioParameterFloatAttributes().withStringFromValueFunction (
            [] (float v, int) { return juce::String ((int) std::lround (v * 100.0f)) + "%"; })));

    // Spectral dynamics.
    layout.add (std::make_unique<AudioParameterBool> (
        ParameterID { specOnId, 1 }, "Spectral Dynamics", false));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { specAmountId, 1 }, "Spectral Amount",
        NormalisableRange<float> (0.0f, 24.0f, 0.0f), 6.0f, dbAttr));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { specSensId, 1 }, "Spectral Sensitivity",
        NormalisableRange<float> (0.0f, 24.0f, 0.0f), 6.0f, dbAttr));

    return layout;
}

} // namespace eeq
