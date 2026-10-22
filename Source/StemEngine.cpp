#include "StemEngine.h"

namespace riq
{

StemEngine::StemEngine()
{
    for (auto& p : peaks)
        p.store (0.0f);
    for (auto& s : sliceInfo)
        s.store (0);
}

void StemEngine::prepare (double sampleRate)
{
    sr = sampleRate;
    // Room for kCaptureBeats down to 40 BPM.
    capacity = (int) std::ceil (kCaptureBeats * sr * 60.0 / 40.0) + 16;
    for (auto& c : cap)
        c.assign ((size_t) capacity, 0.0f);

    capLen = 0;
    capSpb = 0.0;
    lastWrite = -1;
    curBin = -1;
    binEnergy.fill (0.0f);
    for (auto& p : peaks)
        p.store (0.0f);

    state = CaptureState::live;
    xfadeLen = juce::jmax (16, (int) (sr * 0.003));
    gateCoef = 1.0f - std::exp (-1.0f / (float) (sr * 0.002));
    resetPlayback();
}

void StemEngine::resetPlayback() noexcept
{
    curSlice = -1;
    curInstance = std::numeric_limits<juce::int64>::min();
    havePrev = false;
    xfadeRemain = 0;
    rollLatched = false;
    gateGain = 0.0f;
    dispPlaying.store (-1);
}

void StemEngine::updateCaptureState (const StemSettings& s, double ppq) noexcept
{
    if (s.freeze && state == CaptureState::live)
    {
        double q = 0.0;
        switch (s.freezeQuant)
        {
            case 1:  q = 1.0; break;
            case 2:  q = s.beatsPerBar; break;
            case 3:  q = s.loopBeats; break;
            default: q = 0.0; break;
        }

        freezeAt = q > 0.0 ? std::ceil (ppq / q - 1.0e-9) * q : ppq;
        state = CaptureState::armed;
    }
    else if (! s.freeze && state != CaptureState::live)
    {
        state = CaptureState::live;
    }
}

void StemEngine::writeCapture (double ppq, float l, float r, double spb) noexcept
{
    if (capSpb <= 0.0 || std::abs (spb - capSpb) > capSpb * 0.002)
    {
        // Tempo changed (or first write): the old material no longer lines up with the grid.
        capSpb = spb;
        capLen = juce::jmin (capacity, (int) std::ceil (kCaptureBeats * spb));
        for (auto& c : cap)
            std::fill (c.begin(), c.end(), 0.0f);
        binEnergy.fill (0.0f);
        for (auto& p : peaks)
            p.store (0.0f, std::memory_order_relaxed);
        lastWrite = -1;
        curBin = -1;
    }

    const auto beat = wrapBeat (ppq, (double) kCaptureBeats);
    // Round to the nearest sample: positions are integers plus float noise, so reader and
    // writer must resolve them the same way (see readInterp).
    const int w = (int) (std::llround (beat * capSpb) % capLen);

    if (lastWrite >= 0)
    {
        int gap = w - lastWrite;
        if (gap < 0)
            gap += capLen;
        for (int k = 1; k < gap && gap < 8; ++k)
        {
            const int f = (lastWrite + k) % capLen;
            cap[0][(size_t) f] = l;
            cap[1][(size_t) f] = r;
        }
    }

    cap[0][(size_t) w] = l;
    cap[1][(size_t) w] = r;
    lastWrite = w;

    const int bin = juce::jlimit (0, kBins - 1, (int) (beat * kBinsPerBeat));
    if (bin != curBin)
    {
        if (curBin >= 0)
        {
            peaks[(size_t) curBin].store (binPeak, std::memory_order_relaxed);
            binEnergy[(size_t) curBin] = (float) (binSq / juce::jmax (1, binCount));
        }
        curBin = bin;
        binPeak = 0.0f;
        binSq = 0.0;
        binCount = 0;
    }

    binPeak = juce::jmax (binPeak, std::abs (l), std::abs (r));
    binSq += (double) l * l + (double) r * r;
    ++binCount;
}

void StemEngine::readInterp (double idx, float& l, float& r) const noexcept
{
    if (capLen <= 1)
    {
        l = r = 0.0f;
        return;
    }

    idx = wrapBeat (idx, (double) capLen);

    // On-grid reads (same tempo) are bit-exact copies of what was written; off-grid
    // (tempo changed while frozen) uses 4-point Hermite interpolation.
    const double nearest = std::round (idx);
    if (std::abs (idx - nearest) < 1.0e-3)
    {
        const auto i = (size_t) ((juce::int64) nearest % capLen);
        l = cap[0][i];
        r = cap[1][i];
        return;
    }

    const int i1 = (int) idx;
    const auto f = (float) (idx - i1);

    const int i0 = (i1 - 1 + capLen) % capLen, i2 = (i1 + 1) % capLen, i3 = (i1 + 2) % capLen;
    auto hermite = [f] (float y0, float y1, float y2, float y3)
    {
        const float c1 = 0.5f * (y2 - y0);
        const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
        const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
        return ((c3 * f + c2) * f + c1) * f + y1;
    };
    l = hermite (cap[0][(size_t) i0], cap[0][(size_t) i1], cap[0][(size_t) i2], cap[0][(size_t) i3]);
    r = hermite (cap[1][(size_t) i0], cap[1][(size_t) i1], cap[1][(size_t) i2], cap[1][(size_t) i3]);
}

SliceDecision StemEngine::decide (int j, int n, const StemSettings& s) const noexcept
{
    SliceDecision d;
    d.src = j;

    const double S = s.sliceBeats;
    const double bpb = s.beatsPerBar;
    const double pos = j * S;
    const double inBar = wrapBeat (pos, bpb);
    const bool onBeat = std::abs (inBar - std::round (inBar)) < 1.0e-6;

    bool strong = false;
    switch (s.protect)
    {
        case Protect::off:         strong = false; break;
        case Protect::downbeat:    strong = inBar < 1.0e-6; break;
        case Protect::oneAndThree: strong = inBar < 1.0e-6 || (std::fmod (bpb, 2.0) < 1.0e-6 && std::abs (inBar - bpb * 0.5) < 1.0e-6); break;
        case Protect::allBeats:    strong = onBeat; break;
    }

    if (! strong)
    {
        // Weak positions get more action the further they are from the beat,
        // and the end of the bar gets more than the start (fills).
        const double fr = wrapBeat (pos, 1.0);
        const float weakness = fr < 1.0e-6 ? 0.6f : (std::abs (fr - 0.5) < 1.0e-6 ? 0.85f : 1.0f);
        const float endBoost = 0.75f + 0.5f * (float) (inBar / bpb);
        const float w = weakness * endBoost;
        const auto seed = s.seed;

        if (hash01 (seed, (juce::uint32) j, 1) < s.arrange * w)
        {
            const float t = hash01 (seed, (juce::uint32) j, 2);
            if (t < 0.4f)
            {
                d.src = (j - 1 + n) % n; // loop the previous slice
            }
            else if (t < 0.6f)
            {
                d.src = (j - 2 + n) % n;
            }
            else
            {
                // Jump to a slice sitting at the same position inside another beat.
                const int perBeat = juce::jmax (1, (int) std::round (1.0 / S));
                const int beats = juce::jmax (1, n / perBeat);
                if (beats > 1)
                    d.src = (j + (1 + (int) (hash01 (seed, (juce::uint32) j, 5) * (float) (beats - 1))) * perBeat) % n;
                else
                    d.src = (j + 1 + (int) (hash01 (seed, (juce::uint32) j, 5) * (float) (n - 1))) % n;
            }
        }

        // Ratchet zone: weak beats (weighted as above), only the offbeat half of each beat,
        // or only the second half of the bar (beats 3 & 4 in 4/4).
        float wr = w;
        if (s.ratchetZone == 1)
            wr = fr >= 0.5 - 1.0e-9 ? 1.0f : 0.0f;
        else if (s.ratchetZone == 2)
            wr = inBar >= bpb * 0.5 - 1.0e-9 ? 1.0f : 0.0f;

        if (hash01 (seed, (juce::uint32) j, 3) < s.ratchet * wr)
        {
            const float u = hash01 (seed, (juce::uint32) j, 4);
            int r = u < 0.65f ? 2 : 4; // straight subdivisions only, no triplets
            const int maxR = (int) std::floor (S / 0.0625 + 1.0e-6); // no retrigger shorter than 1/64 note
            if (r > maxR)
                r = maxR >= 2 ? 2 : 1;
            d.ratchet = r;
        }

        if (hash01 (s.seedRev, (juce::uint32) j, 6) < s.reverse * w)
            d.rev = true;
    }

    d.silent = s.silence > 0.0f && rank[(size_t) d.src] < s.silence * 0.9f;
    d.altered = d.src != j || d.rev || d.ratchet > 1;
    return d;
}

void StemEngine::buildTable (const StemSettings& s, double ref) noexcept
{
    const double L = s.loopBeats, S = s.sliceBeats;
    numSlices = juce::jlimit (1, kMaxSlices, (int) std::round (L / S));
    const int n = numSlices;

    // Slice energies of the material each slice would currently play (for Silence).
    std::array<float, kMaxSlices> energy {};
    const int binsPerSlice = juce::jmax (1, (int) std::round (S * kBinsPerBeat));
    for (int k = 0; k < n; ++k)
    {
        const double start = srcBeatFor (k * S, ref, L);
        const int b0 = (int) std::floor (wrapBeat (start, (double) kCaptureBeats) * kBinsPerBeat);
        float e = 0.0f;
        for (int b = 0; b < binsPerSlice; ++b)
            e += binEnergy[(size_t) ((b0 + b) % kBins)];
        energy[(size_t) k] = e;
    }

    for (int k = 0; k < n; ++k)
    {
        int below = 0;
        for (int m = 0; m < n; ++m)
            if (energy[(size_t) m] < energy[(size_t) k] || (energy[(size_t) m] == energy[(size_t) k] && m < k))
                ++below;
        rank[(size_t) k] = n > 1 ? (float) below / (float) (n - 1) : 1.0f;
    }

    for (int j = 0; j < n; ++j)
    {
        table[(size_t) j] = decide (j, n, s);
        sliceInfo[(size_t) j].store (table[(size_t) j].pack(), std::memory_order_relaxed);
    }
}

void StemEngine::process (const float* inL, const float* inR, float* outL, float* outR, int numSamples,
                          double ppqStart, double ppqPerSample, double samplesPerBeat,
                          const StemSettings& s, const FillSettings& fill) noexcept
{
    updateCaptureState (s, ppqStart);

    if (s.roll && ! rollHeld)
    {
        rollDec = cur;
        rollDec.ratchet = 1;
        rollDec.altered = true;
        rollLatched = true;
    }
    else if (! s.roll)
    {
        rollLatched = false;
    }
    rollHeld = s.roll;

    const double L = s.loopBeats, S = s.sliceBeats;
    buildTable (s, state == CaptureState::frozen ? frozenRef : ppqStart);

    const float targetL = s.gainL, targetR = s.gainR;
    const float dGL = (targetL - gL) / (float) juce::jmax (1, numSamples);
    const float dGR = (targetR - gR) / (float) juce::jmax (1, numSamples);
    float peak = 0.0f;

    for (int i = 0; i < numSamples; ++i)
    {
        const double ppq = ppqStart + i * ppqPerSample;

        if (state == CaptureState::armed && ppq >= freezeAt)
        {
            state = CaptureState::frozen;
            frozenRef = freezeAt;
        }

        const bool capturing = state != CaptureState::frozen;
        if (capturing)
            writeCapture (ppq, inL[i], inR[i], samplesPerBeat);

        const double ref = capturing ? ppq : frozenRef;
        const double phase = wrapBeat (ppq, L);
        const int j = juce::jlimit (0, numSlices - 1, (int) (phase / S));
        const auto inst = (juce::int64) std::floor (ppq / L);

        if (j != curSlice || inst != curInstance)
        {
            curSlice = j;
            curInstance = inst;
            cur = rollLatched ? rollDec : table[(size_t) j];
        }

        double srcPhase = 0.0;
        bool silent = cur.silent;
        bool reversed = cur.rev;

        const bool fillOn = fill.active && fill.numKeys > 0 && std::floor (ppq / fill.sliceBeats) >= fill.startSlot;
        if (fillOn)
        {
            const auto slot = (juce::int64) (std::floor (ppq / fill.sliceBeats) - fill.startSlot);
            const int key = fill.keys[(size_t) (slot % fill.numKeys)];
            srcPhase = wrapBeat (key * fill.sliceBeats, L) + wrapBeat (ppq, fill.sliceBeats);
            silent = false;
            reversed = false;
        }
        else
        {
            const double seg = S / cur.ratchet;
            double o = std::fmod (phase - j * S, seg);
            if (cur.rev)
                o = juce::jmax (0.0, seg - o - ppqPerSample);
            srcPhase = cur.src * S + o;
        }

        const double srcBeat = srcBeatFor (srcPhase, ref, L);
        const bool live = capturing && std::abs (srcBeat - ppq) < ppqPerSample * 0.5;
        const double idx = capSpb > 0.0 ? wrapBeat (srcBeat, (double) kCaptureBeats) * capSpb : 0.0;
        const double step = (reversed ? -1.0 : 1.0) * capSpb * ppqPerSample;

        float sl = 0.0f, sr_ = 0.0f;
        if (live)
        {
            sl = inL[i];
            sr_ = inR[i];
        }
        else
        {
            readInterp (idx, sl, sr_);
        }

        // Crossfade over any jump in the read position to avoid clicks.
        if (havePrev && capLen > 0)
        {
            double diff = idx - (prevIdx + prevStep);
            const double half = capLen * 0.5;
            if (diff > half) diff -= capLen;
            if (diff < -half) diff += capLen;

            if (std::abs (diff) > 2.0)
            {
                oldIdx = prevIdx + prevStep;
                oldStep = prevStep;
                oldLive = prevLive;
                xfadeRemain = xfadeLen;
            }
        }

        if (xfadeRemain > 0)
        {
            const float g = (float) xfadeRemain / (float) xfadeLen;
            float ol, orr;
            if (oldLive)
            {
                ol = inL[i];
                orr = inR[i];
            }
            else
            {
                readInterp (oldIdx, ol, orr);
            }
            sl = sl * (1.0f - g) + ol * g;
            sr_ = sr_ * (1.0f - g) + orr * g;
            oldIdx += oldStep;
            --xfadeRemain;
        }

        prevIdx = idx;
        prevStep = step;
        prevLive = live;
        havePrev = true;

        const float target = (silent || ! s.audible) ? 0.0f : 1.0f;
        gateGain += (target - gateGain) * gateCoef;
        if (std::abs (target - gateGain) < 1.0e-4f)
            gateGain = target; // land exactly on unity so untouched audio stays bit-exact

        gL += dGL;
        gR += dGR;
        const float yl = sl * gateGain * gL;
        const float yr = sr_ * gateGain * gR;
        outL[i] += yl;
        outR[i] += yr;
        peak = juce::jmax (peak, std::abs (yl), std::abs (yr));
    }

    gL = targetL;
    gR = targetR;

    const double endPpq = ppqStart + numSamples * ppqPerSample;
    dispRef.store (state == CaptureState::frozen ? frozenRef : endPpq, std::memory_order_relaxed);
    dispLoop.store (L, std::memory_order_relaxed);
    dispSlice.store (S, std::memory_order_relaxed);
    dispBeatsPerBar.store (s.beatsPerBar, std::memory_order_relaxed);
    dispPhase.store (wrapBeat (endPpq, L), std::memory_order_relaxed);
    dispSlices.store (numSlices, std::memory_order_relaxed);
    dispPlaying.store (curSlice, std::memory_order_relaxed);
    dispState.store ((int) state, std::memory_order_relaxed);
    dispLevel.store (juce::jmax (peak, dispLevel.load (std::memory_order_relaxed) * 0.85f), std::memory_order_relaxed);
}

} // namespace riq
