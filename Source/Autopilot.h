#pragma once

#include "StemEngine.h"

namespace riq
{

// Generative autopilot, as a pure function of the (fractional) bar position: the audio thread
// uses it to drive the stems, the editor uses the very same function to draw the phrase map.
//
// Like Rhythmiq's Beat Assistant, IQ takes over the stem knobs and moves them over time;
// because every knob step has its own arrangement (see seedIndex in PluginProcessor.h), a
// drifting knob walks through different patterns instead of just adding or removing edits.
//  - drift: IQ and the linked stems' Arrange / Ratchet / Reverse (optionally Silence and
//    slice size) follow smoothed random walks, each stem on its own walk; new targets every
//    RATE bars, gliding (SMOOTH) or stepping on the grid; never periodic
//  - build / fill shift the centre of the walk up across the phrase, peaking in the last bar
//  - drop: a stem sits out a bar now and then; IQ Evolve re-rolls the current step's pattern
struct AutopilotInput
{
    float iq = 0.0f;
    bool iqEvolve = false, iqReverse = false;
    std::array<bool, 3> linked { true, true, true };
    std::array<float, 3> arrange {}, ratchet {}, reverse {}, silence {};
    float drift = 0.5f, build = 0.5f, fill = 0.6f, ratchetAmount = 0.4f, revDepth = 0.3f, drop = 0.0f, evolveChance = 0.5f;
    double rateBars = 2.0;
    bool smooth = true, driftRatchet = true, driftReverse = true, driftSilence = false, driftSlice = false;
    juce::uint32 seed = 0x5eed;
    int phraseBars = 8, evolveEvery = 4;
};

struct AutopilotStem
{
    float arrange = 0.0f, ratchet = 0.0f, reverse = 0.0f, silence = 0.0f;
    int sliceShift = 0; // -1 coarser, +1 finer
    bool drop = false, evolve = false;
};

struct AutopilotBar
{
    std::array<AutopilotStem, 3> stems;
    float iqNow = 0.0f; // IQ after drift
    int phrasePos = 0;
    bool fillBar = false, halfFillBar = false;
};

// Smoothed value noise in [-1, 1]: knots every `rate` bars plus a finer octave.
inline float driftNoise (juce::uint32 seed, juce::uint32 lane, double barPos, double rate, bool smooth) noexcept
{
    auto octave = [&] (double r, juce::uint32 salt)
    {
        const double x = barPos / juce::jmax (0.125, r);
        const double i = std::floor (x);
        const float f = (float) (x - i);
        const float k0 = hash01 (seed ^ salt, lane, (juce::uint32) (juce::int64) i) * 2.0f - 1.0f;
        if (! smooth)
            return k0;
        const float k1 = hash01 (seed ^ salt, lane, (juce::uint32) ((juce::int64) i + 1)) * 2.0f - 1.0f;
        const float t = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::pi * f);
        return k0 + (k1 - k0) * t;
    };
    return juce::jlimit (-1.0f, 1.0f, 0.7f * octave (rate, 0x9e3779b9u) + 0.3f * octave (rate * 0.5, 0x7f4a7c15u));
}

inline AutopilotBar planAutopilot (const AutopilotInput& in, double barPos) noexcept
{
    AutopilotBar out;
    const auto barIdx = (juce::int64) std::floor (barPos);
    const int pb = juce::jmax (1, in.phraseBars);
    out.phrasePos = (int) (((barIdx % pb) + pb) % pb);
    out.fillBar = out.phrasePos == pb - 1;
    out.halfFillBar = pb >= 8 && out.phrasePos == pb / 2 - 1;
    const auto bar = (juce::uint32) barIdx;
    const bool engaged = in.iq > 0.0f;
    const float depth = in.drift;

    // IQ itself drifts around the knob position.
    out.iqNow = engaged ? juce::jlimit (0.0f, 1.0f, in.iq + depth * 0.35f * driftNoise (in.seed, 99u, barPos, in.rateBars * 2.0, in.smooth))
                        : 0.0f;

    // Build / fill: centre of the walk rises across the phrase and peaks in the last bar.
    const float progress = pb > 1 ? (float) out.phrasePos / (float) (pb - 1) : 1.0f;
    float energy = (1.0f - in.build) * 0.6f + in.build * (0.25f + 0.75f * progress);
    if (out.fillBar)
        energy += in.fill * 0.5f;
    else if (out.halfFillBar)
        energy += in.fill * 0.2f;

    // Drops: a linked stem sits out a bar now and then, never on the phrase start,
    // and at least one linked stem always keeps playing.
    std::array<bool, 3> drop {};
    if (engaged && in.drop > 0.0f && out.phrasePos != 0)
    {
        int keep = -1, linkedCount = 0, droppedCount = 0;
        float best = 2.0f;
        for (int s = 0; s < 3; ++s)
        {
            if (! in.linked[(size_t) s])
                continue;
            ++linkedCount;
            const float h = hash01 (in.seed ^ bar, (juce::uint32) s, 41u);
            drop[(size_t) s] = h < in.drop * 0.5f;
            droppedCount += drop[(size_t) s] ? 1 : 0;
            if (h < best) { best = h; keep = s; }
        }
        if (linkedCount > 0 && droppedCount == linkedCount)
            drop[(size_t) keep] = false;
    }

    for (int s = 0; s < 3; ++s)
    {
        auto& st = out.stems[(size_t) s];
        st.arrange = in.arrange[(size_t) s];
        st.ratchet = in.ratchet[(size_t) s];
        st.reverse = in.reverse[(size_t) s];
        st.silence = in.silence[(size_t) s];
        st.drop = drop[(size_t) s];

        if (! in.linked[(size_t) s])
            continue;

        const auto lane = (juce::uint32) s * 16u;
        if (engaged)
        {
            const float na = driftNoise (in.seed, lane + 1, barPos, in.rateBars, in.smooth);
            st.arrange = juce::jlimit (0.0f, 1.0f, out.iqNow * energy + depth * 0.45f * na);

            float r = out.iqNow * in.ratchetAmount * energy;
            if (in.driftRatchet)
                r += depth * 0.4f * driftNoise (in.seed, lane + 2, barPos, in.rateBars, in.smooth);
            st.ratchet = juce::jlimit (0.0f, 1.0f, juce::jmax (st.ratchet, r));

            if (in.driftSilence)
                st.silence = juce::jlimit (0.0f, 1.0f, st.silence + depth * 0.5f
                                                           * juce::jmax (0.0f, driftNoise (in.seed, lane + 4, barPos, in.rateBars, in.smooth)));

            if (in.driftSlice)
            {
                // occasionally hop to a finer or coarser grid (stepped on the bar)
                const float ns = driftNoise (in.seed, lane + 5, (double) barIdx, in.rateBars * 2.0, false);
                const float thr = 1.0f - 0.55f * depth;
                st.sliceShift = ns > thr ? 1 : ns < -thr - 0.1f ? -1 : 0;
            }
        }

        if (in.iqReverse)
        {
            float rv = juce::jmax (st.reverse, in.revDepth * (0.4f + 0.6f * energy));
            if (in.driftReverse)
                rv += depth * 0.35f * driftNoise (in.seed, lane + 3, barPos, in.rateBars, in.smooth);
            st.reverse = juce::jlimit (0.0f, 1.0f, rv);
        }

        // IQ Evolve re-rolls the pattern of the current knob step at the start of the bar.
        st.evolve = in.iqEvolve && barIdx % juce::jmax (1, in.evolveEvery) == 0
                    && hash01 (in.seed ^ bar, (juce::uint32) s, 31u) < in.evolveChance;
    }

    return out;
}

// Whole-bar plan (used where only the bar matters, e.g. evolve and the phrase map labels).
inline AutopilotBar planAutopilotBar (const AutopilotInput& in, juce::int64 barIdx) noexcept
{
    return planAutopilot (in, (double) barIdx + 0.5);
}

} // namespace riq
