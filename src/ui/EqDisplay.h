#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_dsp/juce_dsp.h>
#include "../PluginProcessor.h"

namespace eeq
{

// The main EQ view: spectrum analyzer, summed EQ curve, draggable band handles.
//
// Interactions:
//  - double-click empty space: create a band at that frequency/gain
//  - drag a handle: frequency / gain
//  - mouse wheel over a handle (or selected band): Q
//  - double-click a handle: text entry for freq/gain/Q
//  - Alt+hold a handle: intelligent solo
//  - right-click a handle: shape/slope/channel, copy/paste, snap-to-note, delete
//  - right-click empty space: preset copy/paste, analyzer options
//  - grab dot on a spectrum peak: drag to create a band right on the peak
//  - sketch mode: draw a curve, bands are fitted to it on release
class EqDisplay : public juce::Component,
                  private juce::Timer
{
public:
    explicit EqDisplay (EeqAudioProcessor& p);

    std::function<void (int)> onSelectionChanged; // -1 = nothing selected
    std::function<void()> onSketchFinished;
    int getSelectedBand() const noexcept { return selectedBand; }

    void deleteBand (int band); // removes the band from the display entirely

    void setDisplayRange (float plusMinusDb); // persisted in the plugin state
    float getDisplayRange() const noexcept { return displayRange; }

    void setSketchMode (bool shouldSketch);
    bool isSketchMode() const noexcept { return sketchMode; }

    // Opens the analyzer options menu anchored to a header button (the same
    // menu is also reachable via right-click on the display).
    void showAnalyzerMenu (juce::Component& anchor);

    // EQ Match: capture averaged input/reference spectra, then fit bands to
    // the difference. The reference is the sidechain input or a spectrum
    // pasted from another instance ("Copy Spectrum" on the source instance).
    void showMatchMenu (juce::Component& anchor);

    void paint (juce::Graphics& g) override;
    void resized() override {}

    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    static juce::Colour bandColour (int bandIndex);

private:
    static constexpr int fftOrder = 12;
    static constexpr int fftSize = 1 << fftOrder;
    static constexpr float displayMinFreq = 10.0f, displayMaxFreq = 30000.0f;
    static constexpr float analyzerMinDb = -90.0f, analyzerMaxDb = 10.0f;

    // Rolling FFT state for one analyzer source (pre or post).
    struct Spectrum
    {
        std::array<float, fftSize> ring {};        // most recent fftSize samples
        std::array<float, fftSize * 2> fftData {};
        std::array<float, fftSize / 2> levelsDb;   // smoothed display levels
        bool hasData = false;

        Spectrum() { levelsDb.fill (-120.0f); }
    };

    void timerCallback() override;
    void drainFifo (AnalyzerFifo& fifo, Spectrum& s);
    void runFFT (Spectrum& s);

    float freqToX (float freq) const;
    float xToFreq (float x) const;
    float dbToY (float db) const;
    float yToDb (float y) const;
    float spectrumBinY (const Spectrum& s, int bin) const;

    juce::RangedAudioParameter& param (int band, const char* suffix) const;
    float paramValue (int band, const char* suffix) const;
    void setParamValue (int band, const char* suffix, float plainValue) const;

    bool bandActive (int band) const;
    bool bandEnabled (int band) const;
    juce::Point<float> handlePosition (int band) const;
    int hitTestHandles (juce::Point<float> pos) const;
    void selectBand (int band);
    int createBand (float freq, float gainDb, float q);
    int findFreeBand() const;

    void showBandMenu (int band);
    void showDisplayMenu();
    void showValueEditor (int band);
    juce::PopupMenu buildAnalyzerMenu() const;
    void handleAnalyzerResult (int result);

    void copyBandToClipboard (int band) const;
    std::unique_ptr<juce::XmlElement> getClipboardBand() const;
    void applyBandXml (int band, const juce::XmlElement& xml) const;
    void copyPresetToClipboard() const;
    void pastePresetFromClipboard() const;

    void updateGrabTarget (juce::Point<float> mousePos);
    void fitSketchToBands();

    // EQ Match internals.
    void startMatchCapture();
    void applyMatch();
    void copySpectrumToClipboard() const;
    bool pasteReferenceSpectrum();
    bool hasPastedReferenceOnClipboard() const;
    float capturedDbAt (const double* power, int frames, double binHz,
                        float loFreq, float hiFreq) const;
    float pastedDbAt (float loFreq, float hiFreq) const;

    void buildAnalyzerPath (const Spectrum& s, juce::Path& path) const;
    void paintGrid (juce::Graphics& g) const;
    void paintCurves (juce::Graphics& g) const;
    void paintHandles (juce::Graphics& g) const;
    void paintOverlays (juce::Graphics& g) const;

    const Spectrum* grabSource() const;

    EeqAudioProcessor& proc;
    juce::dsp::FFT fft { fftOrder };
    juce::dsp::WindowingFunction<float> window { fftSize,
        juce::dsp::WindowingFunction<float>::hann };

    Spectrum preSpec, postSpec, extSpec;
    std::vector<float> popBuffer;

    // Analyzer display options (empty-area right-click menu).
    bool showPre = true, showPost = true, showExt = false, frozen = false;
    bool showCollisions = false;

    void paintCollisions (juce::Graphics& g) const;
    float releasePerFrame = 1.5f; // dB per frame

    float displayRange = 30.0f;

    int selectedBand = -1, hoveredBand = -1, draggedBand = -1;
    bool dragging = false, soloHeld = false;

    int grabBin = -1;              // spectrum-grab target, -1 = none
    juce::Point<float> grabPoint;

    bool sketchMode = false;
    juce::Array<juce::Point<float>> sketchPoints;

    // EQ Match capture state: linear power accumulated per bin while capturing.
    bool matchCapturing = false;
    bool matchUsePasted = false;
    std::array<double, fftSize / 2> matchAccIn {}, matchAccExt {};
    int matchFramesIn = 0, matchFramesExt = 0;
    std::vector<float> pastedRefDb;   // reference pasted from another instance
    double pastedRefSr = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EqDisplay)
};

} // namespace eeq
