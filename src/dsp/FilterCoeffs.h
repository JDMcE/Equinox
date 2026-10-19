#pragma once

#include <array>
#include <cmath>
#include <complex>
#include "../Parameters.h"

namespace eeq
{

// One normalized biquad section (a0 == 1).
struct BiquadCoeffs
{
    double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;
};

// Coefficient set for one EQ band: up to maxStagesPerBand cascaded biquads.
struct BandCoeffs
{
    std::array<BiquadCoeffs, maxStagesPerBand> stages;
    int numStages = 0;
};

namespace detail
{
    inline constexpr double pi = 3.141592653589793238;

    // RBJ cookbook second-order sections ---------------------------------

    inline BiquadCoeffs rbjBell (double w0, double q, double gainDb)
    {
        const double A = std::pow (10.0, gainDb / 40.0);
        const double cs = std::cos (w0), alpha = std::sin (w0) / (2.0 * q);
        const double a0 = 1.0 + alpha / A;
        return { (1.0 + alpha * A) / a0, (-2.0 * cs) / a0, (1.0 - alpha * A) / a0,
                 (-2.0 * cs) / a0, (1.0 - alpha / A) / a0 };
    }

    inline BiquadCoeffs rbjShelf (double w0, double q, double gainDb, bool high)
    {
        const double A = std::pow (10.0, gainDb / 40.0);
        const double cs = std::cos (w0), alpha = std::sin (w0) / (2.0 * q);
        const double sqA2a = 2.0 * std::sqrt (A) * alpha;
        const double sign = high ? -1.0 : 1.0; // low shelf uses +cos terms flipped vs high shelf

        const double b0 =      A * ((A + 1.0) - sign * (A - 1.0) * cs + sqA2a);
        const double b1 = 2.0 * A * ((A - 1.0) - sign * (A + 1.0) * cs) * sign;
        const double b2 =      A * ((A + 1.0) - sign * (A - 1.0) * cs - sqA2a);
        const double a0 =           (A + 1.0) + sign * (A - 1.0) * cs + sqA2a;
        const double a1 = -2.0 *   ((A - 1.0) + sign * (A + 1.0) * cs) * sign;
        const double a2 =           (A + 1.0) + sign * (A - 1.0) * cs - sqA2a;

        return { b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0 };
    }

    inline BiquadCoeffs rbjNotch (double w0, double q)
    {
        const double cs = std::cos (w0), alpha = std::sin (w0) / (2.0 * q);
        const double a0 = 1.0 + alpha;
        return { 1.0 / a0, -2.0 * cs / a0, 1.0 / a0, -2.0 * cs / a0, (1.0 - alpha) / a0 };
    }

    inline BiquadCoeffs rbjBandPass (double w0, double q)
    {
        // Constant 0 dB peak gain band pass.
        const double cs = std::cos (w0), alpha = std::sin (w0) / (2.0 * q);
        const double a0 = 1.0 + alpha;
        return { alpha / a0, 0.0, -alpha / a0, -2.0 * cs / a0, (1.0 - alpha) / a0 };
    }

    inline BiquadCoeffs rbjAllPass (double w0, double q)
    {
        const double cs = std::cos (w0), alpha = std::sin (w0) / (2.0 * q);
        const double a0 = 1.0 + alpha;
        return { (1.0 - alpha) / a0, -2.0 * cs / a0, (1.0 + alpha) / a0,
                 -2.0 * cs / a0, (1.0 - alpha) / a0 };
    }

    inline BiquadCoeffs rbjCut2 (double w0, double q, bool highCut)
    {
        const double cs = std::cos (w0), alpha = std::sin (w0) / (2.0 * q);
        const double a0 = 1.0 + alpha;

        if (highCut) // low-pass response
            return { (1.0 - cs) * 0.5 / a0, (1.0 - cs) / a0, (1.0 - cs) * 0.5 / a0,
                     -2.0 * cs / a0, (1.0 - alpha) / a0 };

        return { (1.0 + cs) * 0.5 / a0, -(1.0 + cs) / a0, (1.0 + cs) * 0.5 / a0,
                 -2.0 * cs / a0, (1.0 - alpha) / a0 };
    }

    inline BiquadCoeffs cut1 (double freq, double sampleRate, bool highCut)
    {
        // First-order section via bilinear transform, expressed as a degenerate biquad.
        const double w = std::tan (pi * freq / sampleRate);
        const double n = 1.0 / (1.0 + w);

        if (highCut) // one-pole low-pass
            return { w * n, w * n, 0.0, (w - 1.0) * n, 0.0 };

        return { n, -n, 0.0, (w - 1.0) * n, 0.0 };
    }
} // namespace detail

// Computes the biquad cascade for a band. freq is clamped against Nyquist.
inline BandCoeffs computeBandCoeffs (FilterType type, double freq, double gainDb, double q,
                                     int slopeIndex, double sampleRate)
{
    using namespace detail;
    BandCoeffs out;

    freq = juce::jlimit ((double) minFreq, sampleRate * 0.49, freq);
    q    = juce::jlimit ((double) minQ, (double) maxQ, q);
    const double w0 = 2.0 * pi * freq / sampleRate;

    switch (type)
    {
        case FilterType::bell:
            out.stages[out.numStages++] = rbjBell (w0, q, gainDb);
            break;

        case FilterType::lowShelf:
            out.stages[out.numStages++] = rbjShelf (w0, q, gainDb, false);
            break;

        case FilterType::highShelf:
            out.stages[out.numStages++] = rbjShelf (w0, q, gainDb, true);
            break;

        case FilterType::notch:
            out.stages[out.numStages++] = rbjNotch (w0, q);
            break;

        case FilterType::bandPass:
            out.stages[out.numStages++] = rbjBandPass (w0, q);
            break;

        case FilterType::allPass:
            out.stages[out.numStages++] = rbjAllPass (w0, q);
            break;

        case FilterType::tiltShelf:
            // Tilt: low side down by gain/2, high side up by gain/2 around freq.
            out.stages[out.numStages++] = rbjShelf (w0, q, -gainDb * 0.5, false);
            out.stages[out.numStages++] = rbjShelf (w0, q,  gainDb * 0.5, true);
            break;

        case FilterType::flatTilt:
        {
            // Broad tilt across the audible spectrum, centred at ~632 Hz
            // (geometric mean of 20 Hz and 20 kHz) with a very wide Q.
            const double wc = 2.0 * pi * 632.455 / sampleRate;
            out.stages[out.numStages++] = rbjShelf (wc, 0.30, -gainDb * 0.5, false);
            out.stages[out.numStages++] = rbjShelf (wc, 0.30,  gainDb * 0.5, true);
            break;
        }

        case FilterType::lowCut:
        case FilterType::highCut:
        {
            const bool highCut = (type == FilterType::highCut);
            const int order = slopeOrders[juce::jlimit (0, slopeChoices.size() - 1, slopeIndex)];
            const int numSecond = order / 2;

            // Butterworth pole placement; the band Q parameter scales the
            // highest-Q (last) section to act as adjustable resonance.
            for (int k = 0; k < numSecond; ++k)
            {
                // Angle of the k-th pole pair from the negative real axis.
                const double psi = (order % 2 == 0) ? pi * (2.0 * k + 1.0) / (2.0 * order)
                                                    : pi * (k + 1.0) / order;
                double sectionQ = 1.0 / (2.0 * std::cos (psi));
                if (k == numSecond - 1)
                    sectionQ *= q / 0.7071067811865476;
                out.stages[out.numStages++] = rbjCut2 (w0, sectionQ, highCut);
            }

            if (order % 2 != 0)
                out.stages[out.numStages++] = cut1 (freq, sampleRate, highCut);
            break;
        }
    }

    return out;
}

// Magnitude of the cascade at a given frequency, in dB.
inline double bandMagnitudeDb (const BandCoeffs& c, double freq, double sampleRate)
{
    const double w = 2.0 * detail::pi * freq / sampleRate;
    const std::complex<double> z1 = std::polar (1.0, -w);
    const std::complex<double> z2 = z1 * z1;

    double db = 0.0;
    for (int s = 0; s < c.numStages; ++s)
    {
        const auto& st = c.stages[(size_t) s];
        const auto num = st.b0 + st.b1 * z1 + st.b2 * z2;
        const auto den = 1.0 + st.a1 * z1 + st.a2 * z2;
        db += 20.0 * std::log10 (std::abs (num) / juce::jmax (1.0e-30, std::abs (den)));
    }
    return db;
}

} // namespace eeq
