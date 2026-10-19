#pragma once

#include <juce_dsp/juce_dsp.h>
#include <array>
#include <atomic>
#include <complex>
#include <vector>
#include "FilterCoeffs.h"

namespace eeq
{

// Snapshot of one band's parameters, taken on the message thread.
struct BandSnapshot
{
    bool on = false;
    FilterType type = FilterType::bell;
    float freq = 1000.0f, gain = 0.0f, q = 1.0f;
    int slope = defaultSlopeIndex;
    ChannelMode chan = ChannelMode::stereo;
};

// Linear phase EQ engine.
//
// The message thread turns the current band setup into FIR kernels
// (rebuild()); the audio thread convolves with them via FFT overlap-add
// (process()). Because bands can address L/R/M/S, the EQ as a whole is a
// 2x2 linear system: [L'; R'] = [[Kll, Klr], [Krl, Krr]] * [L; R]. All four
// kernels are built; the cross kernels are skipped when no band needs them.
//
// Latency: firLength / 2 (symmetric kernel) + firLength (block buffering).
class LinearPhaseEngine
{
public:
    // Everything is allocated up front for the worst case so that rebuild()
    // (message thread) and process() (audio thread) are safe to call even if
    // they race plugin preparation.
    LinearPhaseEngine()
    {
        for (int order = minOrder; order <= maxOrder; ++order)
            ffts[(size_t) (order - minOrder)] = std::make_unique<juce::dsp::FFT> (order);

        const int maxFft = maxFirLength * 2;
        for (auto* v : { &timeA, &timeB, &freqA, &freqB, &freqC, &freqD,
                         &buildTime, &buildSpec })
            v->resize ((size_t) maxFft);

        for (auto& s : slots)
            for (auto* k : { &s.ka, &s.kb, &s.kc, &s.kd })
                k->resize ((size_t) maxFft);

        const int ringSize = juce::nextPowerOfTwo (maxFirLength * 2 + maxSupportedBlock);
        for (auto& ch : channels)
        {
            ch.inBuf.assign ((size_t) maxFirLength, 0.0f);
            ch.tail.assign ((size_t) maxFirLength, 0.0f);
            ch.outRing.assign ((size_t) ringSize, 0.0f);
        }
        ringMask = ringSize - 1;

        reset();
    }

    void prepare (int) { reset(); }

    void reset()
    {
        inCount = 0;
        currentLen = 0; // forces re-sync with the active kernel set
        activeSlot.store (-1);
    }

    static int latencyForLength (int firLen) noexcept { return firLen + firLen / 2; }

    // ---- audio thread ------------------------------------------------------

    // Returns false when no kernels are ready yet (caller should bypass).
    bool process (float* l, float* r, int numSamples)
    {
        if (numSamples > maxSupportedBlock)
            return false;

        const int slot = activeSlot.load (std::memory_order_acquire);
        if (slot < 0)
            return false;

        const auto& k = slots[(size_t) slot];

        if (k.firLen != currentLen)
            syncToLength (k.firLen);

        float* io[2] = { l, r };
        int done = 0;

        while (done < numSamples)
        {
            const int todo = juce::jmin (numSamples - done, currentLen - inCount);

            for (int ch = 0; ch < 2; ++ch)
                std::copy (io[ch] + done, io[ch] + done + todo,
                           channels[(size_t) ch].inBuf.begin() + inCount);

            inCount += todo;
            done += todo;

            if (inCount == currentLen)
            {
                processChunk (k);
                inCount = 0;
            }
        }

        for (int ch = 0; ch < 2; ++ch)
        {
            auto& c = channels[(size_t) ch];
            for (int i = 0; i < numSamples; ++i)
            {
                io[(size_t) ch][i] = c.outRing[(size_t) (readPos + i) & (size_t) ringMask];
                c.outRing[(size_t) (readPos + i) & (size_t) ringMask] = 0.0f;
            }
        }
        readPos = (readPos + numSamples) & ringMask;

        return true;
    }

    // ---- message thread ----------------------------------------------------

    void rebuild (const std::array<BandSnapshot, numBands>& bands, double sampleRate,
                  int firLen, float gainScale)
    {
        firLen = juce::jlimit (1 << minFirOrder, maxFirLength, firLen);
        const int fftLen = firLen * 2;
        const int half = firLen / 2;

        // 1. Sample the 2x2 magnitude matrix at firLen/2+1 frequencies.
        magLL.assign ((size_t) half + 1, 1.0f);
        magLR.assign ((size_t) half + 1, 0.0f);
        magRL.assign ((size_t) half + 1, 0.0f);
        magRR.assign ((size_t) half + 1, 1.0f);

        bool hasCross = false;

        for (const auto& b : bands)
        {
            if (! b.on)
                continue;

            const auto coeffs = computeBandCoeffs (b.type, b.freq, b.gain * gainScale,
                                                   b.q, b.slope, sampleRate);

            for (int bin = 0; bin <= half; ++bin)
            {
                const double freq = juce::jlimit (0.0, sampleRate * 0.499,
                                                  bin * sampleRate / firLen);
                const float g = (float) std::pow (10.0,
                                    bandMagnitudeDb (coeffs, freq, sampleRate) / 20.0);

                // Band matrix in the L/R basis.
                float a = 1.0f, bb = 0.0f, c = 0.0f, d = 1.0f;
                switch (b.chan)
                {
                    case ChannelMode::stereo: a = g; d = g; break;
                    case ChannelMode::left:   a = g; break;
                    case ChannelMode::right:  d = g; break;
                    case ChannelMode::mid:
                        a = d = 0.5f * (g + 1.0f);
                        bb = c = 0.5f * (g - 1.0f);
                        break;
                    case ChannelMode::side:
                        a = d = 0.5f * (g + 1.0f);
                        bb = c = 0.5f * (1.0f - g);
                        break;
                }

                // T = M * T
                const size_t i = (size_t) bin;
                const float tll = magLL[i], tlr = magLR[i], trl = magRL[i], trr = magRR[i];
                magLL[i] = a * tll + bb * trl;
                magLR[i] = a * tlr + bb * trr;
                magRL[i] = c * tll + d * trl;
                magRR[i] = c * tlr + d * trr;
            }

            if (b.chan != ChannelMode::stereo)
                hasCross = true;
        }

        // 2. Build the four kernels into the inactive slot.
        const int current = activeSlot.load();
        const int target = current == 0 ? 1 : 0;
        auto& s = slots[(size_t) target];

        s.firLen = firLen;
        s.fftLen = fftLen;
        s.hasCross = hasCross;

        buildKernel (magLL, s.ka, firLen);
        buildKernel (magRR, s.kd, firLen);

        if (hasCross)
        {
            buildKernel (magLR, s.kb, firLen);
            buildKernel (magRL, s.kc, firLen);
        }

        activeSlot.store (target, std::memory_order_release);
    }

private:
    static constexpr int minFirOrder = 10;   // FIR 1024
    static constexpr int minOrder = minFirOrder; // kernel build FFTs are FIR-sized,
    static constexpr int maxOrder = 15;           // convolution FFTs are 2x (32768 max)
    static constexpr int maxSupportedBlock = 16384;

    struct KernelSet
    {
        int firLen = 0, fftLen = 0;
        bool hasCross = false;
        std::vector<std::complex<float>> ka, kb, kc, kd;
    };

    struct ChannelState
    {
        std::vector<float> inBuf, tail, outRing;
    };

    juce::dsp::FFT& fftFor (int fftLen)
    {
        const int order = juce::jlimit (minOrder, maxOrder,
                                        juce::roundToInt (std::log2 ((double) fftLen)));
        return *ffts[(size_t) (order - minOrder)];
    }

    // Builds a windowed linear phase kernel from magnitudes and stores its
    // spectrum (FFT size 2 * firLen).
    void buildKernel (const std::vector<float>& mags, std::vector<std::complex<float>>& out,
                      int firLen)
    {
        const int fftLen = firLen * 2;
        const int half = firLen / 2;

        // Zero-phase spectrum with a firLen/2 delay: H[k] = M[k] * (-1)^k.
        std::fill (buildSpec.begin(), buildSpec.begin() + firLen, std::complex<float>());
        for (int k = 0; k <= half; ++k)
        {
            const float v = (k & 1) != 0 ? -mags[(size_t) k] : mags[(size_t) k];
            buildSpec[(size_t) k] = v;
            if (k > 0 && k < half)
                buildSpec[(size_t) (firLen - k)] = v;
        }

        fftFor (firLen).perform (buildSpec.data(), buildTime.data(), true); // -> impulse

        // Window (Hann) and zero-pad to the convolution FFT size.
        for (int n = 0; n < firLen; ++n)
        {
            const float w = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi
                                                    * (float) n / (float) firLen);
            buildSpec[(size_t) n] = buildTime[(size_t) n].real() * w;
        }
        std::fill (buildSpec.begin() + firLen, buildSpec.begin() + fftLen, std::complex<float>());

        fftFor (fftLen).perform (buildSpec.data(), out.data(), false);
    }

    void syncToLength (int firLen)
    {
        currentLen = firLen;
        inCount = 0;
        readPos = 0;

        for (auto& c : channels)
        {
            std::fill (c.inBuf.begin(), c.inBuf.end(), 0.0f);
            std::fill (c.tail.begin(), c.tail.end(), 0.0f);
            std::fill (c.outRing.begin(), c.outRing.end(), 0.0f);
        }

        // Prime one block of silence: output lags input by firLen samples.
        writePos = firLen & ringMask;
    }

    void processChunk (const KernelSet& k)
    {
        const int L = k.firLen, F = k.fftLen;
        auto& fft = fftFor (F);

        // Forward FFTs of both channels.
        for (int n = 0; n < L; ++n)
            timeA[(size_t) n] = channels[0].inBuf[(size_t) n];
        std::fill (timeA.begin() + L, timeA.begin() + F, std::complex<float>());
        fft.perform (timeA.data(), freqA.data(), false); // X_L

        for (int n = 0; n < L; ++n)
            timeA[(size_t) n] = channels[1].inBuf[(size_t) n];
        std::fill (timeA.begin() + L, timeA.begin() + F, std::complex<float>());
        fft.perform (timeA.data(), freqB.data(), false); // X_R

        // Y_L = Ka*X_L (+ Kb*X_R), Y_R = Kd*X_R (+ Kc*X_L)
        for (int i = 0; i < F; ++i)
        {
            const auto xl = freqA[(size_t) i], xr = freqB[(size_t) i];
            auto yl = k.ka[(size_t) i] * xl;
            auto yr = k.kd[(size_t) i] * xr;
            if (k.hasCross)
            {
                yl += k.kb[(size_t) i] * xr;
                yr += k.kc[(size_t) i] * xl;
            }
            freqC[(size_t) i] = yl;
            freqD[(size_t) i] = yr;
        }

        // Inverse FFTs, overlap-add into the output rings.
        fft.perform (freqC.data(), timeA.data(), true);
        fft.perform (freqD.data(), timeB.data(), true);

        const std::complex<float>* results[2] = { timeA.data(), timeB.data() };

        for (int ch = 0; ch < 2; ++ch)
        {
            auto& c = channels[(size_t) ch];
            for (int n = 0; n < L; ++n)
            {
                const float v = results[ch][(size_t) n].real() + c.tail[(size_t) n];
                c.outRing[(size_t) (writePos + n) & (size_t) ringMask] += v;
                c.tail[(size_t) n] = results[ch][(size_t) (n + L)].real();
            }
        }
        writePos = (writePos + L) & ringMask;
    }

    std::array<std::unique_ptr<juce::dsp::FFT>, maxOrder - minOrder + 1> ffts;
    std::array<KernelSet, 2> slots;
    std::atomic<int> activeSlot { -1 };

    // Message-thread scratch (rebuild / buildKernel only).
    std::vector<float> magLL, magLR, magRL, magRR;
    std::vector<std::complex<float>> buildTime, buildSpec;

    // Audio-thread scratch (processChunk only).
    std::vector<std::complex<float>> timeA, timeB, freqA, freqB, freqC, freqD;

    std::array<ChannelState, 2> channels;
    int ringMask = 0;
    int currentLen = 0, inCount = 0, readPos = 0, writePos = 0;
};

} // namespace eeq
