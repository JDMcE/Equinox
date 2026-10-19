#pragma once

#include "FilterCoeffs.h"

namespace eeq
{

// Runtime state for one EQ band: coefficient cascade plus per-channel
// biquad state (transposed direct form II). Channel 0 = L (or M), 1 = R (or S).
class EqBand
{
public:
    void prepare (double newSampleRate)
    {
        sampleRate = newSampleRate;
        lastKey = {};
        reset();
    }

    void reset()
    {
        for (auto& ch : state)
            for (auto& st : ch)
                st = {};
    }

    // Called once per block from the audio thread. rateOverride replaces the
    // prepared sample rate when the band runs inside an oversampled chain.
    void update (bool on, FilterType newType, float freq, float gainDb, float q,
                 int slopeIndex, ChannelMode newMode, double rateOverride = 0.0)
    {
        const bool becameActive = on && ! active;
        active = on;
        type = newType;
        mode = newMode;

        if (active)
        {
            const double rate = rateOverride > 0.0 ? rateOverride : sampleRate;
            const Key key { newType, freq, gainDb, q, slopeIndex, rate };

            if (key != lastKey)
            {
                coeffs = computeBandCoeffs (type, freq, gainDb, q, slopeIndex, rate);
                lastKey = key;
            }

            if (becameActive)
                reset();
        }
    }

    bool isActive() const noexcept { return active; }
    const BandCoeffs& getCoeffs() const noexcept { return coeffs; }

    void processBlock (float* l, float* r, int numSamples) noexcept
    {
        if (! active || coeffs.numStages == 0)
            return;

        switch (mode)
        {
            case ChannelMode::stereo:
                for (int i = 0; i < numSamples; ++i)
                {
                    l[i] = (float) processSample (0, l[i]);
                    r[i] = (float) processSample (1, r[i]);
                }
                break;

            case ChannelMode::left:
                for (int i = 0; i < numSamples; ++i)
                    l[i] = (float) processSample (0, l[i]);
                break;

            case ChannelMode::right:
                for (int i = 0; i < numSamples; ++i)
                    r[i] = (float) processSample (1, r[i]);
                break;

            case ChannelMode::mid:
                for (int i = 0; i < numSamples; ++i)
                {
                    const double m = processSample (0, 0.5 * ((double) l[i] + r[i]));
                    const double s = 0.5 * ((double) l[i] - r[i]);
                    l[i] = (float) (m + s);
                    r[i] = (float) (m - s);
                }
                break;

            case ChannelMode::side:
                for (int i = 0; i < numSamples; ++i)
                {
                    const double m = 0.5 * ((double) l[i] + r[i]);
                    const double s = processSample (1, 0.5 * ((double) l[i] - r[i]));
                    l[i] = (float) (m + s);
                    r[i] = (float) (m - s);
                }
                break;
        }
    }

private:
    struct StageState { double z1 = 0.0, z2 = 0.0; };

    double processSample (int channel, double x) noexcept
    {
        for (int s = 0; s < coeffs.numStages; ++s)
        {
            const auto& c = coeffs.stages[(size_t) s];
            auto& st = state[(size_t) channel][(size_t) s];

            const double y = c.b0 * x + st.z1;
            st.z1 = c.b1 * x - c.a1 * y + st.z2;
            st.z2 = c.b2 * x - c.a2 * y;
            x = y;
        }
        return x;
    }

    struct Key
    {
        FilterType type = (FilterType) -1;
        float freq = 0.0f, gain = 0.0f, q = 0.0f;
        int slope = -1;
        double rate = 0.0;

        bool operator!= (const Key& o) const
        {
            return type != o.type || freq != o.freq || gain != o.gain
                || q != o.q || slope != o.slope || rate != o.rate;
        }
    };

    BandCoeffs coeffs;
    Key lastKey;
    std::array<std::array<StageState, maxStagesPerBand>, 2> state;
    double sampleRate = 44100.0;
    FilterType type = FilterType::bell;
    ChannelMode mode = ChannelMode::stereo;
    bool active = false;
};

} // namespace eeq
