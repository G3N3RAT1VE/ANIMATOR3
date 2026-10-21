#pragma once

#include <juce_core/juce_core.h>
#include <array>
#include <cmath>
#include <vector>

namespace riq
{

// Lookahead true-peak limiter.
//  - inter-sample peaks are estimated with 4x polyphase windowed-sinc interpolation,
//    so the ceiling holds in dBTP, not just per sample;
//  - gain = moving average (length L) of a running minimum (length L+1) of the required
//    gain, applied to the signal delayed by L: the average never exceeds the gain needed
//    at the delayed sample, so there is no overshoot and no attack distortion;
//  - below the ceiling the gain is exactly 1.0, i.e. the audio is passed bit-exact.
class TruePeakLimiter
{
public:
    static constexpr int kHalfTaps = 6; // 12 taps per phase
    static constexpr int kPhases = 4;

    void prepare (double sampleRate)
    {
        sr = sampleRate;
        lookahead = juce::jmax (16, (int) std::round (sr * 0.0015));

        // windowed-sinc coefficients for the fractional positions 1/4, 2/4, 3/4
        for (int p = 1; p < kPhases; ++p)
        {
            const double frac = (double) p / kPhases;
            for (int k = 0; k < 2 * kHalfTaps; ++k)
            {
                const double x = (double) (k - kHalfTaps + 1) - frac; // tap at m + (k - K + 1)
                const double sinc = std::abs (x) < 1.0e-9 ? 1.0 : std::sin (juce::MathConstants<double>::pi * x) / (juce::MathConstants<double>::pi * x);
                const double w = 0.5 * (1.0 + std::cos (juce::MathConstants<double>::pi * x / (kHalfTaps + 0.5)));
                coef[(size_t) p][(size_t) k] = (float) (sinc * w);
            }
        }

        size = juce::nextPowerOfTwo (lookahead + 4 * kHalfTaps + 8);
        for (auto& b : hist)
            b.assign ((size_t) size, 0.0f);
        need.assign ((size_t) size, 1.0f);
        held.assign ((size_t) size, 1.0f);
        pos = 0;
        heldSum = (double) lookahead;
        nonUnity = 0;
        gain = 1.0;
    }

    int getLatency() const noexcept { return lookahead + kHalfTaps; }
    float getGainReductionDb() const noexcept { return grDb; }

    // In place. When !enabled the signal is only delayed (latency stays constant).
    void process (float* l, float* r, int n, bool enabled, float drive, float ceiling, float releaseMs) noexcept
    {
        const int mask = size - 1;
        const float relCoef = (float) std::exp (-1.0 / (juce::jmax (1.0f, releaseMs) * 0.001 * sr));
        const float pre = enabled ? drive : 1.0f;
        float minGain = 1.0f;

        for (int i = 0; i < n; ++i)
        {
            hist[0][(size_t) pos] = l[i] * pre;
            hist[1][(size_t) pos] = r[i] * pre;

            // True peak of sample m = pos - K (needs K samples of future).
            const int m = (pos - kHalfTaps) & mask;
            float tp = juce::jmax (std::abs (hist[0][(size_t) m]), std::abs (hist[1][(size_t) m]));
            if (enabled)
            {
                for (int p = 1; p < kPhases; ++p)
                {
                    float yl = 0.0f, yr = 0.0f;
                    for (int k = 0; k < 2 * kHalfTaps; ++k)
                    {
                        const int idx = (m + k - kHalfTaps + 1) & mask;
                        yl += coef[(size_t) p][(size_t) k] * hist[0][(size_t) idx];
                        yr += coef[(size_t) p][(size_t) k] * hist[1][(size_t) idx];
                    }
                    tp = juce::jmax (tp, std::abs (yl), std::abs (yr));
                }
            }
            need[(size_t) m] = (enabled && tp > ceiling) ? ceiling / tp : 1.0f;

            // running minimum over need[m - L .. m]
            float h = 1.0f;
            for (int k = 0; k <= lookahead; ++k)
                h = juce::jmin (h, need[(size_t) ((m - k) & mask)]);

            // moving average of the held minimum over L samples
            const float leaving = held[(size_t) ((m - lookahead) & mask)];
            heldSum += (double) h - (double) leaving;
            nonUnity += (h < 1.0f ? 1 : 0) - (leaving < 1.0f ? 1 : 0);
            held[(size_t) m] = h;
            if (nonUnity == 0)
                heldSum = (double) lookahead; // drop accumulated rounding: exactly unity again
            const float avg = nonUnity == 0 ? 1.0f : juce::jmin (1.0f, (float) (heldSum / (double) lookahead));

            // double state: a float stalls just below 1.0 because the release step
            // becomes smaller than float resolution there
            gain = avg < gain ? (double) avg : gain + ((double) avg - gain) * (1.0 - (double) relCoef);
            if (gain > avg)
                gain = avg; // release may only ever lag below the safe gain
            if (avg >= 1.0f && gain > 0.99999)
                gain = 1.0;
            const auto g = (float) gain;

            const int out = (m - lookahead) & mask;
            float yl = hist[0][(size_t) out], yr = hist[1][(size_t) out];
            if (enabled)
            {
                yl = juce::jlimit (-ceiling, ceiling, yl * g);
                yr = juce::jlimit (-ceiling, ceiling, yr * g);
            }
            l[i] = yl;
            r[i] = yr;
            minGain = juce::jmin (minGain, g);
            pos = (pos + 1) & mask;
        }

        grDb = juce::Decibels::gainToDecibels (minGain, -60.0f);
    }

private:
    double sr = 48000.0;
    int lookahead = 72, size = 256, pos = 0;
    std::array<std::array<float, 2 * kHalfTaps>, kPhases> coef {};
    std::array<std::vector<float>, 2> hist;
    std::vector<float> need, held;
    double heldSum = 72.0;
    int nonUnity = 0;
    double gain = 1.0;
    float grDb = 0.0f;
};

// ITU-R BS.1770 short-term loudness (3 s window) and sample-peak follower.
class LoudnessMeter
{
public:
    void prepare (double sampleRate)
    {
        // K-weighting (pre-filter shelf + RLB high-pass), as in libebur128
        {
            const double f0 = 1681.974450955533, G = 3.999843853973347, Q = 0.7071752369554196;
            const double K = std::tan (juce::MathConstants<double>::pi * f0 / sampleRate);
            const double Vh = std::pow (10.0, G / 20.0), Vb = std::pow (Vh, 0.4996667741545416);
            const double a0 = 1.0 + K / Q + K * K;
            s1 = { (Vh + Vb * K / Q + K * K) / a0, 2.0 * (K * K - Vh) / a0, (Vh - Vb * K / Q + K * K) / a0,
                   2.0 * (K * K - 1.0) / a0, (1.0 - K / Q + K * K) / a0 };
        }
        {
            const double f0 = 38.13547087602444, Q = 0.5003270373238773;
            const double K = std::tan (juce::MathConstants<double>::pi * f0 / sampleRate);
            const double a0 = 1.0 + K / Q + K * K;
            s2 = { 1.0, -2.0, 1.0, 2.0 * (K * K - 1.0) / a0, (1.0 - K / Q + K * K) / a0 };
        }
        blockLen = juce::jmax (1, (int) std::round (sampleRate * 0.1));
        blocks.fill (0.0);
        z = {};
        acc = 0.0;
        accN = 0;
        blockIdx = 0;
        filled = 0;
    }

    void process (const float* l, const float* r, int n) noexcept
    {
        float pk = 0.0f;
        for (int i = 0; i < n; ++i)
        {
            const double xl = run (run ((double) l[i], s1, z[0]), s2, z[1]);
            const double xr = run (run ((double) r[i], s1, z[2]), s2, z[3]);
            acc += xl * xl + xr * xr;
            pk = juce::jmax (pk, std::abs (l[i]), std::abs (r[i]));
            if (++accN >= blockLen)
            {
                blocks[(size_t) blockIdx] = acc / accN;
                blockIdx = (blockIdx + 1) % (int) blocks.size();
                filled = juce::jmin (filled + 1, (int) blocks.size());
                acc = 0.0;
                accN = 0;
                double sum = 0.0;
                for (int b = 0; b < filled; ++b)
                    sum += blocks[(size_t) b];
                const double ms = sum / juce::jmax (1, filled);
                shortTerm = ms > 1.0e-12 ? (float) (-0.691 + 10.0 * std::log10 (ms)) : -70.0f;
            }
        }
        peak = juce::jmax (pk, peak * 0.97f);
    }

    float getShortTermLufs() const noexcept { return shortTerm; }
    float getPeakDb() const noexcept { return juce::Decibels::gainToDecibels (peak, -60.0f); }

private:
    struct Biquad { double b0, b1, b2, a1, a2; };
    struct State { double x1 = 0, x2 = 0, y1 = 0, y2 = 0; };

    static double run (double x, const Biquad& c, State& s) noexcept
    {
        const double y = c.b0 * x + c.b1 * s.x1 + c.b2 * s.x2 - c.a1 * s.y1 - c.a2 * s.y2;
        s.x2 = s.x1; s.x1 = x; s.y2 = s.y1; s.y1 = y;
        return y;
    }

    Biquad s1 {}, s2 {};
    std::array<State, 4> z {};
    std::array<double, 30> blocks {};
    double acc = 0.0;
    int accN = 0, blockLen = 4800, blockIdx = 0, filled = 0;
    float shortTerm = -70.0f, peak = 0.0f;
};

} // namespace riq
