#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include "Parameters.h"
#include "presets/PresetManager.h"
#include "dsp/EqBand.h"
#include "dsp/AnalyzerFifo.h"
#include "dsp/LinearPhaseEngine.h"
#include "dsp/SpectralDynamics.h"

class EeqAudioProcessor : public juce::AudioProcessor
{
public:
    EeqAudioProcessor();
    ~EeqAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Equinox"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;
    juce::UndoManager undoManager;

    // Applies a whole state tree (preset load / host session restore):
    // migration + MIDI-map rebuild included. Message thread only.
    void applyLoadedState (const juce::ValueTree& state);

    // Preset ecosystem (ported from Meridian): factory + user presets with
    // metadata, favorites, import/export.
    eeq::PresetManager presets { apvts, eeq::makePresetConfig(),
        [this] (const juce::ValueTree& s) { applyLoadedState (s); } };

    eeq::AnalyzerFifo preFifo, postFifo, extFifo;

    // Peak levels since last read by the GUI (read with exchange (0)).
    std::atomic<float> peakLeft { 0.0f }, peakRight { 0.0f };

    // Intelligent solo: band index to audition, -1 = off. Set by the GUI.
    std::atomic<int> soloBand { -1 };

    // Current dynamic gain offset per band in dB, for the display.
    std::array<std::atomic<float>, eeq::numBands> dynOffsetUI {};

    bool sidechainConnected() const noexcept { return scWasConnected; }

    // Normally driven by an internal timer; public so offline tests can pump
    // kernel rebuilds / auto gain without a message loop.
    void messageThreadTick();

    // A/B comparison (call from the message thread only).
    void toggleAB();
    void copyCurrentToOtherSlot();
    bool isSlotB() const noexcept { return slotIsB; }

    // MIDI Learn ----------------------------------------------------------
    // Arm with a target index (eeq::MidiTarget); the next CC received is
    // mapped to it. -1 = not armed.
    std::atomic<int> midiLearnTarget { -1 };

    // Band currently selected in the editor; "selected band" targets drive it.
    std::atomic<int> activeBandUI { 0 };

    int getCCMapping (int cc) const noexcept
    {
        return cc >= 0 && cc < 128 ? ccTargets[(size_t) cc].load() : -1;
    }
    void setCCMapping (int cc, int target); // message thread; target -1 removes
    void clearCCMappings();

private:
    struct BandParams
    {
        std::atomic<float>* on = nullptr;
        std::atomic<float>* type = nullptr;
        std::atomic<float>* freq = nullptr;
        std::atomic<float>* gain = nullptr;
        std::atomic<float>* q = nullptr;
        std::atomic<float>* slope = nullptr;
        std::atomic<float>* chan = nullptr;
        std::atomic<float>* dyn = nullptr;
        std::atomic<float>* dynRange = nullptr;
        std::atomic<float>* thresh = nullptr;
        std::atomic<float>* att = nullptr;
        std::atomic<float>* rel = nullptr;
        std::atomic<float>* ext = nullptr;
        std::atomic<float>* scf = nullptr;
    };

    // Per-band dynamic EQ runtime state: detection filter + envelope.
    struct DynState
    {
        eeq::BiquadCoeffs scCoeffs;
        double z1 = 0.0, z2 = 0.0;   // detection biquad (TDF2)
        float env = 0.0f;            // linear envelope

        // Cache key so detection coefficients are only recomputed on change.
        float scFreq = -1.0f, scQ = -1.0f;
        int scType = -1;
        double scRate = 0.0;

        void reset() { z1 = z2 = 0.0; env = 0.0f; scFreq = -1.0f; }
    };

    // Advances smoothers and updates band coefficients for a block of
    // numSamples at the host rate, then processes procSamples at procRate
    // (differs when oversampling). scL/scR point at the sidechain signal at
    // the host rate (nullptr when not connected).
    void processBands (float* l, float* r, int numSamples, int procSamples, double procRate,
                       const float* scL, const float* scR);

    struct Ticker : juce::Timer
    {
        explicit Ticker (EeqAudioProcessor& o) : owner (o) {}
        void timerCallback() override { owner.messageThreadTick(); }
        EeqAudioProcessor& owner;
    };

    std::array<eeq::EqBand, eeq::numBands> bands;
    std::array<BandParams, eeq::numBands> bandParams;
    std::array<DynState, eeq::numBands> dynStates;
    bool scWasConnected = false;

    eeq::EqBand soloFilter;
    juce::ValueTree altState;
    bool slotIsB = false;

    eeq::LinearPhaseEngine linearEngine;
    eeq::SpectralDynamics spectral;
    bool spectralWasOn = false;
    std::unique_ptr<juce::dsp::Oversampling<float>> oversampler;
    Ticker ticker { *this };

    std::atomic<float> autoGainTargetDb { 0.0f };
    juce::SmoothedValue<float> autoGainSmooth;
    uint64_t lastSnapshotHash = 0;
    int lastLatency = -1;
    bool kernelsBuilt = false;

    std::array<juce::SmoothedValue<float>, eeq::numBands> freqSmooth, gainSmooth, qSmooth;
    juce::SmoothedValue<float> outGainSmooth;

    std::atomic<float>* outGainParam = nullptr;
    std::atomic<float>* phaseInvParam = nullptr;
    std::atomic<float>* phaseModeParam = nullptr;
    std::atomic<float>* lpQualityParam = nullptr;
    std::atomic<float>* characterParam = nullptr;
    std::atomic<float>* autoGainParam = nullptr;
    std::atomic<float>* gainScaleParam = nullptr;
    std::atomic<float>* specOnParam = nullptr;
    std::atomic<float>* specAmountParam = nullptr;
    std::atomic<float>* specSensParam = nullptr;

    juce::AudioBuffer<float> monoScratch;

    // MIDI Learn: CC number -> target index (-1 = unmapped). The map is
    // mirrored into the state tree ("midimap" property) for persistence.
    std::array<std::atomic<int>, 128> ccTargets;
    std::atomic<bool> midiMapDirty { false };
    void handleMidi (const juce::MidiBuffer& midi);
    void applyCC (int target, float norm01);
    void rebuildMidiMapFromState();
    juce::String midiMapToString() const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EeqAudioProcessor)
};
