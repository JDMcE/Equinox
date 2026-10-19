#include "PluginProcessor.h"
#include "PluginEditor.h"

EeqAudioProcessor::EeqAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                          .withInput ("Sidechain", juce::AudioChannelSet::stereo(), false)),
      apvts (*this, &undoManager, "Params", eeq::createParameterLayout())
{
    for (int i = 0; i < eeq::numBands; ++i)
    {
        auto& p = bandParams[(size_t) i];
        p.on    = apvts.getRawParameterValue (eeq::bandParamId (i, "on"));
        p.type  = apvts.getRawParameterValue (eeq::bandParamId (i, "type"));
        p.freq  = apvts.getRawParameterValue (eeq::bandParamId (i, "freq"));
        p.gain  = apvts.getRawParameterValue (eeq::bandParamId (i, "gain"));
        p.q     = apvts.getRawParameterValue (eeq::bandParamId (i, "q"));
        p.slope = apvts.getRawParameterValue (eeq::bandParamId (i, "slope"));
        p.chan  = apvts.getRawParameterValue (eeq::bandParamId (i, "chan"));
        p.dyn      = apvts.getRawParameterValue (eeq::bandParamId (i, "dyn"));
        p.dynRange = apvts.getRawParameterValue (eeq::bandParamId (i, "dynrange"));
        p.thresh   = apvts.getRawParameterValue (eeq::bandParamId (i, "thresh"));
        p.att      = apvts.getRawParameterValue (eeq::bandParamId (i, "att"));
        p.rel      = apvts.getRawParameterValue (eeq::bandParamId (i, "rel"));
        p.ext      = apvts.getRawParameterValue (eeq::bandParamId (i, "ext"));
        p.scf      = apvts.getRawParameterValue (eeq::bandParamId (i, "scf"));
    }

    outGainParam   = apvts.getRawParameterValue (eeq::outGainId);
    phaseInvParam  = apvts.getRawParameterValue (eeq::phaseInvId);
    phaseModeParam = apvts.getRawParameterValue (eeq::phaseModeId);
    lpQualityParam = apvts.getRawParameterValue (eeq::lpQualityId);
    characterParam = apvts.getRawParameterValue (eeq::characterId);
    autoGainParam  = apvts.getRawParameterValue (eeq::autoGainId);
    gainScaleParam = apvts.getRawParameterValue (eeq::gainScaleId);
    specOnParam     = apvts.getRawParameterValue (eeq::specOnId);
    specAmountParam = apvts.getRawParameterValue (eeq::specAmountId);
    specSensParam   = apvts.getRawParameterValue (eeq::specSensId);

    for (auto& t : ccTargets)
        t.store (-1);

    ticker.startTimerHz (20);
}

bool EeqAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& in = layouts.getMainInputChannelSet();
    const auto& out = layouts.getMainOutputChannelSet();

    if (in != out)
        return false;

    if (in != juce::AudioChannelSet::mono() && in != juce::AudioChannelSet::stereo())
        return false;

    const auto& sc = layouts.getChannelSet (true, 1);
    return sc.isDisabled() || sc == juce::AudioChannelSet::mono()
        || sc == juce::AudioChannelSet::stereo();
}

void EeqAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    for (auto& b : bands)
        b.prepare (sampleRate);
    soloFilter.prepare (sampleRate);

    for (auto& d : dynStates)
        d.reset();

    spectral.reset();
    spectralWasOn = false;

    constexpr double smoothTime = 0.05;
    for (int i = 0; i < eeq::numBands; ++i)
    {
        auto& p = bandParams[(size_t) i];
        freqSmooth[(size_t) i].reset (sampleRate, smoothTime);
        gainSmooth[(size_t) i].reset (sampleRate, smoothTime);
        qSmooth[(size_t) i].reset (sampleRate, smoothTime);
        freqSmooth[(size_t) i].setCurrentAndTargetValue (p.freq->load());
        gainSmooth[(size_t) i].setCurrentAndTargetValue (p.gain->load());
        qSmooth[(size_t) i].setCurrentAndTargetValue (p.q->load());
    }

    outGainSmooth.reset (sampleRate, smoothTime);
    outGainSmooth.setCurrentAndTargetValue (
        juce::Decibels::decibelsToGain (outGainParam->load()));

    autoGainSmooth.reset (sampleRate, 0.1);
    autoGainSmooth.setCurrentAndTargetValue (1.0f);

    monoScratch.setSize (1, samplesPerBlock);

    linearEngine.prepare (samplesPerBlock);

    oversampler = std::make_unique<juce::dsp::Oversampling<float>> (
        2, 1, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, false);
    oversampler->initProcessing ((size_t) samplesPerBlock);

    lastSnapshotHash = 0; // force a kernel rebuild on the next tick
    kernelsBuilt = false;
    lastLatency = -1;
}

void EeqAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;

    handleMidi (midi);

    const int numSamples = buffer.getNumSamples();
    if (numSamples == 0)
        return;

    auto mainBus = getBusBuffer (buffer, true, 0);
    const bool stereo = mainBus.getNumChannels() > 1;

    float* l = mainBus.getWritePointer (0);
    float* r = nullptr;

    // Sidechain (may be disabled or absent).
    const float* scL = nullptr;
    const float* scR = nullptr;
    if (auto* scBus = getBus (true, 1); scBus != nullptr && scBus->isEnabled())
    {
        auto sc = getBusBuffer (buffer, true, 1);
        if (sc.getNumChannels() > 0)
        {
            scL = sc.getReadPointer (0);
            scR = sc.getNumChannels() > 1 ? sc.getReadPointer (1) : scL;
        }
    }
    scWasConnected = scL != nullptr;

    if (scL != nullptr)
        extFifo.push (scL, scR, numSamples);

    if (stereo)
    {
        r = buffer.getWritePointer (1);
    }
    else
    {
        // Duplicate the mono signal so band processing can treat it as stereo;
        // the scratch copy is discarded afterwards.
        if (monoScratch.getNumSamples() < numSamples)
            monoScratch.setSize (1, numSamples, false, false, true);
        monoScratch.copyFrom (0, 0, l, numSamples);
        r = monoScratch.getWritePointer (0);
    }

    preFifo.push (l, r, numSamples);

    // Intelligent solo: audition the region a band works on instead of the EQ.
    if (const int solo = soloBand.load(); solo >= 0 && solo < eeq::numBands)
    {
        auto& p = bandParams[(size_t) solo];
        const auto type = (eeq::FilterType) (int) p.type->load();
        const float freq = p.freq->load();
        const float q = p.q->load();

        auto soloType = eeq::FilterType::bandPass;
        float soloQ = juce::jlimit (0.5f, eeq::maxQ, q);

        if (type == eeq::FilterType::lowShelf || type == eeq::FilterType::lowCut)
        {
            soloType = eeq::FilterType::highCut; // pass the low region
            soloQ = 0.7071f;
        }
        else if (type == eeq::FilterType::highShelf || type == eeq::FilterType::highCut)
        {
            soloType = eeq::FilterType::lowCut; // pass the high region
            soloQ = 0.7071f;
        }

        soloFilter.update (true, soloType, freq, 0.0f, soloQ,
                           (int) p.slope->load(), eeq::ChannelMode::stereo);
        soloFilter.processBlock (l, r, numSamples);
    }
    else
    {
        soloFilter.update (false, eeq::FilterType::bandPass, 1000.0f, 0.0f, 1.0f,
                           eeq::defaultSlopeIndex, eeq::ChannelMode::stereo);

        const auto mode = (eeq::PhaseMode) (int) phaseModeParam->load();

        if (mode == eeq::PhaseMode::linearPhase && linearEngine.process (l, r, numSamples))
        {
            // FIR path done. (Falls through to the IIR path below only while
            // kernels are still being built right after switching modes.)
        }
        else if (mode == eeq::PhaseMode::naturalPhase && oversampler != nullptr)
        {
            float* chans[2] = { l, r };
            juce::dsp::AudioBlock<float> block (chans, 2, (size_t) numSamples);
            auto osBlock = oversampler->processSamplesUp (block);

            processBands (osBlock.getChannelPointer (0), osBlock.getChannelPointer (1),
                          numSamples, (int) osBlock.getNumSamples(), getSampleRate() * 2.0,
                          scL, scR);

            oversampler->processSamplesDown (block);
        }
        else
        {
            processBands (l, r, numSamples, numSamples, getSampleRate(), scL, scR);
        }
    }

    // Spectral dynamics (adds fftSize latency while enabled; skipped while
    // soloing so the audition stays untouched).
    const bool specOn = specOnParam->load() > 0.5f && soloBand.load() < 0;
    if (specOn)
    {
        if (! spectralWasOn)
            spectral.reset();
        spectral.process (l, r, numSamples, specAmountParam->load(), specSensParam->load());
    }
    spectralWasOn = specOn;

    // Character: gentle analog-style saturation (unity slope at low level).
    const auto character = (eeq::CharacterMode) (int) characterParam->load();
    if (character != eeq::CharacterMode::clean)
    {
        const float drive = character == eeq::CharacterMode::gentle ? 0.35f : 0.8f;
        const float inv = 1.0f / drive;
        for (int i = 0; i < numSamples; ++i)
        {
            l[i] = std::tanh (drive * l[i]) * inv;
            r[i] = std::tanh (drive * r[i]) * inv;
        }
    }

    // Output gain, auto gain and polarity.
    outGainSmooth.setTargetValue (juce::Decibels::decibelsToGain (outGainParam->load()));
    autoGainSmooth.setTargetValue (autoGainParam->load() > 0.5f
        ? juce::Decibels::decibelsToGain (autoGainTargetDb.load())
        : 1.0f);
    const float polarity = phaseInvParam->load() > 0.5f ? -1.0f : 1.0f;

    for (int i = 0; i < numSamples; ++i)
    {
        const float g = outGainSmooth.getNextValue() * autoGainSmooth.getNextValue() * polarity;
        l[i] *= g;
        r[i] *= g;
    }

    postFifo.push (l, r, numSamples);

    // Peak metering (GUI resets with exchange).
    float pl = peakLeft.load(), pr = peakRight.load();
    for (int i = 0; i < numSamples; ++i)
    {
        pl = juce::jmax (pl, std::abs (l[i]));
        pr = juce::jmax (pr, std::abs (r[i]));
    }
    peakLeft.store (pl);
    peakRight.store (pr);
}

void EeqAudioProcessor::handleMidi (const juce::MidiBuffer& midi)
{
    if (midi.isEmpty())
        return;

    for (const auto metadata : midi)
    {
        const auto msg = metadata.getMessage();
        if (! msg.isController())
            continue;

        const int cc = msg.getControllerNumber();
        if (cc < 0 || cc >= 128)
            continue;

        // Learn mode: the first CC that moves gets the armed target.
        if (const int armed = midiLearnTarget.load(); armed >= 0)
        {
            ccTargets[(size_t) cc].store (armed);
            midiLearnTarget.store (-1);
            midiMapDirty.store (true); // persisted on the next message-thread tick
            continue;
        }

        if (const int target = ccTargets[(size_t) cc].load(); target >= 0)
            applyCC (target, (float) msg.getControllerValue() / 127.0f);
    }
}

void EeqAudioProcessor::applyCC (int target, float norm01)
{
    const int band = juce::jlimit (0, eeq::numBands - 1, activeBandUI.load());

    juce::RangedAudioParameter* param = nullptr;
    switch ((eeq::MidiTarget) target)
    {
        case eeq::MidiTarget::selFreq:   param = apvts.getParameter (eeq::bandParamId (band, "freq")); break;
        case eeq::MidiTarget::selGain:   param = apvts.getParameter (eeq::bandParamId (band, "gain")); break;
        case eeq::MidiTarget::selQ:      param = apvts.getParameter (eeq::bandParamId (band, "q")); break;
        case eeq::MidiTarget::outGain:   param = apvts.getParameter (eeq::outGainId); break;
        case eeq::MidiTarget::gainScale: param = apvts.getParameter (eeq::gainScaleId); break;
        default: break;
    }

    if (param != nullptr)
        param->setValueNotifyingHost (norm01);
}

void EeqAudioProcessor::setCCMapping (int cc, int target)
{
    if (cc < 0 || cc >= 128)
        return;
    ccTargets[(size_t) cc].store (target);
    apvts.state.setProperty ("midimap", midiMapToString(), nullptr);
}

void EeqAudioProcessor::clearCCMappings()
{
    for (auto& t : ccTargets)
        t.store (-1);
    apvts.state.setProperty ("midimap", "", nullptr);
}

juce::String EeqAudioProcessor::midiMapToString() const
{
    juce::StringArray entries;
    for (int cc = 0; cc < 128; ++cc)
        if (const int t = ccTargets[(size_t) cc].load(); t >= 0)
            entries.add (juce::String (cc) + ":" + juce::String (t));
    return entries.joinIntoString (",");
}

void EeqAudioProcessor::rebuildMidiMapFromState()
{
    for (auto& t : ccTargets)
        t.store (-1);

    const auto text = apvts.state.getProperty ("midimap", "").toString();
    for (const auto& entry : juce::StringArray::fromTokens (text, ",", ""))
    {
        const int cc = entry.upToFirstOccurrenceOf (":", false, false).getIntValue();
        const int target = entry.fromFirstOccurrenceOf (":", false, false).getIntValue();
        if (cc >= 0 && cc < 128 && target >= 0 && target < eeq::numMidiTargets)
            ccTargets[(size_t) cc].store (target);
    }
}

// Detection-signal channel selection for dynamic bands.
static double pickChannel (eeq::ChannelMode chan, double l, double r)
{
    switch (chan)
    {
        case eeq::ChannelMode::left:  return l;
        case eeq::ChannelMode::right: return r;
        case eeq::ChannelMode::side:  return 0.5 * (l - r);
        case eeq::ChannelMode::mid:
        case eeq::ChannelMode::stereo:
        default:                      return 0.5 * (l + r);
    }
}

void EeqAudioProcessor::processBands (float* l, float* r, int numSamples,
                                      int procSamples, double procRate,
                                      const float* scL, const float* scR)
{
    const float scale = gainScaleParam->load();

    // Per-block band setup: collect flags, set smoother targets.
    struct Setup
    {
        bool on = false, dynamic = false, interp = false, ext = false, scf = false;
        eeq::FilterType type = eeq::FilterType::bell;
        eeq::ChannelMode chan = eeq::ChannelMode::stereo;
        float freq = 0.0f, gain = 0.0f, q = 0.0f;
        int slope = 0;
        float range = 0.0f, thresh = 0.0f, attCoef = 0.0f, relCoef = 0.0f;
    };
    std::array<Setup, eeq::numBands> setup;
    bool anySubBlock = false;

    for (int i = 0; i < eeq::numBands; ++i)
    {
        auto& p = bandParams[(size_t) i];
        auto& s = setup[(size_t) i];
        auto& fs = freqSmooth[(size_t) i];
        auto& gs = gainSmooth[(size_t) i];
        auto& qs = qSmooth[(size_t) i];

        fs.setTargetValue (p.freq->load());
        gs.setTargetValue (p.gain->load());
        qs.setTargetValue (p.q->load());

        s.on = p.on->load() > 0.5f;
        s.type = (eeq::FilterType) (int) p.type->load();
        s.chan = (eeq::ChannelMode) (int) p.chan->load();
        s.slope = (int) p.slope->load();

        s.range = p.dynRange->load();
        s.dynamic = s.on && p.dyn->load() > 0.5f && s.range != 0.0f
                 && eeq::typeSupportsDynamics (s.type);

        // Smart parameter interpolation: while freq/gain/Q are still gliding
        // (host automation or UI drags), the band re-tunes its coefficients
        // every sub-block instead of once per buffer.
        s.interp = s.on && ! s.dynamic
                && (fs.isSmoothing() || gs.isSmoothing() || qs.isSmoothing());

        if (s.dynamic)
        {
            s.ext = p.ext->load() > 0.5f && scL != nullptr;
            s.scf = p.scf->load() > 0.5f;
            s.thresh = p.thresh->load();
            s.attCoef = 1.0f - std::exp (-1.0f / (float) (p.att->load() * 0.001 * procRate));
            s.relCoef = 1.0f - std::exp (-1.0f / (float) (p.rel->load() * 0.001 * procRate));
        }
        else
        {
            dynOffsetUI[(size_t) i].store (0.0f, std::memory_order_relaxed);
        }

        anySubBlock |= s.dynamic || s.interp;

        if (! s.dynamic && ! s.interp)
        {
            // Settled (or off): advance smoothers for the whole block (no-op
            // when settled) and update coefficients at most once. EqBand
            // caches coefficients, so unchanged parameters cost nothing here.
            fs.skip (numSamples);
            gs.skip (numSamples);
            qs.skip (numSamples);
            s.freq = fs.getCurrentValue();
            s.gain = gs.getCurrentValue() * scale;
            s.q = qs.getCurrentValue();

            bands[(size_t) i].update (s.on, s.type, s.freq, s.gain, s.q, s.slope,
                                      s.chan, procRate);
        }
    }

    // Fast path: everything settled and static; process full blocks.
    if (! anySubBlock)
    {
        for (int i = 0; i < eeq::numBands; ++i)
            bands[(size_t) i].processBlock (l, r, procSamples);
        return;
    }

    // Sub-block path: run the serial band chain in short sub-blocks so dynamic
    // bands can follow their envelopes and interpolating bands can re-tune
    // with fine time resolution.
    constexpr int subBlock = 32;
    const int osShift = procSamples > numSamples ? 1 : 0; // 2x oversampled?

    for (int start = 0; start < procSamples; start += subBlock)
    {
        const int len = juce::jmin (subBlock, procSamples - start);

        // Host-rate samples covered by this sub-block (halved when oversampled).
        const int hostLen = ((start + len) >> osShift) - (start >> osShift);

        for (int i = 0; i < eeq::numBands; ++i)
        {
            auto& s = setup[(size_t) i];
            if (! s.on)
                continue;

            if (s.dynamic || s.interp)
            {
                auto& fs = freqSmooth[(size_t) i];
                auto& gs = gainSmooth[(size_t) i];
                auto& qs = qSmooth[(size_t) i];
                fs.skip (hostLen);
                gs.skip (hostLen);
                qs.skip (hostLen);
                s.freq = fs.getCurrentValue();
                s.gain = gs.getCurrentValue() * scale;
                s.q = qs.getCurrentValue();
            }

            if (s.interp)
            {
                bands[(size_t) i].update (true, s.type, s.freq, s.gain, s.q,
                                          s.slope, s.chan, procRate);
            }
            else if (s.dynamic)
            {
                auto& d = dynStates[(size_t) i];

                // Detection filter around the band's working region (tracks
                // the smoothed frequency; recomputed only on change).
                if (d.scFreq != s.freq || d.scQ != s.q
                    || d.scType != (int) s.type || d.scRate != procRate)
                {
                    d.scFreq = s.freq;
                    d.scQ = s.q;
                    d.scType = (int) s.type;
                    d.scRate = procRate;

                    const double w0 = 2.0 * 3.141592653589793 *
                        juce::jlimit (10.0, procRate * 0.49, (double) s.freq) / procRate;

                    if (s.type == eeq::FilterType::lowShelf)
                        d.scCoeffs = eeq::detail::rbjCut2 (w0, 0.7071, true);   // low-pass region
                    else if (s.type == eeq::FilterType::highShelf)
                        d.scCoeffs = eeq::detail::rbjCut2 (w0, 0.7071, false);  // high-pass region
                    else
                        d.scCoeffs = eeq::detail::rbjBandPass (
                            w0, juce::jlimit (0.3, 8.0, (double) s.q));
                }

                // Envelope of the (filtered) detection signal over this sub-block.
                for (int n = 0; n < len; ++n)
                {
                    const int idx = start + n;
                    double det;

                    if (s.ext)
                    {
                        const int scIdx = idx >> osShift; // sidechain is at host rate
                        const double sl = scL[scIdx], sr = scR[scIdx];
                        det = pickChannel (s.chan, sl, sr);
                    }
                    else
                    {
                        det = pickChannel (s.chan, l[idx], r[idx]);
                    }

                    if (s.scf)
                    {
                        const double y = d.scCoeffs.b0 * det + d.z1;
                        d.z1 = d.scCoeffs.b1 * det - d.scCoeffs.a1 * y + d.z2;
                        d.z2 = d.scCoeffs.b2 * det - d.scCoeffs.a2 * y;
                        det = y;
                    }

                    const float a = (float) std::abs (det);
                    d.env += (a > d.env ? s.attCoef : s.relCoef) * (a - d.env);
                }

                // Gain offset: 2:1 above threshold, bounded by the dyn range.
                const float envDb = juce::Decibels::gainToDecibels (d.env, -80.0f);
                const float over = juce::jmax (0.0f, envDb - s.thresh);
                const float amount = juce::jmin (std::abs (s.range), over * 0.5f);
                const float offset = s.range < 0.0f ? -amount : amount;

                dynOffsetUI[(size_t) i].store (offset, std::memory_order_relaxed);

                bands[(size_t) i].update (true, s.type, s.freq, s.gain + offset,
                                          s.q, s.slope, s.chan, procRate);
            }

            bands[(size_t) i].processBlock (l + start, r + start, len);
        }
    }
}

void EeqAudioProcessor::messageThreadTick()
{
    // Persist MIDI mappings learned on the audio thread.
    if (midiMapDirty.exchange (false))
        apvts.state.setProperty ("midimap", midiMapToString(), nullptr);

    // Snapshot the parameters that shape the EQ curve and hash them so work
    // only happens on change.
    std::array<eeq::BandSnapshot, eeq::numBands> snapshot;
    const float scale = gainScaleParam->load();

    uint64_t hash = 1469598103934665603ull; // FNV-1a
    auto mix = [&hash] (float v)
    {
        uint32_t bits;
        memcpy (&bits, &v, sizeof (bits));
        hash = (hash ^ bits) * 1099511628211ull;
    };

    for (int i = 0; i < eeq::numBands; ++i)
    {
        auto& p = bandParams[(size_t) i];
        auto& s = snapshot[(size_t) i];
        s.on = p.on->load() > 0.5f;
        s.type = (eeq::FilterType) (int) p.type->load();
        s.freq = p.freq->load();
        s.gain = p.gain->load();
        s.q = p.q->load();
        s.slope = (int) p.slope->load();
        s.chan = (eeq::ChannelMode) (int) p.chan->load();

        mix (s.on ? 1.0f : 0.0f);
        mix ((float) (int) s.type);
        mix (s.freq);
        mix (s.gain);
        mix (s.q);
        mix ((float) s.slope);
        mix ((float) (int) s.chan);
    }
    mix (scale);
    mix (phaseModeParam->load());
    mix (lpQualityParam->load());

    const double sr = getSampleRate() > 0 ? getSampleRate() : 48000.0;
    const auto mode = (eeq::PhaseMode) (int) phaseModeParam->load();
    const int quality = juce::jlimit (0, eeq::lpQualityChoices.size() - 1,
                                      (int) lpQualityParam->load());
    const int firLen = eeq::lpFirLengths[quality];

    const bool changed = hash != lastSnapshotHash;
    lastSnapshotHash = hash;

    if (changed)
    {
        // Auto gain: negative mean of the summed response over the audible range.
        std::vector<eeq::BandCoeffs> activeCoeffs;
        for (const auto& s : snapshot)
            if (s.on)
                activeCoeffs.push_back (eeq::computeBandCoeffs (
                    s.type, s.freq, s.gain * scale, s.q, s.slope, sr));

        double sum = 0.0;
        int count = 0;
        for (int pt = 0; pt <= 24; ++pt)
        {
            const double freq = 20.0 * std::pow (1000.0, pt / 24.0); // 20 Hz .. 20 kHz
            if (freq > sr * 0.499)
                break;

            for (const auto& c : activeCoeffs)
                sum += eeq::bandMagnitudeDb (c, freq, sr);
            ++count;
        }
        autoGainTargetDb.store (count > 0
            ? (float) juce::jlimit (-18.0, 18.0, -sum / count) : 0.0f);
    }

    if (mode == eeq::PhaseMode::linearPhase && (changed || ! kernelsBuilt))
    {
        linearEngine.rebuild (snapshot, sr, firLen, scale);
        kernelsBuilt = true;
    }

    // Report latency to the host when the mode, quality or spectral state changes.
    int latency = 0;
    if (mode == eeq::PhaseMode::linearPhase)
        latency = eeq::LinearPhaseEngine::latencyForLength (firLen);
    else if (mode == eeq::PhaseMode::naturalPhase && oversampler != nullptr)
        latency = juce::roundToInt (oversampler->getLatencyInSamples());

    if (specOnParam->load() > 0.5f)
        latency += eeq::SpectralDynamics::latencySamples();

    if (latency != lastLatency)
    {
        lastLatency = latency;
        setLatencySamples (latency);
    }
}

void EeqAudioProcessor::toggleAB()
{
    auto current = apvts.copyState();

    if (altState.isValid())
        apvts.replaceState (altState);

    altState = current;
    slotIsB = ! slotIsB;
    rebuildMidiMapFromState();
}

void EeqAudioProcessor::copyCurrentToOtherSlot()
{
    altState = apvts.copyState().createCopy();
}

void EeqAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void EeqAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            applyLoadedState (juce::ValueTree::fromXml (*xml));
}

void EeqAudioProcessor::applyLoadedState (const juce::ValueTree& stateIn)
{
    auto state = stateIn.createCopy();
    eeq::migrateBandActiveFlags (state);
    apvts.replaceState (state);
    rebuildMidiMapFromState();
}

juce::AudioProcessorEditor* EeqAudioProcessor::createEditor()
{
    return new EeqAudioProcessorEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new EeqAudioProcessor();
}
