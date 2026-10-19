#include "EqDisplay.h"
#include "BandValueEditor.h"
#include "LookAndFeel.h"
#include "../dsp/FilterCoeffs.h"
#include "../util/NoteNames.h"

namespace eeq
{

namespace
{
    // Const references onto the shared family palette: existing paint code
    // keeps its names, and a runtime palette swap is picked up for free.
    const juce::Colour& backgroundCol = colors::background;
    const juce::Colour& gridCol       = colors::panelRaised;
    const juce::Colour& gridStrongCol = colors::outline;
    const juce::Colour& textCol       = colors::textDim;
    const juce::Colour& curveCol      = colors::curve; // summed EQ curve (family accent)
    const juce::Colour preCol         { 0x30aab2c0 };
    const juce::Colour postCol        { 0x504a9eff };

    const juce::Colour bandPalette[] = {
        juce::Colour (0xffe5686f), juce::Colour (0xffe8a04c), juce::Colour (0xffe8d44c),
        juce::Colour (0xff7fc95c), juce::Colour (0xff4fc9b1), juce::Colour (0xff4fa9e8),
        juce::Colour (0xff8f7fe8), juce::Colour (0xffd97fd0)
    };

    constexpr float handleRadius = 8.0f;
    constexpr int labelFreqs[] = { 20, 30, 40, 50, 100, 200, 300, 500,
                                   1000, 2000, 3000, 5000, 10000, 20000 };

    constexpr const char* bandClipTag = "eeQBand";
    constexpr const char* bandClipSuffixes[] = { "type", "freq", "gain", "q", "slope", "chan" };

    // Band-menu item ids.
    enum MenuIds
    {
        menuDelete = 1, menuCopyBand = 10, menuPasteBand = 11,
        menuSnapToNote = 12, menuResetGain = 13,
        menuCopyPreset = 50, menuPastePreset = 51,
        menuAnalyzerPre = 20, menuAnalyzerPost = 21, menuAnalyzerFreeze = 22,
        menuAnalyzerExt = 23, menuAnalyzerCollisions = 24,
        menuSpeedSlow = 30, menuSpeedMedium = 31, menuSpeedFast = 32,
        menuMatchCapture = 60, menuMatchApply = 61, menuMatchCopy = 62,
        menuMatchPasteRef = 63, menuMatchRefExt = 64, menuMatchRefPasted = 65,
        menuTypeBase = 100, menuSlopeBase = 200, menuChanBase = 300
    };

    constexpr const char* spectrumClipTag = "EquinoxSpectrum";
}

juce::Colour EqDisplay::bandColour (int bandIndex)
{
    return bandPalette[bandIndex % (int) std::size (bandPalette)];
}

EqDisplay::EqDisplay (EeqAudioProcessor& p) : proc (p)
{
    popBuffer.resize (fftSize);
    displayRange = (float) (double) proc.apvts.state.getProperty ("displayRange", 30.0);
    setOpaque (true);
    startTimerHz (30);
}

void EqDisplay::setDisplayRange (float plusMinusDb)
{
    displayRange = plusMinusDb;
    proc.apvts.state.setProperty ("displayRange", (double) plusMinusDb, nullptr);
    repaint();
}

void EqDisplay::setSketchMode (bool shouldSketch)
{
    sketchMode = shouldSketch;
    sketchPoints.clear();
    setMouseCursor (sketchMode ? juce::MouseCursor::CrosshairCursor
                               : juce::MouseCursor::NormalCursor);
    repaint();
}

// Coordinate mapping ---------------------------------------------------------

float EqDisplay::freqToX (float freq) const
{
    const float norm = std::log (freq / displayMinFreq) / std::log (displayMaxFreq / displayMinFreq);
    return norm * (float) getWidth();
}

float EqDisplay::xToFreq (float x) const
{
    const float norm = x / (float) getWidth();
    return displayMinFreq * std::pow (displayMaxFreq / displayMinFreq, norm);
}

float EqDisplay::dbToY (float db) const
{
    return juce::jmap (db, -displayRange, displayRange, (float) getHeight(), 0.0f);
}

float EqDisplay::yToDb (float y) const
{
    return juce::jmap (y, (float) getHeight(), 0.0f, -displayRange, displayRange);
}

// Parameter helpers ----------------------------------------------------------

juce::RangedAudioParameter& EqDisplay::param (int band, const char* suffix) const
{
    auto* p = proc.apvts.getParameter (bandParamId (band, suffix));
    jassert (p != nullptr);
    return *p;
}

float EqDisplay::paramValue (int band, const char* suffix) const
{
    return proc.apvts.getRawParameterValue (bandParamId (band, suffix))->load();
}

void EqDisplay::setParamValue (int band, const char* suffix, float plainValue) const
{
    auto& p = param (band, suffix);
    p.setValueNotifyingHost (p.convertTo0to1 (plainValue));
}

bool EqDisplay::bandActive (int band) const
{
    return isBandActive (proc.apvts.state, band);
}

bool EqDisplay::bandEnabled (int band) const
{
    return paramValue (band, "on") >= 0.5f;
}

// Analyzer -------------------------------------------------------------------

void EqDisplay::timerCallback()
{
    drainFifo (proc.preFifo, preSpec);
    drainFifo (proc.postFifo, postSpec);
    drainFifo (proc.extFifo, extSpec);

    // A deleted band switched on again by host automation comes back into
    // existence, so anything that is processing is always visible.
    for (int i = 0; i < numBands; ++i)
        if (bandEnabled (i) && ! bandActive (i))
            setBandActive (proc.apvts.state, i, true, proc.apvts.undoManager);

    repaint();
}

void EqDisplay::drainFifo (AnalyzerFifo& fifo, Spectrum& s)
{
    bool gotNew = false;

    while (fifo.getNumReady() > 0)
    {
        const int n = fifo.pop (popBuffer.data(), (int) popBuffer.size());
        if (n <= 0)
            break;

        if (frozen)
            continue; // keep draining so the FIFO stays fresh, but hold the display

        if (n >= fftSize)
        {
            std::copy (popBuffer.begin() + (n - fftSize), popBuffer.begin() + n, s.ring.begin());
        }
        else
        {
            std::move (s.ring.begin() + n, s.ring.end(), s.ring.begin());
            std::copy (popBuffer.begin(), popBuffer.begin() + n, s.ring.end() - n);
        }
        gotNew = true;
    }

    if (frozen)
        return;

    if (gotNew)
    {
        s.hasData = true;
        runFFT (s);
    }
    else
    {
        // No audio flowing: let the display fall away.
        for (auto& l : s.levelsDb)
            l = juce::jmax (-120.0f, l - releasePerFrame);
    }
}

void EqDisplay::runFFT (Spectrum& s)
{
    std::copy (s.ring.begin(), s.ring.end(), s.fftData.begin());
    std::fill (s.fftData.begin() + fftSize, s.fftData.end(), 0.0f);

    window.multiplyWithWindowingTable (s.fftData.data(), fftSize);
    fft.performFrequencyOnlyForwardTransform (s.fftData.data());

    // Hann window coherent gain = 0.5; normalise so a full-scale sine reads ~0 dB.
    const float scale = 4.0f / (float) fftSize;

    // EQ Match capture: accumulate linear power (no display ballistics).
    double* matchAcc = nullptr;
    if (matchCapturing)
    {
        if (&s == &preSpec)      { matchAcc = matchAccIn.data();  ++matchFramesIn; }
        else if (&s == &extSpec) { matchAcc = matchAccExt.data(); ++matchFramesExt; }
    }

    for (int bin = 0; bin < fftSize / 2; ++bin)
    {
        const float mag = s.fftData[(size_t) bin] * scale;
        const float db = juce::Decibels::gainToDecibels (mag, -120.0f);

        if (matchAcc != nullptr)
            matchAcc[(size_t) bin] += (double) mag * mag;

        // Fast attack, slow release.
        auto& level = s.levelsDb[(size_t) bin];
        level = db > level ? db : juce::jmax (-120.0f, level - releasePerFrame);
    }
}

float EqDisplay::spectrumBinY (const Spectrum& s, int bin) const
{
    const double sr = proc.getSampleRate() > 0 ? proc.getSampleRate() : 48000.0;
    const float freq = (float) (bin * sr / fftSize);
    const float tilt = 4.5f * std::log2 (juce::jmax (1.0f, freq) / 1000.0f);
    const float db = s.levelsDb[(size_t) bin] + tilt;
    return juce::jmap (juce::jlimit (analyzerMinDb, analyzerMaxDb, db),
                       analyzerMinDb, analyzerMaxDb, (float) getHeight(), 0.0f);
}

void EqDisplay::buildAnalyzerPath (const Spectrum& s, juce::Path& path) const
{
    const double sr = proc.getSampleRate() > 0 ? proc.getSampleRate() : 48000.0;
    const float h = (float) getHeight();
    const int numBins = fftSize / 2;

    path.startNewSubPath (-2.0f, h + 2.0f);
    float lastX = -10.0f;

    for (int bin = 1; bin < numBins; ++bin)
    {
        const float freq = (float) (bin * sr / fftSize);
        if (freq < displayMinFreq)
            continue;
        if (freq > displayMaxFreq)
            break;

        const float x = freqToX (freq);
        if (x - lastX < 1.0f)
            continue;
        lastX = x;

        path.lineTo (x, spectrumBinY (s, bin));
    }

    path.lineTo ((float) getWidth() + 2.0f, h + 2.0f);
    path.closeSubPath();
}

// Painting -------------------------------------------------------------------

void EqDisplay::paint (juce::Graphics& g)
{
    g.fillAll (backgroundCol);
    paintGrid (g);

    if (showPre && preSpec.hasData)
    {
        juce::Path p;
        buildAnalyzerPath (preSpec, p);
        g.setColour (preCol);
        g.fillPath (p);
    }

    if (showPost && postSpec.hasData)
    {
        juce::Path p;
        buildAnalyzerPath (postSpec, p);
        g.setColour (postCol);
        g.fillPath (p);
    }

    if (showExt && extSpec.hasData)
    {
        juce::Path p;
        buildAnalyzerPath (extSpec, p);
        g.setColour (colors::sidechain.withAlpha (0.25f)); // external sidechain trace (family purple)
        g.fillPath (p);
    }

    if (showCollisions && preSpec.hasData && extSpec.hasData)
        paintCollisions (g);

    paintCurves (g);
    paintHandles (g);
    paintOverlays (g);
}

void EqDisplay::paintGrid (juce::Graphics& g) const
{
    g.setFont (juce::Font (juce::FontOptions (11.0f)));

    for (int f : labelFreqs)
    {
        const float x = freqToX ((float) f);
        const bool strong = (f == 100 || f == 1000 || f == 10000);
        g.setColour (strong ? gridStrongCol : gridCol);
        g.drawVerticalLine ((int) x, 0.0f, (float) getHeight());

        g.setColour (textCol);
        const juce::String label = f >= 1000 ? juce::String (f / 1000) + "k" : juce::String (f);
        g.drawText (label, (int) x - 20, getHeight() - 16, 40, 14, juce::Justification::centred);
    }

    const int step = displayRange <= 3.0f ? 1 : displayRange <= 6.0f ? 2
                   : displayRange <= 12.0f ? 4 : 10;

    for (int db = -((int) displayRange - step); db <= (int) displayRange - step; db += step)
    {
        const float y = dbToY ((float) db);
        // The 0 dB line is the display's "equator": a whisper of the accent
        // so the neutral line reads at a glance (and nods to the name).
        g.setColour (db == 0 ? gridStrongCol.interpolatedWith (colors::accent, 0.30f)
                             : gridCol);
        g.drawHorizontalLine ((int) y, 0.0f, (float) getWidth());

        if (db != 0)
        {
            g.setColour (textCol);
            g.drawText (juce::String (db > 0 ? "+" : "") + juce::String (db),
                        4, (int) y - 14, 32, 12, juce::Justification::left);
        }
    }
}

void EqDisplay::paintCurves (juce::Graphics& g) const
{
    const double sr = proc.getSampleRate() > 0 ? proc.getSampleRate() : 48000.0;
    const int w = getWidth();
    if (w < 2)
        return;

    const int numPoints = juce::jmin (512, w);
    const float scale = proc.apvts.getRawParameterValue (gainScaleId)->load();

    // Collect coefficient sets of active bands once. "ghost" mirrors the
    // static curve but with dynamic bands at their current live gain.
    std::vector<BandCoeffs> active;
    std::vector<BandCoeffs> selected;
    std::vector<BandCoeffs> ghost;
    bool anyDynamic = false;

    for (int i = 0; i < numBands; ++i)
    {
        const bool enabled = bandEnabled (i);

        // A disabled band contributes nothing, but the selected one still
        // shows its own (greyed) curve.
        if (! bandActive (i) || (! enabled && i != selectedBand))
            continue;

        const auto type = (FilterType) (int) paramValue (i, "type");
        const float freq = paramValue (i, "freq");
        const float gain = paramValue (i, "gain") * scale;
        const float q = paramValue (i, "q");
        const int slope = (int) paramValue (i, "slope");

        auto c = computeBandCoeffs (type, freq, gain, q, slope, sr);

        if (i == selectedBand)
            selected.push_back (c);

        if (! enabled)
            continue;

        const bool dyn = paramValue (i, "dyn") > 0.5f && typeSupportsDynamics (type)
                      && paramValue (i, "dynrange") != 0.0f;
        if (dyn)
        {
            anyDynamic = true;
            const float offset = proc.dynOffsetUI[(size_t) i].load (std::memory_order_relaxed);
            ghost.push_back (computeBandCoeffs (type, freq, gain + offset, q, slope, sr));
        }
        else
        {
            ghost.push_back (c);
        }

        active.push_back (std::move (c));
    }

    auto buildCurve = [&] (const std::vector<BandCoeffs>& sets, juce::Path& path)
    {
        for (int pt = 0; pt <= numPoints; ++pt)
        {
            const float x = (float) pt / (float) numPoints * (float) w;
            const double freq = juce::jmin ((double) xToFreq (x), sr * 0.499);

            double db = 0.0;
            for (const auto& c : sets)
                db += bandMagnitudeDb (c, freq, sr);

            const float y = dbToY ((float) juce::jlimit (-60.0, 60.0, db));
            if (pt == 0)
                path.startNewSubPath (x, y);
            else
                path.lineTo (x, y);
        }
    };

    if (! selected.empty())
    {
        juce::Path p;
        buildCurve (selected, p);
        g.setColour ((bandEnabled (selectedBand) ? bandColour (selectedBand)
                                                 : juce::Colour (0xff70747e)).withAlpha (0.5f));
        g.strokePath (p, juce::PathStrokeType (1.5f));
    }

    // Animated ghost of the curve as the dynamics currently shape it.
    if (anyDynamic)
    {
        juce::Path p;
        buildCurve (ghost, p);
        g.setColour (curveCol.withAlpha (0.45f));
        g.strokePath (p, juce::PathStrokeType (1.8f));
    }

    juce::Path sum;
    buildCurve (active, sum);
    g.setColour (curveCol);
    g.strokePath (sum, juce::PathStrokeType (2.5f));
}

juce::Point<float> EqDisplay::handlePosition (int band) const
{
    const auto type = (FilterType) (int) paramValue (band, "type");
    const float x = freqToX (paramValue (band, "freq"));
    const float y = typeHasGain (type) ? dbToY (paramValue (band, "gain")) : dbToY (0.0f);
    return { x, y };
}

void EqDisplay::paintHandles (juce::Graphics& g) const
{
    g.setFont (juce::Font (juce::FontOptions (11.0f, juce::Font::bold)));

    for (int i = 0; i < numBands; ++i)
    {
        if (! bandActive (i))
            continue;

        const bool enabled = bandEnabled (i);
        const auto pos = handlePosition (i);
        const auto col = enabled ? bandColour (i) : juce::Colour (0xff5c6069);
        const bool isSelected = (i == selectedBand);
        const bool isHovered = (i == hoveredBand);
        const float radius = handleRadius + (isSelected ? 2.0f : 0.0f);

        g.setColour (isSelected || isHovered ? col : col.withAlpha (0.75f));
        g.fillEllipse (pos.x - radius, pos.y - radius, radius * 2.0f, radius * 2.0f);

        if (isSelected)
        {
            g.setColour (juce::Colours::white.withAlpha (0.9f));
            g.drawEllipse (pos.x - radius, pos.y - radius, radius * 2.0f, radius * 2.0f, 1.5f);
        }

        g.setColour (juce::Colours::black.withAlpha (enabled ? 0.8f : 0.5f));
        g.drawText (juce::String (i + 1), (int) (pos.x - radius), (int) (pos.y - radius),
                    (int) (radius * 2.0f), (int) (radius * 2.0f), juce::Justification::centred);
    }
}

void EqDisplay::paintOverlays (juce::Graphics& g) const
{
    // Spectrum-grab target.
    if (grabBin >= 0 && ! sketchMode)
    {
        g.setColour (juce::Colours::white.withAlpha (0.85f));
        g.drawEllipse (grabPoint.x - 6.0f, grabPoint.y - 6.0f, 12.0f, 12.0f, 1.5f);
        g.setColour (juce::Colours::white.withAlpha (0.35f));
        g.drawVerticalLine ((int) grabPoint.x, grabPoint.y + 6.0f, (float) getHeight());

        g.setFont (juce::Font (juce::FontOptions (11.0f)));
        g.setColour (textCol);
        const double sr = proc.getSampleRate() > 0 ? proc.getSampleRate() : 48000.0;
        const auto freq = grabBin * sr / fftSize;
        g.drawText (freqToText ((float) freq, 0) + " Hz",
                    (int) grabPoint.x + 10, (int) grabPoint.y - 18, 80, 14,
                    juce::Justification::left);
    }

    // Sketch line and hint.
    if (sketchMode)
    {
        g.setFont (juce::Font (juce::FontOptions (14.0f)));
        g.setColour (curveCol.withAlpha (0.8f));
        g.drawText ("Sketch: draw the curve you want, bands are created on release",
                    getLocalBounds().removeFromTop (30), juce::Justification::centred);

        if (sketchPoints.size() > 1)
        {
            juce::Path p;
            p.startNewSubPath (sketchPoints.getReference (0));
            for (int i = 1; i < sketchPoints.size(); ++i)
                p.lineTo (sketchPoints.getReference (i));
            g.setColour (juce::Colours::white.withAlpha (0.8f));
            g.strokePath (p, juce::PathStrokeType (2.0f));
        }
    }

    // Solo badge.
    if (proc.soloBand.load() >= 0)
    {
        auto r = getLocalBounds().removeFromTop (26).removeFromRight (110).reduced (4);
        g.setColour (juce::Colour (0xffe5686f));
        g.fillRoundedRectangle (r.toFloat(), 4.0f);
        g.setColour (juce::Colours::black.withAlpha (0.85f));
        g.setFont (juce::Font (juce::FontOptions (12.0f, juce::Font::bold)));
        g.drawText ("SOLO " + juce::String (proc.soloBand.load() + 1), r,
                    juce::Justification::centred);
    }

    if (frozen)
    {
        g.setColour (juce::Colour (0xff4fa9e8));
        g.setFont (juce::Font (juce::FontOptions (11.0f, juce::Font::bold)));
        g.drawText ("FREEZE", getLocalBounds().removeFromBottom (34).removeFromRight (70),
                    juce::Justification::centred);
    }

    if (matchCapturing)
    {
        auto r = getLocalBounds().removeFromTop (26).removeFromLeft (190).reduced (4);
        g.setColour (juce::Colour (0xff4fc9b1));
        g.fillRoundedRectangle (r.toFloat(), 4.0f);
        g.setColour (juce::Colours::black.withAlpha (0.85f));
        g.setFont (juce::Font (juce::FontOptions (12.0f, juce::Font::bold)));
        g.drawText ("MATCH: capturing spectrum...", r, juce::Justification::centred);
    }
}

// Collision detection: fill the region where both the input (pre) and the
// external sidechain carry significant energy at the same frequencies --
// i.e. where the two signals mask each other.
void EqDisplay::paintCollisions (juce::Graphics& g) const
{
    const double sr = proc.getSampleRate() > 0 ? proc.getSampleRate() : 48000.0;
    const float h = (float) getHeight();
    constexpr float floorDb = -50.0f; // both signals must exceed this

    juce::Path path;
    bool inSegment = false;
    float lastX = -10.0f;

    for (int bin = 1; bin < fftSize / 2; ++bin)
    {
        const float freq = (float) (bin * sr / fftSize);
        if (freq < displayMinFreq)
            continue;
        if (freq > displayMaxFreq)
            break;

        const float x = freqToX (freq);
        if (x - lastX < 1.0f && inSegment)
            continue;

        const float tilt = 4.5f * std::log2 (freq / 1000.0f);
        const float shared = juce::jmin (preSpec.levelsDb[(size_t) bin],
                                         extSpec.levelsDb[(size_t) bin]) + tilt;

        if (shared > floorDb)
        {
            const float y = juce::jmap (juce::jlimit (analyzerMinDb, analyzerMaxDb, shared),
                                        analyzerMinDb, analyzerMaxDb, h, 0.0f);
            if (! inSegment)
            {
                path.startNewSubPath (x, h);
                inSegment = true;
            }
            path.lineTo (x, y);
            lastX = x;
        }
        else if (inSegment)
        {
            path.lineTo (lastX, h);
            path.closeSubPath();
            inSegment = false;
        }
    }

    if (inSegment)
    {
        path.lineTo (lastX, h);
        path.closeSubPath();
    }

    g.setColour (juce::Colour (0x58e5686f)); // translucent red
    g.fillPath (path);
}

// Spectrum grab --------------------------------------------------------------

const EqDisplay::Spectrum* EqDisplay::grabSource() const
{
    if (showPre && preSpec.hasData)
        return &preSpec;
    if (showPost && postSpec.hasData)
        return &postSpec;
    return nullptr;
}

void EqDisplay::updateGrabTarget (juce::Point<float> mousePos)
{
    const int oldBin = grabBin;
    grabBin = -1;

    if (const auto* s = grabSource(); s != nullptr && hoveredBand < 0)
    {
        const double sr = proc.getSampleRate() > 0 ? proc.getSampleRate() : 48000.0;
        const auto binForX = [&] (float x)
        {
            return juce::jlimit (1, fftSize / 2 - 1,
                                 (int) (xToFreq (x) * fftSize / sr));
        };

        // Search the bins within +/- 25 px of the cursor for the loudest one.
        const int lo = binForX (mousePos.x - 25.0f);
        const int hi = binForX (mousePos.x + 25.0f);

        int best = -1;
        float bestDb = -200.0f;
        for (int b = lo; b <= hi; ++b)
        {
            if (s->levelsDb[(size_t) b] > bestDb)
            {
                bestDb = s->levelsDb[(size_t) b];
                best = b;
            }
        }

        if (best > 0)
        {
            const auto peak = juce::Point<float> (freqToX ((float) (best * sr / fftSize)),
                                                  spectrumBinY (*s, best));
            if (peak.getDistanceFrom (mousePos) < 16.0f)
            {
                grabBin = best;
                grabPoint = peak;
            }
        }
    }

    if (grabBin != oldBin)
        repaint();
}

// Interaction ----------------------------------------------------------------

int EqDisplay::hitTestHandles (juce::Point<float> pos) const
{
    int best = -1;
    float bestDist = handleRadius + 4.0f;

    for (int i = 0; i < numBands; ++i)
    {
        if (! bandActive (i))
            continue;

        const float d = pos.getDistanceFrom (handlePosition (i));
        if (d < bestDist)
        {
            bestDist = d;
            best = i;
        }
    }
    return best;
}

void EqDisplay::selectBand (int band)
{
    if (selectedBand == band)
        return;

    selectedBand = band;
    if (onSelectionChanged)
        onSelectionChanged (band);
    repaint();
}

int EqDisplay::findFreeBand() const
{
    for (int i = 0; i < numBands; ++i)
        if (! bandActive (i))
            return i;
    return -1;
}

int EqDisplay::createBand (float freq, float gainDb, float q)
{
    const int i = findFreeBand();
    if (i < 0)
        return -1;

    setParamValue (i, "type", (float) (int) FilterType::bell);
    setParamValue (i, "freq", juce::jlimit (minFreq, maxFreq, freq));
    setParamValue (i, "gain", juce::jlimit (-maxBandGain, maxBandGain, gainDb));
    setParamValue (i, "q", juce::jlimit (minQ, maxQ, q));
    setParamValue (i, "chan", 0.0f);
    setParamValue (i, "slope", (float) defaultSlopeIndex);
    setParamValue (i, "on", 1.0f);
    setBandActive (proc.apvts.state, i, true, proc.apvts.undoManager);

    selectBand (i);
    return i;
}

void EqDisplay::deleteBand (int band)
{
    if (band < 0 || band >= numBands)
        return;

    setParamValue (band, "on", 0.0f);
    setBandActive (proc.apvts.state, band, false, proc.apvts.undoManager);

    if (selectedBand == band)
        selectBand (-1);
    repaint();
}

void EqDisplay::mouseMove (const juce::MouseEvent& e)
{
    const int hit = hitTestHandles (e.position);
    if (hit != hoveredBand)
    {
        hoveredBand = hit;
        if (! sketchMode)
            setMouseCursor (hit >= 0 ? juce::MouseCursor::PointingHandCursor
                                     : juce::MouseCursor::NormalCursor);
        repaint();
    }

    updateGrabTarget (e.position);
}

void EqDisplay::mouseExit (const juce::MouseEvent&)
{
    hoveredBand = -1;
    grabBin = -1;
    repaint();
}

void EqDisplay::mouseDown (const juce::MouseEvent& e)
{
    if (sketchMode)
    {
        sketchPoints.clearQuick();
        sketchPoints.add (e.position);
        return;
    }

    const int hit = hitTestHandles (e.position);

    if (e.mods.isPopupMenu())
    {
        if (hit >= 0)
        {
            selectBand (hit);
            showBandMenu (hit);
        }
        else
        {
            showDisplayMenu();
        }
        return;
    }

    if (hit >= 0)
    {
        selectBand (hit);

        if (e.mods.isAltDown())
        {
            // Intelligent solo while held.
            soloHeld = true;
            proc.soloBand.store (hit);
            return;
        }

        draggedBand = hit;
        dragging = true;
        param (hit, "freq").beginChangeGesture();
        param (hit, "gain").beginChangeGesture();
        return;
    }

    if (grabBin >= 0)
    {
        // Spectrum grab: create a band right on the peak and drag it.
        const double sr = proc.getSampleRate() > 0 ? proc.getSampleRate() : 48000.0;
        const int band = createBand ((float) (grabBin * sr / fftSize), 0.0f, 4.0f);

        if (band >= 0)
        {
            draggedBand = band;
            dragging = true;
            param (band, "freq").beginChangeGesture();
            param (band, "gain").beginChangeGesture();
        }
        return;
    }

    selectBand (-1);
}

void EqDisplay::mouseDrag (const juce::MouseEvent& e)
{
    if (sketchMode)
    {
        if (sketchPoints.isEmpty()
            || e.position.getDistanceFrom (sketchPoints.getLast()) > 3.0f)
        {
            sketchPoints.add (e.position);
            repaint();
        }
        return;
    }

    if (! dragging || draggedBand < 0)
        return;

    const float freq = juce::jlimit (minFreq, maxFreq, xToFreq (e.position.x));
    setParamValue (draggedBand, "freq", freq);

    const auto type = (FilterType) (int) paramValue (draggedBand, "type");
    if (typeHasGain (type))
    {
        const float gain = juce::jlimit (-maxBandGain, maxBandGain, yToDb (e.position.y));
        setParamValue (draggedBand, "gain", gain);
    }
}

void EqDisplay::mouseUp (const juce::MouseEvent&)
{
    if (sketchMode)
    {
        fitSketchToBands();
        setSketchMode (false);
        if (onSketchFinished)
            onSketchFinished();
        return;
    }

    if (soloHeld)
    {
        proc.soloBand.store (-1);
        soloHeld = false;
        repaint();
    }

    if (dragging && draggedBand >= 0)
    {
        param (draggedBand, "freq").endChangeGesture();
        param (draggedBand, "gain").endChangeGesture();
    }
    dragging = false;
    draggedBand = -1;
}

void EqDisplay::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (sketchMode)
        return;

    const int hit = hitTestHandles (e.position);

    if (hit >= 0)
    {
        showValueEditor (hit);
        return;
    }

    createBand (xToFreq (e.position.x), yToDb (e.position.y), 1.0f);
}

void EqDisplay::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    int target = hitTestHandles (e.position);
    if (target < 0)
        target = selectedBand;
    if (target < 0)
        return;

    const float factor = std::pow (1.6f, wheel.deltaY);
    const float q = juce::jlimit (minQ, maxQ, paramValue (target, "q") * factor);
    setParamValue (target, "q", q);
}

void EqDisplay::showValueEditor (int band)
{
    auto content = std::make_unique<BandValueEditor> (proc, band);
    const auto handle = handlePosition (band).toInt();
    juce::CallOutBox::launchAsynchronously (
        std::move (content),
        localAreaToGlobal (juce::Rectangle<int> (handle.x - 8, handle.y - 8, 16, 16)),
        nullptr);
}

// Clipboard ------------------------------------------------------------------

void EqDisplay::copyBandToClipboard (int band) const
{
    juce::XmlElement xml (bandClipTag);
    for (auto* suffix : bandClipSuffixes)
        xml.setAttribute (suffix, paramValue (band, suffix));
    juce::SystemClipboard::copyTextToClipboard (xml.toString());
}

std::unique_ptr<juce::XmlElement> EqDisplay::getClipboardBand() const
{
    auto xml = juce::parseXML (juce::SystemClipboard::getTextFromClipboard());
    if (xml != nullptr && xml->hasTagName (bandClipTag))
        return xml;
    return nullptr;
}

void EqDisplay::applyBandXml (int band, const juce::XmlElement& xml) const
{
    for (auto* suffix : bandClipSuffixes)
        setParamValue (band, suffix, (float) xml.getDoubleAttribute (suffix));
    setParamValue (band, "on", 1.0f);
    setBandActive (proc.apvts.state, band, true, proc.apvts.undoManager);
}

void EqDisplay::copyPresetToClipboard() const
{
    if (auto xml = proc.apvts.copyState().createXml())
        juce::SystemClipboard::copyTextToClipboard (xml->toString());
}

void EqDisplay::pastePresetFromClipboard() const
{
    auto xml = juce::parseXML (juce::SystemClipboard::getTextFromClipboard());
    if (xml != nullptr && xml->hasTagName (proc.apvts.state.getType()))
    {
        auto state = juce::ValueTree::fromXml (*xml);
        migrateBandActiveFlags (state);
        proc.apvts.replaceState (state);
    }
}

// Menus ----------------------------------------------------------------------

void EqDisplay::showBandMenu (int band)
{
    juce::PopupMenu menu;

    const auto currentType = (int) paramValue (band, "type");
    juce::PopupMenu shapes;
    for (int t = 0; t < typeChoices.size(); ++t)
        shapes.addItem (menuTypeBase + t, typeChoices[t], true, t == currentType);
    menu.addSubMenu ("Shape", shapes);

    const auto currentSlope = (int) paramValue (band, "slope");
    juce::PopupMenu slopes;
    for (int s = 0; s < slopeChoices.size(); ++s)
        slopes.addItem (menuSlopeBase + s, slopeChoices[s], true, s == currentSlope);
    menu.addSubMenu ("Slope", slopes, typeHasSlope ((FilterType) currentType));

    const auto currentChan = (int) paramValue (band, "chan");
    juce::PopupMenu chans;
    for (int c = 0; c < channelChoices.size(); ++c)
        chans.addItem (menuChanBase + c, channelChoices[c], true, c == currentChan);
    menu.addSubMenu ("Channel", chans);

    menu.addSeparator();
    menu.addItem (menuCopyBand, "Copy Band");
    menu.addItem (menuPasteBand, "Paste Band", getClipboardBand() != nullptr);
    menu.addItem (menuSnapToNote, "Snap to Note ("
        + freqToNoteName (snapFreqToNote (paramValue (band, "freq"))) + ")");
    menu.addItem (menuResetGain, "Reset Gain");
    menu.addSeparator();
    menu.addItem (menuDelete, "Delete Band");

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this),
        [this, band] (int result)
        {
            if (result == 0)
                return;

            if (result == menuDelete)
            {
                deleteBand (band);
            }
            else if (result == menuCopyBand)
                copyBandToClipboard (band);
            else if (result == menuPasteBand)
            {
                if (auto xml = getClipboardBand())
                    applyBandXml (band, *xml);
            }
            else if (result == menuSnapToNote)
                setParamValue (band, "freq",
                    (float) snapFreqToNote (paramValue (band, "freq")));
            else if (result == menuResetGain)
                setParamValue (band, "gain", 0.0f);
            else if (result >= menuTypeBase && result < menuTypeBase + 100)
                setParamValue (band, "type", (float) (result - menuTypeBase));
            else if (result >= menuSlopeBase && result < menuSlopeBase + 100)
                setParamValue (band, "slope", (float) (result - menuSlopeBase));
            else if (result >= menuChanBase && result < menuChanBase + 100)
                setParamValue (band, "chan", (float) (result - menuChanBase));

            repaint();
        });
}

juce::PopupMenu EqDisplay::buildAnalyzerMenu() const
{
    juce::PopupMenu analyzer;
    analyzer.addItem (menuAnalyzerPre, "Pre-EQ", true, showPre);
    analyzer.addItem (menuAnalyzerPost, "Post-EQ", true, showPost);
    analyzer.addItem (menuAnalyzerExt, "External (sidechain)",
                      proc.sidechainConnected() || showExt, showExt);
    analyzer.addItem (menuAnalyzerCollisions, "Collisions (input vs external)",
                      proc.sidechainConnected() || showCollisions, showCollisions);
    analyzer.addItem (menuAnalyzerFreeze, "Freeze", true, frozen);
    analyzer.addSeparator();
    analyzer.addItem (menuSpeedSlow, "Slow", true, releasePerFrame < 1.0f);
    analyzer.addItem (menuSpeedMedium, "Medium", true,
                      releasePerFrame >= 1.0f && releasePerFrame < 2.5f);
    analyzer.addItem (menuSpeedFast, "Fast", true, releasePerFrame >= 2.5f);
    return analyzer;
}

void EqDisplay::handleAnalyzerResult (int result)
{
    switch (result)
    {
        case menuAnalyzerPre:        showPre = ! showPre; break;
        case menuAnalyzerPost:       showPost = ! showPost; break;
        case menuAnalyzerExt:        showExt = ! showExt; break;
        case menuAnalyzerCollisions: showCollisions = ! showCollisions; break;
        case menuAnalyzerFreeze:     frozen = ! frozen; break;
        case menuSpeedSlow:          releasePerFrame = 0.6f; break;
        case menuSpeedMedium:        releasePerFrame = 1.5f; break;
        case menuSpeedFast:          releasePerFrame = 3.5f; break;
        default: break;
    }
    repaint();
}

void EqDisplay::showAnalyzerMenu (juce::Component& anchor)
{
    buildAnalyzerMenu().showMenuAsync (
        juce::PopupMenu::Options().withTargetComponent (&anchor),
        [this] (int result) { handleAnalyzerResult (result); });
}

void EqDisplay::showDisplayMenu()
{
    juce::PopupMenu menu;

    const bool canPasteBand = getClipboardBand() != nullptr && findFreeBand() >= 0;
    menu.addItem (menuPasteBand, "Paste Band", canPasteBand);
    menu.addSeparator();
    menu.addItem (menuCopyPreset, "Copy Preset");
    menu.addItem (menuPastePreset, "Paste Preset",
        [this]
        {
            auto xml = juce::parseXML (juce::SystemClipboard::getTextFromClipboard());
            return xml != nullptr && xml->hasTagName (proc.apvts.state.getType());
        }());

    menu.addSeparator();
    menu.addSubMenu ("Analyzer", buildAnalyzerMenu());

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this),
        [this] (int result)
        {
            switch (result)
            {
                case menuPasteBand:
                    if (auto xml = getClipboardBand())
                    {
                        const int band = findFreeBand();
                        if (band >= 0)
                        {
                            applyBandXml (band, *xml);
                            selectBand (band);
                        }
                    }
                    break;

                case menuCopyPreset:  copyPresetToClipboard(); break;
                case menuPastePreset: pastePresetFromClipboard(); break;
                default:              handleAnalyzerResult (result); break;
            }
            repaint();
        });
}

// EQ Sketch ------------------------------------------------------------------

void EqDisplay::fitSketchToBands()
{
    if (sketchPoints.size() < 2)
        return;

    auto points = sketchPoints;
    std::sort (points.begin(), points.end(),
               [] (const auto& a, const auto& b) { return a.x < b.x; });

    const float xMin = points.getFirst().x, xMax = points.getLast().x;
    if (xMax - xMin < 40.0f)
        return;

    const auto dbAtX = [&] (float x)
    {
        for (int i = 1; i < points.size(); ++i)
        {
            if (points.getReference (i).x >= x)
            {
                const auto& a = points.getReference (i - 1);
                const auto& b = points.getReference (i);
                const float t = b.x > a.x ? (x - a.x) / (b.x - a.x) : 0.0f;
                return yToDb (a.y + t * (b.y - a.y));
            }
        }
        return yToDb (points.getLast().y);
    };

    constexpr int numAnchors = 10;
    const float spanOctaves = std::log2 (xToFreq (xMax) / xToFreq (xMin));
    const float spacing = spanOctaves / (numAnchors - 1);

    // Choose Q so each bell's bandwidth roughly covers the anchor spacing.
    const float bw = juce::jmax (0.1f, spacing * 1.25f);
    const float q = juce::jlimit (0.4f, 6.0f,
        1.0f / (2.0f * std::sinh (0.5f * std::log (2.0f) * bw)));

    for (int a = 0; a < numAnchors; ++a)
    {
        const float x = juce::jmap ((float) a, 0.0f, (float) numAnchors - 1.0f, xMin, xMax);
        const float db = dbAtX (x);

        if (std::abs (db) < 1.0f)
            continue;

        if (createBand (xToFreq (x), db, q) < 0)
            break; // no free bands left
    }

    selectBand (-1);
}

// EQ Match --------------------------------------------------------------------

void EqDisplay::startMatchCapture()
{
    matchAccIn.fill (0.0);
    matchAccExt.fill (0.0);
    matchFramesIn = matchFramesExt = 0;
    matchCapturing = true;
    repaint();
}

// Average level (dB) of an accumulated power spectrum across [loFreq, hiFreq].
float EqDisplay::capturedDbAt (const double* power, int frames, double binHz,
                               float loFreq, float hiFreq) const
{
    if (frames <= 0)
        return -200.0f;

    const int b0 = juce::jmax (1, (int) (loFreq / binHz));
    const int b1 = juce::jmin (fftSize / 2 - 1, (int) std::ceil (hiFreq / binHz));
    if (b1 < b0)
        return -200.0f;

    double sum = 0.0;
    for (int b = b0; b <= b1; ++b)
        sum += power[b];

    const double mean = sum / ((double) (b1 - b0 + 1) * frames);
    return (float) (10.0 * std::log10 (mean + 1.0e-12));
}

float EqDisplay::pastedDbAt (float loFreq, float hiFreq) const
{
    if (pastedRefDb.empty() || pastedRefSr <= 0.0)
        return -200.0f;

    const double binHz = pastedRefSr / (double) (2 * pastedRefDb.size());
    const int b0 = juce::jmax (1, (int) (loFreq / binHz));
    const int b1 = juce::jmin ((int) pastedRefDb.size() - 1, (int) std::ceil (hiFreq / binHz));
    if (b1 < b0)
        return -200.0f;

    // Average in the power domain so loud bins dominate like in capturedDbAt.
    double sum = 0.0;
    for (int b = b0; b <= b1; ++b)
        sum += std::pow (10.0, (double) pastedRefDb[(size_t) b] / 10.0);
    return (float) (10.0 * std::log10 (sum / (b1 - b0 + 1) + 1.0e-12));
}

void EqDisplay::copySpectrumToClipboard() const
{
    if (matchFramesIn <= 0)
        return;

    const double sr = proc.getSampleRate() > 0 ? proc.getSampleRate() : 48000.0;

    juce::StringArray bins;
    for (int b = 0; b < fftSize / 2; ++b)
    {
        const double mean = matchAccIn[(size_t) b] / matchFramesIn;
        bins.add (juce::String (10.0 * std::log10 (mean + 1.0e-12), 1));
    }

    juce::XmlElement xml (spectrumClipTag);
    xml.setAttribute ("sr", sr);
    xml.setAttribute ("bins", bins.joinIntoString (","));
    juce::SystemClipboard::copyTextToClipboard (xml.toString());
}

bool EqDisplay::hasPastedReferenceOnClipboard() const
{
    auto xml = juce::parseXML (juce::SystemClipboard::getTextFromClipboard());
    return xml != nullptr && xml->hasTagName (spectrumClipTag);
}

bool EqDisplay::pasteReferenceSpectrum()
{
    auto xml = juce::parseXML (juce::SystemClipboard::getTextFromClipboard());
    if (xml == nullptr || ! xml->hasTagName (spectrumClipTag))
        return false;

    pastedRefSr = xml->getDoubleAttribute ("sr", 0.0);
    pastedRefDb.clear();
    for (const auto& v : juce::StringArray::fromTokens (xml->getStringAttribute ("bins"), ",", ""))
        pastedRefDb.push_back (v.getFloatValue());

    if (pastedRefSr <= 0.0 || pastedRefDb.size() < 64)
    {
        pastedRefDb.clear();
        return false;
    }

    matchUsePasted = true;
    return true;
}

void EqDisplay::applyMatch()
{
    const double sr = proc.getSampleRate() > 0 ? proc.getSampleRate() : 48000.0;
    const double binHz = sr / fftSize;

    const bool haveRef = matchUsePasted ? ! pastedRefDb.empty() : matchFramesExt > 0;
    if (matchFramesIn <= 0 || ! haveRef)
        return;

    // Sample both spectra at log-spaced anchors, 1/3-octave smoothed.
    constexpr int numAnchors = 14;
    const float fLo = 30.0f;
    const float fHi = juce::jmin (16000.0f, (float) (sr * 0.45));
    const float spanOctaves = std::log2 (fHi / fLo);
    const float spacing = spanOctaves / (numAnchors - 1);

    std::array<float, numAnchors> freq {}, diff {};
    int valid = 0;
    float mean = 0.0f;

    for (int a = 0; a < numAnchors; ++a)
    {
        const float f = fLo * std::pow (2.0f, spacing * (float) a);
        const float lo = f / std::pow (2.0f, 1.0f / 6.0f);
        const float hi = f * std::pow (2.0f, 1.0f / 6.0f);

        const float inDb = capturedDbAt (matchAccIn.data(), matchFramesIn, binHz, lo, hi);
        const float refDb = matchUsePasted
            ? pastedDbAt (lo, hi)
            : capturedDbAt (matchAccExt.data(), matchFramesExt, binHz, lo, hi);

        if (inDb <= -150.0f || refDb <= -150.0f)
        {
            freq[(size_t) a] = -1.0f; // no usable data in this region
            continue;
        }

        freq[(size_t) a] = f;
        diff[(size_t) a] = refDb - inDb;
        mean += diff[(size_t) a];
        ++valid;
    }

    if (valid < 3)
        return;

    // Match the shape, not the level: remove the mean and cap corrections.
    mean /= (float) valid;

    const float bw = juce::jmax (0.1f, spacing * 1.25f);
    const float q = juce::jlimit (0.4f, 6.0f,
        1.0f / (2.0f * std::sinh (0.5f * std::log (2.0f) * bw)));

    for (int a = 0; a < numAnchors; ++a)
    {
        if (freq[(size_t) a] <= 0.0f)
            continue;

        const float db = juce::jlimit (-15.0f, 15.0f, diff[(size_t) a] - mean);
        if (std::abs (db) < 1.0f)
            continue;

        if (createBand (freq[(size_t) a], db, q) < 0)
            break; // no free bands left
    }

    selectBand (-1);
}

void EqDisplay::showMatchMenu (juce::Component& anchor)
{
    juce::PopupMenu menu;

    menu.addItem (menuMatchCapture,
                  matchCapturing ? "Stop capture" : "Capture (play audio now)",
                  true, matchCapturing);
    menu.addSeparator();
    menu.addItem (menuMatchRefExt, "Reference: sidechain input",
                  proc.sidechainConnected() || ! matchUsePasted, ! matchUsePasted);
    menu.addItem (menuMatchRefPasted, "Reference: pasted spectrum",
                  ! pastedRefDb.empty(), matchUsePasted);
    menu.addItem (menuMatchPasteRef, "Paste reference spectrum",
                  hasPastedReferenceOnClipboard());
    menu.addSeparator();
    menu.addItem (menuMatchCopy, "Copy captured spectrum", matchFramesIn > 0);

    const bool haveRef = matchUsePasted ? ! pastedRefDb.empty() : matchFramesExt > 0;
    menu.addItem (menuMatchApply, "Apply Match (fit bands)",
                  matchFramesIn > 0 && haveRef);

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&anchor),
        [this] (int result)
        {
            switch (result)
            {
                case menuMatchCapture:
                    if (matchCapturing)
                        matchCapturing = false;
                    else
                        startMatchCapture();
                    break;

                case menuMatchRefExt:    matchUsePasted = false; break;
                case menuMatchRefPasted: matchUsePasted = ! pastedRefDb.empty(); break;
                case menuMatchPasteRef:  pasteReferenceSpectrum(); break;
                case menuMatchCopy:      copySpectrumToClipboard(); break;

                case menuMatchApply:
                    matchCapturing = false;
                    applyMatch();
                    break;

                default: break;
            }
            repaint();
        });
}

} // namespace eeq
