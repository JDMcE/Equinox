#pragma once

#include <juce_dsp/juce_dsp.h>
#include <array>
#include <complex>
#include <vector>

namespace eeq
{

// Spectral dynamics: STFT processing that attenuates frequency bins which
// stick out above their spectral neighbourhood (+/- 1/6 octave average) --
// i.e. resonances and other problem frequencies -- while leaving balanced
// material untouched.
//
//  - sensitivity (dB): how far a bin must exceed its neighbourhood before
//    reduction starts (lower = more aggressive)
//  - amount (dB): maximum reduction applied to a bin
//
// Stereo-linked detection (max of both channels), identical gain on both
// channels. FFT 2048, hop 512, Hann analysis/synthesis. Latency: fftSize.
class SpectralDynamics
{
public:
    SpectralDynamics()
    {
        window.resize (fftSize);
        for (int n = 0; n < fftSize; ++n)
            window[(size_t) n] = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi
                                                         * (float) n / (float) fftSize);

        for (auto& c : ch)
        {
            c.inRing.assign (fftSize, 0.0f);
            c.outAccum.assign (fftSize + hopSize, 0.0f);
            c.outRing.assign (ringSize, 0.0f);
            c.frame.resize (fftSize);
            c.spec.resize (fftSize);
        }

        magsDb.assign (numBins, -100.0f);
        neighDb.assign (numBins, -100.0f);
        prefix.assign (numBins + 1, 0.0);
        gainsDb.assign (numBins, 0.0f);
        gainsSmoothed.assign (numBins, 0.0f);

        // +/- 1/6 octave neighbourhood bounds per bin, precomputed.
        loBin.resize (numBins);
        hiBin.resize (numBins);
        const double r = std::pow (2.0, 1.0 / 6.0);
        for (int k = 0; k < numBins; ++k)
        {
            loBin[(size_t) k] = juce::jlimit (1, numBins - 1, (int) std::floor (k / r));
            hiBin[(size_t) k] = juce::jlimit (1, numBins - 1, (int) std::ceil (k * r));
        }

        reset();
    }

    static constexpr int latencySamples() noexcept { return fftSize; }

    void reset()
    {
        for (auto& c : ch)
        {
            std::fill (c.inRing.begin(), c.inRing.end(), 0.0f);
            std::fill (c.outAccum.begin(), c.outAccum.end(), 0.0f);
            std::fill (c.outRing.begin(), c.outRing.end(), 0.0f);
        }
        std::fill (gainsDb.begin(), gainsDb.end(), 0.0f);
        std::fill (gainsSmoothed.begin(), gainsSmoothed.end(), 0.0f);

        inPos = 0;
        sinceHop = 0;
        outRead = 0;
        frameWrite = fftSize; // prime: output lags input by fftSize
    }

    void process (float* l, float* r, int numSamples, float amountDb, float sensitivityDb)
    {
        float* io[2] = { l, r };

        for (int i = 0; i < numSamples; ++i)
        {
            for (int c = 0; c < 2; ++c)
                ch[(size_t) c].inRing[(size_t) inPos] = io[c][i];

            inPos = (inPos + 1) % fftSize;

            if (++sinceHop >= hopSize)
            {
                sinceHop = 0;
                processFrame (amountDb, sensitivityDb);
            }
        }

        for (int c = 0; c < 2; ++c)
        {
            auto& s = ch[(size_t) c];
            for (int i = 0; i < numSamples; ++i)
            {
                const size_t idx = (size_t) ((outRead + i) & ringMask);
                io[c][i] = s.outRing[idx];
                s.outRing[idx] = 0.0f;
            }
        }
        outRead = (outRead + numSamples) & ringMask;
    }

private:
    static constexpr int fftOrder = 11;
    static constexpr int fftSize = 1 << fftOrder; // 2048
    static constexpr int hopSize = fftSize / 4;   // 75 % overlap
    static constexpr int numBins = fftSize / 2;
    static constexpr int ringSize = 1 << 15;      // 32768 > fftSize + max block
    static constexpr int ringMask = ringSize - 1;
    // Hann^2 with 75 % overlap sums to 1.5.
    static constexpr float colaScale = 1.0f / 1.5f;

    struct Channel
    {
        std::vector<float> inRing, outAccum, outRing;
        std::vector<std::complex<float>> frame, spec;
    };

    void processFrame (float amountDb, float sensitivityDb)
    {
        // Assemble windowed frames (most recent fftSize samples) and FFT.
        for (int c = 0; c < 2; ++c)
        {
            auto& s = ch[(size_t) c];
            for (int n = 0; n < fftSize; ++n)
            {
                const int idx = (inPos + n) % fftSize; // oldest -> newest
                s.frame[(size_t) n] = s.inRing[(size_t) idx] * window[(size_t) n];
            }
            fft.perform (s.frame.data(), s.spec.data(), false);
        }

        // Stereo-linked magnitudes in dB (floored to keep averages sane).
        for (int k = 0; k < numBins; ++k)
        {
            const float m = juce::jmax (std::abs (ch[0].spec[(size_t) k]),
                                        std::abs (ch[1].spec[(size_t) k]));
            magsDb[(size_t) k] = juce::jmax (-100.0f,
                juce::Decibels::gainToDecibels (m / (float) fftSize * 4.0f, -100.0f));
        }

        // Neighbourhood average via prefix sums.
        prefix[0] = 0.0;
        for (int k = 0; k < numBins; ++k)
            prefix[(size_t) k + 1] = prefix[(size_t) k] + magsDb[(size_t) k];

        for (int k = 1; k < numBins; ++k)
        {
            const int lo = loBin[(size_t) k], hi = hiBin[(size_t) k];
            neighDb[(size_t) k] = (float) ((prefix[(size_t) hi + 1] - prefix[(size_t) lo])
                                           / (double) (hi - lo + 1));
        }

        // Per-bin target reduction with temporal smoothing.
        for (int k = 1; k < numBins; ++k)
        {
            const float excess = magsDb[(size_t) k] - neighDb[(size_t) k] - sensitivityDb;
            const float target = -juce::jmin (amountDb, juce::jmax (0.0f, excess));

            auto& g = gainsDb[(size_t) k];
            g += (target < g ? attCoef : relCoef) * (target - g);
        }

        // Mild spectral smoothing of the gain curve (3-bin box) to avoid
        // isolated-bin artefacts.
        gainsSmoothed[0] = 0.0f;
        for (int k = 1; k < numBins; ++k)
        {
            const float a = gainsDb[(size_t) juce::jmax (1, k - 1)];
            const float b = gainsDb[(size_t) k];
            const float c = gainsDb[(size_t) juce::jmin (numBins - 1, k + 1)];
            gainsSmoothed[(size_t) k] = (a + b + c) * (1.0f / 3.0f);
        }

        // Apply (conjugate-symmetric), inverse FFT, overlap-add.
        for (int c = 0; c < 2; ++c)
        {
            auto& s = ch[(size_t) c];

            for (int k = 1; k < numBins; ++k)
            {
                const float lin = juce::Decibels::decibelsToGain (gainsSmoothed[(size_t) k]);
                s.spec[(size_t) k] *= lin;
                s.spec[(size_t) (fftSize - k)] *= lin;
            }

            fft.perform (s.spec.data(), s.frame.data(), true);

            // Overlap-add with synthesis window; emit the first hop.
            for (int n = 0; n < fftSize; ++n)
                s.outAccum[(size_t) n] += s.frame[(size_t) n].real()
                                        * window[(size_t) n] * colaScale;

            for (int n = 0; n < hopSize; ++n)
                s.outRing[(size_t) ((frameWrite + n) & ringMask)] = s.outAccum[(size_t) n];

            std::move (s.outAccum.begin() + hopSize, s.outAccum.end(), s.outAccum.begin());
            std::fill (s.outAccum.end() - hopSize, s.outAccum.end(), 0.0f);
        }
        frameWrite = (frameWrite + hopSize) & ringMask;
    }

    juce::dsp::FFT fft { fftOrder };
    std::vector<float> window;
    std::array<Channel, 2> ch;

    std::vector<float> magsDb, neighDb, gainsDb, gainsSmoothed;
    std::vector<double> prefix;
    std::vector<int> loBin, hiBin;

    static constexpr float attCoef = 0.75f; // per frame (~10 ms hop)
    static constexpr float relCoef = 0.18f;

    int inPos = 0, sinceHop = 0, outRead = 0, frameWrite = 0;
};

} // namespace eeq
