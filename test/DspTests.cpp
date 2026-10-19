// Offline DSP verification for eeQ.
//
// Instantiates the processor directly, drives sines through it, and checks
// measured gains against the expected EQ behaviour. Exits non-zero on failure.

#include "../src/PluginProcessor.h"
#include <cstdio>

namespace
{

int failures = 0;

void expect (bool ok, const char* what, double got, double expected, double tol)
{
    std::printf ("%-58s got %8.2f  expected %8.2f (+/-%.2f)  %s\n",
                 what, got, expected, tol, ok ? "OK" : "FAIL");
    if (! ok)
        ++failures;
}

void expectNear (const char* what, double got, double expected, double tol)
{
    expect (std::abs (got - expected) <= tol, what, got, expected, tol);
}

void setParam (EeqAudioProcessor& p, const juce::String& id, float plainValue)
{
    auto* param = p.apvts.getParameter (id);
    jassert (param != nullptr);
    param->setValueNotifyingHost (param->convertTo0to1 (plainValue));
}

void setBandParam (EeqAudioProcessor& p, int band, const char* suffix, float v)
{
    setParam (p, eeq::bandParamId (band, suffix), v);
}

constexpr double sr = 48000.0;
constexpr int blockSize = 512;

// Feeds a stereo sine and returns the steady-state output gain in dB.
double measureGainDb (EeqAudioProcessor& p, double freq, double amp, int numBlocks = 120)
{
    juce::AudioBuffer<float> buf (2, blockSize);
    juce::MidiBuffer midi;

    double phase = 0.0;
    const double inc = juce::MathConstants<double>::twoPi * freq / sr;
    double sumSq = 0.0;
    long count = 0;

    for (int b = 0; b < numBlocks; ++b)
    {
        for (int i = 0; i < blockSize; ++i)
        {
            const float v = (float) (amp * std::sin (phase));
            buf.setSample (0, i, v);
            buf.setSample (1, i, v);
            phase += inc;
        }

        p.processBlock (buf, midi);

        if (b >= (numBlocks * 3) / 5) // measure the last 40 %
        {
            for (int i = 0; i < blockSize; ++i)
            {
                const double v = buf.getSample (0, i);
                sumSq += v * v;
                ++count;
            }
        }
    }

    const double rms = std::sqrt (sumSq / (double) count);
    const double inRms = amp / std::sqrt (2.0);
    return 20.0 * std::log10 (rms / inRms);
}

std::unique_ptr<EeqAudioProcessor> makeProcessor()
{
    auto p = std::make_unique<EeqAudioProcessor>();
    p->setPlayConfigDetails (2, 2, sr, blockSize);
    p->prepareToPlay (sr, blockSize);
    return p;
}

void testStaticBell()
{
    auto p = makeProcessor();
    setBandParam (*p, 0, "type", (float) (int) eeq::FilterType::bell);
    setBandParam (*p, 0, "freq", 1000.0f);
    setBandParam (*p, 0, "gain", 12.0f);
    setBandParam (*p, 0, "q", 1.0f);
    setBandParam (*p, 0, "on", 1.0f);

    expectNear ("static bell +12 dB @ 1 kHz: gain at 1 kHz",
                measureGainDb (*p, 1000.0, 0.1), 12.0, 0.5);
    expectNear ("static bell +12 dB @ 1 kHz: gain at 60 Hz",
                measureGainDb (*p, 60.0, 0.1), 0.0, 0.7);
}

void testDynamicCompression()
{
    auto p = makeProcessor();
    setBandParam (*p, 0, "type", (float) (int) eeq::FilterType::bell);
    setBandParam (*p, 0, "freq", 1000.0f);
    setBandParam (*p, 0, "gain", 0.0f);
    setBandParam (*p, 0, "q", 1.0f);
    setBandParam (*p, 0, "dyn", 1.0f);
    setBandParam (*p, 0, "dynrange", -12.0f);
    setBandParam (*p, 0, "thresh", -40.0f);
    setBandParam (*p, 0, "att", 5.0f);
    setBandParam (*p, 0, "rel", 50.0f);
    setBandParam (*p, 0, "on", 1.0f);

    // Loud 1 kHz sine: envelope far above threshold -> full -12 dB reduction.
    expectNear ("dynamic bell (range -12): loud sine fully compressed",
                measureGainDb (*p, 1000.0, 0.5), -12.0, 1.0);

    // Quiet sine below threshold: no gain change.
    auto p2 = makeProcessor();
    setBandParam (*p2, 0, "type", (float) (int) eeq::FilterType::bell);
    setBandParam (*p2, 0, "freq", 1000.0f);
    setBandParam (*p2, 0, "dyn", 1.0f);
    setBandParam (*p2, 0, "dynrange", -12.0f);
    setBandParam (*p2, 0, "thresh", -40.0f);
    setBandParam (*p2, 0, "on", 1.0f);

    expectNear ("dynamic bell (range -12): quiet sine untouched",
                measureGainDb (*p2, 1000.0, 0.002), 0.0, 0.5);

    // Off-frequency loud content must not trigger the (filtered) detector.
    auto p3 = makeProcessor();
    setBandParam (*p3, 0, "type", (float) (int) eeq::FilterType::bell);
    setBandParam (*p3, 0, "freq", 4000.0f);
    setBandParam (*p3, 0, "q", 4.0f);
    setBandParam (*p3, 0, "dyn", 1.0f);
    setBandParam (*p3, 0, "dynrange", -12.0f);
    setBandParam (*p3, 0, "thresh", -40.0f);
    setBandParam (*p3, 0, "on", 1.0f);

    // 100 Hz sine, far below the 4 kHz band: detector should stay quiet, and
    // in any case a 4 kHz bell doesn't touch 100 Hz -> expect ~0 dB.
    expectNear ("dynamic bell @4 kHz: off-band 100 Hz sine unaffected",
                measureGainDb (*p3, 100.0, 0.5), 0.0, 0.5);
}

void testUpwardExpansion()
{
    auto p = makeProcessor();
    setBandParam (*p, 0, "type", (float) (int) eeq::FilterType::bell);
    setBandParam (*p, 0, "freq", 1000.0f);
    setBandParam (*p, 0, "gain", 0.0f);
    setBandParam (*p, 0, "dyn", 1.0f);
    setBandParam (*p, 0, "dynrange", 6.0f); // positive: upward
    setBandParam (*p, 0, "thresh", -40.0f);
    setBandParam (*p, 0, "on", 1.0f);

    expectNear ("dynamic bell (range +6): loud sine boosted",
                measureGainDb (*p, 1000.0, 0.5), 6.0, 1.0);
}

void testLinearPhaseGain()
{
    auto p = makeProcessor();
    setBandParam (*p, 0, "type", (float) (int) eeq::FilterType::bell);
    setBandParam (*p, 0, "freq", 1000.0f);
    setBandParam (*p, 0, "gain", 9.0f);
    setBandParam (*p, 0, "q", 1.0f);
    setBandParam (*p, 0, "on", 1.0f);
    setParam (*p, eeq::phaseModeId, (float) (int) eeq::PhaseMode::linearPhase);
    setParam (*p, eeq::lpQualityId, 0.0f); // Low = FIR 1024

    p->messageThreadTick(); // build kernels + report latency

    expectNear ("linear phase bell +9 dB: gain at 1 kHz",
                measureGainDb (*p, 1000.0, 0.1), 9.0, 0.6);
    expectNear ("linear phase: reported latency (samples)",
                (double) p->getLatencySamples(), 1024.0 * 1.5, 0.0);

    // Mid band in linear phase (exercises the MIMO cross kernels).
    auto p2 = makeProcessor();
    setBandParam (*p2, 0, "type", (float) (int) eeq::FilterType::bell);
    setBandParam (*p2, 0, "freq", 1000.0f);
    setBandParam (*p2, 0, "gain", 9.0f);
    setBandParam (*p2, 0, "chan", (float) (int) eeq::ChannelMode::mid);
    setBandParam (*p2, 0, "on", 1.0f);
    setParam (*p2, eeq::phaseModeId, (float) (int) eeq::PhaseMode::linearPhase);
    setParam (*p2, eeq::lpQualityId, 0.0f);
    p2->messageThreadTick();

    // Correlated stereo input is pure mid -> the mid band applies fully.
    expectNear ("linear phase mid-band bell +9 dB on correlated input",
                measureGainDb (*p2, 1000.0, 0.1), 9.0, 0.6);
}

void testSpectralDynamics()
{
    // A pure sine towers above its spectral neighbourhood -> reduced by the
    // full amount.
    auto p = makeProcessor();
    setParam (*p, eeq::specOnId, 1.0f);
    setParam (*p, eeq::specAmountId, 12.0f);
    setParam (*p, eeq::specSensId, 6.0f);
    p->messageThreadTick();

    expectNear ("spectral dynamics: pure 1 kHz sine reduced by amount",
                measureGainDb (*p, 1000.0, 0.25), -12.0, 2.5);
    expectNear ("spectral dynamics: reported latency (samples)",
                (double) p->getLatencySamples(), 2048.0, 0.0);

    // Broadband noise is spectrally balanced -> passes (nearly) untouched.
    auto p2 = makeProcessor();
    setParam (*p2, eeq::specOnId, 1.0f);
    setParam (*p2, eeq::specAmountId, 12.0f);
    setParam (*p2, eeq::specSensId, 6.0f);

    juce::AudioBuffer<float> buf (2, blockSize);
    juce::MidiBuffer midi;
    juce::Random rng (42);
    double inSq = 0.0, outSq = 0.0;
    long count = 0;
    constexpr int numBlocks = 120;

    for (int b = 0; b < numBlocks; ++b)
    {
        for (int i = 0; i < blockSize; ++i)
        {
            const float v = 0.25f * (rng.nextFloat() * 2.0f - 1.0f);
            buf.setSample (0, i, v);
            buf.setSample (1, i, v);
            if (b >= (numBlocks * 3) / 5)
                inSq += (double) v * v;
        }

        p2->processBlock (buf, midi);

        if (b >= (numBlocks * 3) / 5)
        {
            for (int i = 0; i < blockSize; ++i)
            {
                const double v = buf.getSample (0, i);
                outSq += v * v;
                ++count;
            }
        }
    }

    const double deltaDb = 10.0 * std::log10 (outSq / inSq);
    expectNear ("spectral dynamics: broadband noise nearly untouched", deltaDb, 0.0, 2.0);
}

void testNaturalPhaseGain()
{
    auto p = makeProcessor();
    setBandParam (*p, 0, "type", (float) (int) eeq::FilterType::bell);
    setBandParam (*p, 0, "freq", 1000.0f);
    setBandParam (*p, 0, "gain", 12.0f);
    setBandParam (*p, 0, "on", 1.0f);
    setParam (*p, eeq::phaseModeId, (float) (int) eeq::PhaseMode::naturalPhase);

    expectNear ("natural phase bell +12 dB: gain at 1 kHz",
                measureGainDb (*p, 1000.0, 0.1), 12.0, 0.6);
}

void testPresetSystem()
{
    auto p = makeProcessor();
    auto& pm = p->presets;

    // Every factory preset must load.
    int factoryCount = 0;
    for (const auto& info : pm.getPresets())
        if (info.factory)
        {
            ++factoryCount;
            if (! pm.loadPreset (info))
                expect (false, ("factory preset loads: " + info.name).toRawUTF8(),
                        0.0, 1.0, 0.0);
        }
    expect (factoryCount >= 10, "factory preset count", factoryCount, 15, 5);

    // A known preset lands in the parameters, the active flags and the name.
    const eeq::PresetInfo* kick = nullptr;
    for (const auto& info : pm.getPresets())
        if (info.name == "Kick Punch")
            kick = &info;

    expect (kick != nullptr, "Kick Punch factory preset exists",
            kick != nullptr ? 1.0 : 0.0, 1.0, 0.0);
    if (kick != nullptr)
    {
        pm.loadPreset (*kick);
        expectNear ("preset: band 0 tuned to 55 Hz",
                    p->apvts.getRawParameterValue (eeq::bandParamId (0, "freq"))->load(),
                    55.0, 0.5);
        expect (eeq::isBandActive (p->apvts.state, 0),
                "preset: band 0 active flag derived from 'on'",
                eeq::isBandActive (p->apvts.state, 0) ? 1.0 : 0.0, 1.0, 0.0);
        expect (! eeq::isBandActive (p->apvts.state, 3),
                "preset: unused band stays inactive",
                eeq::isBandActive (p->apvts.state, 3) ? 1.0 : 0.0, 0.0, 0.0);
        expect (pm.getCurrentPresetName() == "Kick Punch",
                "preset: current name follows the load",
                pm.getCurrentPresetName() == "Kick Punch" ? 1.0 : 0.0, 1.0, 0.0);
    }

    // Wrapper parsing, legacy bare-state parsing, and the version gate.
    auto stateXml = p->apvts.copyState().createXml();
    const auto presetCfg = eeq::makePresetConfig();
    juce::XmlElement wrapper (presetCfg.presetTag.toString());
    wrapper.setAttribute ("stateVersion", presetCfg.stateVersion);
    wrapper.addChildElement (new juce::XmlElement (*stateXml));

    expect (eeq::PresetManager::stateFromPresetXml (wrapper, presetCfg).isValid(),
            "preset XML: metadata wrapper parses",
            eeq::PresetManager::stateFromPresetXml (wrapper, presetCfg).isValid() ? 1.0 : 0.0, 1.0, 0.0);
    expect (eeq::PresetManager::stateFromPresetXml (*stateXml, presetCfg).isValid(),
            "preset XML: bare-state preset parses",
            eeq::PresetManager::stateFromPresetXml (*stateXml, presetCfg).isValid() ? 1.0 : 0.0, 1.0, 0.0);

    wrapper.setAttribute ("stateVersion", presetCfg.stateVersion + 1);
    expect (! eeq::PresetManager::stateFromPresetXml (wrapper, presetCfg).isValid(),
            "preset XML: newer schema rejected",
            eeq::PresetManager::stateFromPresetXml (wrapper, presetCfg).isValid() ? 1.0 : 0.0, 0.0, 0.0);

    // Loading a preset without MIDI mappings inherits the session's, and
    // never touches the user's UI preferences riding in the state tree.
    p->setCCMapping (21, (int) eeq::MidiTarget::outGain);
    p->apvts.state.setProperty ("ui_gpu", true, nullptr);
    pm.loadPreset (pm.getPresets().front()); // Init
    expect (p->getCCMapping (21) == (int) eeq::MidiTarget::outGain,
            "preset load inherits MIDI mappings",
            (double) p->getCCMapping (21), (double) (int) eeq::MidiTarget::outGain, 0.0);
    expect ((bool) p->apvts.state.getProperty ("ui_gpu", false),
            "preset load preserves UI preferences",
            (bool) p->apvts.state.getProperty ("ui_gpu", false) ? 1.0 : 0.0, 1.0, 0.0);

    // Header prev/next stepping wraps through the list.
    expect (pm.getCurrentPresetName() == "Init",
            "stepping: starts on Init",
            pm.getCurrentPresetName() == "Init" ? 1.0 : 0.0, 1.0, 0.0);
    pm.loadAdjacent (true);
    expect (pm.getCurrentPresetName() != "Init",
            "stepping: next moves off Init",
            pm.getCurrentPresetName() != "Init" ? 1.0 : 0.0, 1.0, 0.0);
    pm.loadAdjacent (false);
    expect (pm.getCurrentPresetName() == "Init",
            "stepping: prev returns to Init",
            pm.getCurrentPresetName() == "Init" ? 1.0 : 0.0, 1.0, 0.0);

    // A factory preset audibly reaches the DSP.
    auto p2 = makeProcessor();
    const eeq::PresetInfo* hp = nullptr;
    for (const auto& info : p2->presets.getPresets())
        if (info.name == "Gentle High-Pass")
            hp = &info;
    if (hp != nullptr)
    {
        p2->presets.loadPreset (*hp);
        expectNear ("factory high-pass: 30 Hz attenuated",
                    measureGainDb (*p2, 30.0, 0.1), -16.5, 4.0);
        expectNear ("factory high-pass: 1 kHz untouched",
                    measureGainDb (*p2, 1000.0, 0.1), 0.0, 0.5);
    }
    else
    {
        expect (false, "Gentle High-Pass factory preset exists", 0.0, 1.0, 0.0);
    }
}

} // namespace

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    std::printf ("eeQ DSP tests @ %.0f Hz, block %d\n\n", sr, blockSize);

    testStaticBell();
    testDynamicCompression();
    testUpwardExpansion();
    testLinearPhaseGain();
    testNaturalPhaseGain();
    testSpectralDynamics();
    testPresetSystem();

    std::printf ("\n%s (%d failure%s)\n",
                 failures == 0 ? "ALL DSP TESTS PASSED" : "DSP TESTS FAILED",
                 failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
