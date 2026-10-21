#pragma once

#include "StemEngine.h"

namespace riq
{

// Beat Assistant / autopilot as a pure function of the bar number: the audio thread uses
// it to drive the stems, the editor uses the very same function to draw the phrase map,
// so what you see is exactly what will play.
struct AutopilotInput
{
    float iq = 0.0f;
    bool iqEvolve = false, iqReverse = false;
    std::array<bool, 3> linked { true, true, true };
    std::array<float, 3> arrange {}, ratchet {}, reverse {};
    float fill = 0.7f, motion = 0.5f, fillRatchet = 0.5f, revDepth = 0.3f, drop = 0.0f, evolveChance = 0.5f;
    int phraseBars = 8, evolveEvery = 4;
};

struct AutopilotStem
{
    float arrange = 0.0f, ratchet = 0.0f, reverse = 0.0f;
    bool drop = false, evolve = false;
};

struct AutopilotBar
{
    std::array<AutopilotStem, 3> stems;
    int phrasePos = 0;
    bool fillBar = false, halfFillBar = false;
};

inline AutopilotBar planAutopilotBar (const AutopilotInput& in, juce::int64 barIdx) noexcept
{
    AutopilotBar out;
    const int pb = juce::jmax (1, in.phraseBars);
    out.phrasePos = (int) (((barIdx % pb) + pb) % pb);
    out.fillBar = out.phrasePos == pb - 1;
    out.halfFillBar = pb >= 8 && out.phrasePos == pb / 2 - 1;
    const auto bar = (juce::uint32) barIdx;

    // Drops: a linked stem sits out a bar now and then, never on the phrase start,
    // and at least one linked stem always keeps playing.
    std::array<bool, 3> drop {};
    if (in.iq > 0.0f && in.drop > 0.0f && out.phrasePos != 0)
    {
        int keep = -1, linkedCount = 0, droppedCount = 0;
        float best = 2.0f;
        for (int s = 0; s < 3; ++s)
        {
            if (! in.linked[(size_t) s])
                continue;
            ++linkedCount;
            const float h = hash01 (bar, (juce::uint32) s, 41u);
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
        st.drop = drop[(size_t) s];

        if (! in.linked[(size_t) s])
            continue;

        if (in.iq > 0.0f)
        {
            // IQ Arrange takes over the stem Arrange knob: gentle motion from bar to bar,
            // building into a fill at the end of the phrase (and a smaller one half way).
            const float wobble = hash01 (bar, (juce::uint32) s, 11u);
            float a = in.iq * (0.55f + in.motion * (wobble - 0.5f) * 0.9f);
            float r = in.iq * in.fillRatchet * 0.15f * wobble;
            if (out.fillBar)
            {
                a = juce::jmax (a, in.iq * (0.5f + 0.5f * in.fill) + in.fill * 0.25f);
                r = juce::jmax (r, in.fillRatchet * in.fill * (0.4f + 0.6f * in.iq));
            }
            else if (out.halfFillBar)
            {
                a = juce::jmax (a, in.iq * (0.45f + 0.35f * in.fill));
                r = juce::jmax (r, in.fillRatchet * in.fill * 0.5f * in.iq);
            }
            st.arrange = juce::jlimit (0.0f, 1.0f, a);
            st.ratchet = juce::jlimit (0.0f, 1.0f, juce::jmax (st.ratchet, r));
        }

        if (in.iqReverse)
        {
            const float base = (barIdx & 1) ? in.revDepth : in.revDepth * 0.35f;
            st.reverse = juce::jmin (1.0f, juce::jmax (st.reverse * (1.0f + in.revDepth), base));
        }

        // IQ Evolve presses this stem's Evolve buttons at the start of the bar.
        st.evolve = in.iqEvolve && barIdx % juce::jmax (1, in.evolveEvery) == 0
                    && hash01 (bar, (juce::uint32) s, 31u) < in.evolveChance;
    }

    return out;
}

} // namespace riq
