// Offline sanity test: one SEND instance + one ENGINE instance driven by a fake host transport.
#include "../Source/PluginProcessor.h"
#include <iostream>
#include <set>

namespace
{
constexpr double kSr = 48000.0, kBpm = 120.0;
constexpr int kBlock = 512;
const double kSpb = kSr * 60.0 / kBpm; // 24000 samples per beat

struct FakeHead : juce::AudioPlayHead
{
    juce::int64 t = 0;
    int wobble = 0; // host position jitter in samples
    juce::int64 loopLen = 0; // host loop length in samples (0 = no loop)
    juce::int64 pos() const { return (loopLen > 0 ? t % loopLen : t) + wobble; }
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo p;
        p.setIsPlaying (true);
        p.setBpm (kBpm);
        p.setTimeInSamples (pos());
        p.setPpqPosition ((double) pos() / kSpb);
        p.setTimeSignature (juce::AudioPlayHead::TimeSignature { 4, 4 });
        return p;
    }
};

void setParam (RiqLiveProcessor& p, const juce::String& id, float value)
{
    auto* prm = p.apvts.getParameter (id);
    prm->setValueNotifyingHost (prm->convertTo0to1 (value));
}

// Source signal: a click at every 1/16 note, amplitude encodes its 16th index in the bar.
float source (juce::int64 t)
{
    const auto sixteenth = (juce::int64) (kSpb / 4);
    const auto k = t / sixteenth;
    const auto off = t % sixteenth;
    const float amp = 0.1f + 0.05f * (float) (k % 16) / 2.0f;
    return off < 2000 ? amp * std::sin ((float) off * 0.05f) * (1.0f - (float) off / 2000.0f) : 0.0f;
}

// Second track: a steady low tone.
float source2 (juce::int64 t)
{
    return 0.2f * std::sin ((float) (t % 4800) * (juce::MathConstants<float>::twoPi / 480.0f));
}
} // namespace

int main()
{
    juce::ScopedJuceInitialiser_GUI init;

    RiqLiveProcessor send (true), engine (false);
    FakeHead head;
    send.setPlayHead (&head);
    engine.setPlayHead (&head);
    setParam (send, "slot", 0.0f); // Send A
    setParam (engine, "s1_src", 0.0f);
    setParam (engine, "s2_on", 0.0f);
    setParam (engine, "s3_on", 0.0f);
    setParam (engine, "limiter", 0.0f);

    send.prepareToPlay (kSr, kBlock);
    engine.prepareToPlay (kSr, kBlock);
    const int D = engine.getLatencySamples();
    std::cout << "engine latency " << D << " samples\n";

    juce::AudioBuffer<float> sb (2, kBlock), eb (8, kBlock); // Mix + 3 stem outs
    juce::MidiBuffer midi;
    std::vector<float> outLog, stemLog;
    bool silenceInput = false;
    std::vector<RiqLiveProcessor*> extras; // more Send instances, fed source2
    juce::AudioBuffer<float> xb (2, kBlock);
    juce::Random rng (42);
    bool jitter = false;

    auto run = [&] (double beats)
    {
        const auto blocks = (int) (beats * kSpb / kBlock);
        for (int b = 0; b < blocks; ++b)
        {
            for (int i = 0; i < kBlock; ++i)
            {
                const float v = silenceInput ? 0.0f : source (head.t + i);
                sb.setSample (0, i, v);
                sb.setSample (1, i, v);
            }
            head.wobble = jitter ? rng.nextInt (7) - 3 : 0;
            send.processBlock (sb, midi);
            for (auto* x : extras)
            {
                for (int i = 0; i < kBlock; ++i)
                {
                    const float v = source2 (head.t + i);
                    xb.setSample (0, i, v);
                    xb.setSample (1, i, v);
                }
                x->processBlock (xb, midi);
            }
            eb.clear();
            engine.processBlock (eb, midi);
            for (int i = 0; i < kBlock; ++i)
            {
                outLog.push_back (eb.getSample (0, i));
                stemLog.push_back (eb.getSample (2, i));
            }
            head.t += kBlock;
        }
    };

    // Compare output (delayed by D) against the source over a window of beats.
    auto compare = [&] (double fromBeat, double toBeat, double sliceBeats, std::vector<double>* perSliceErr = nullptr)
    {
        double maxErr = 0.0;
        const auto a = (juce::int64) (fromBeat * kSpb), z = (juce::int64) (toBeat * kSpb);
        for (auto s = a; s < z && s < (juce::int64) outLog.size(); ++s)
        {
            // skip the 3 ms declick crossfade at each slice start
            const auto inSlice = (s - D) % (juce::int64) (sliceBeats * kSpb);
            if (inSlice < 160)
                continue;
            const double e = std::abs (outLog[(size_t) s] - source (s - D));
            maxErr = juce::jmax (maxErr, e);
            if (perSliceErr != nullptr)
            {
                const auto slice = (size_t) (((double) (s - D) / kSpb) / sliceBeats) % perSliceErr->size();
                (*perSliceErr)[slice] = juce::jmax ((*perSliceErr)[slice], e);
            }
        }
        return maxErr;
    };

    int failures = 0;
    auto check = [&] (bool ok, const char* what)
    {
        std::cout << (ok ? "PASS  " : "FAIL  ") << what << "\n";
        failures += ok ? 0 : 1;
    };

    // 1) All knobs at zero: engine must reproduce the live input exactly (just delayed).
    run (8.0);
    const double errLive = compare (2.0, 8.0, 0.25);
    std::cout << "  live max error " << errLive << "\n";
    check (errLive == 0.0, "passthrough with knobs at zero is bit-exact");

    // 1b) Several Sends on one slot are summed, like tracks routed into one audio track.
    auto addSend = [&] (int slot)
    {
        auto* x = new RiqLiveProcessor (true);
        x->setPlayHead (&head);
        setParam (*x, "slot", (float) slot);
        x->prepareToPlay (kSr, kBlock);
        extras.push_back (x);
        return x;
    };
    auto* second = addSend (0); // also slot A
    run (0.5);
    check (RiqLiveProcessor::getSlotSendCount (0) == 2, "two Sends registered on slot A");
    const auto sumFrom = outLog.size();
    run (4.0);
    double sumErr = 0.0;
    for (auto s = sumFrom + (size_t) D + 200; s < outLog.size(); ++s)
    {
        const auto t = (juce::int64) s - D;
        sumErr = juce::jmax (sumErr, (double) std::abs (outLog[s] - (source (t) + source2 (t))));
    }
    std::cout << "  sum max error " << sumErr << "\n";
    check (sumErr == 0.0, "stem plays the exact sum of both tracks");

    setParam (*second, "slot", 1.0f); // move it to slot B
    run (0.5);
    const auto moveFrom = outLog.size();
    run (4.0);
    double moveErr = 0.0;
    for (auto s = moveFrom + (size_t) D + 200; s < outLog.size(); ++s)
        moveErr = juce::jmax (moveErr, (double) std::abs (outLog[s] - source ((juce::int64) s - D)));
    check (moveErr == 0.0 && RiqLiveProcessor::getSlotSendCount (1) == 1, "moving a Send to slot B leaves stem 1 untouched");
    extras.clear();
    delete second;
    run (0.5);

    // 1c) Host position wobbling by +-3 samples must not cause dropouts.
    jitter = true;
    const auto jitFrom = outLog.size();
    run (8.0);
    jitter = false;
    int dropouts = 0;
    for (auto b = jitFrom + (size_t) kBlock * 4; b + kBlock <= outLog.size(); b += kBlock)
    {
        double eo = 0.0, es = 0.0;
        for (size_t i = b; i < b + kBlock; ++i)
        {
            eo += (double) outLog[i] * outLog[i];
            const double v = source ((juce::int64) i - D);
            es += v * v;
        }
        if (es > 1.0e-4 && eo < 0.25 * es)
            ++dropouts;
    }
    std::cout << "  jitter dropouts " << dropouts << "\n";
    check (dropouts == 0, "position jitter causes no dropouts");
    run (1.0); // re-settle after jitter

    // 2) Ratchet + arrange: beats 1 and 3 stay untouched, weak 16ths get retriggered.
    setParam (engine, "s1_ratchet", 1.0f);
    setParam (engine, "s1_arrange", 0.6f);
    const auto mark = (double) outLog.size() / kSpb;
    run (8.0);
    std::vector<double> err (16, 0.0);
    compare (mark + 1.0, mark + 8.0, 0.25, &err);
    int changedWeak = 0;
    bool strongClean = true;
    for (int k = 0; k < 16; ++k)
    {
        const bool strong = (k == 0 || k == 8);
        // allow the 3 ms crossfade from the neighbouring slice to leak in
        if (strong && err[(size_t) k] > 1.0e-4) strongClean = false;
        if (! strong && err[(size_t) k] > 0.01) ++changedWeak;
        std::cout << "  16th " << k << (strong ? " [strong] " : "          ") << "err " << err[(size_t) k] << "\n";
    }
    check (strongClean, "slices on beats 1 and 3 are protected");
    check (changedWeak >= 4, "weak-beat slices get retriggered");

    // Ratchet must only ever subdivide by 2 or 4 (no triplets), across many variations.
    bool noTriplets = true;
    int ratchets = 0, ratchetsLastBeat = 0, ratchetsLastTwo = 0, ratchetsGroove = 0;
    for (int evo = 0; evo < 100; ++evo)
    {
        setParam (engine, "s1_evolveArr", 1.0f);
        run (0.25);
        setParam (engine, "s1_evolveArr", 0.0f);
        run (0.25);
        auto& st = engine.getStem (0);
        const double sl = st.dispSlice.load();
        for (int j = 0; j < st.dispSlices.load(); ++j)
        {
            const auto d = riq::SliceDecision::unpack (st.sliceInfo[(size_t) j].load());
            ratchets += d.ratchet > 1 ? 1 : 0;
            noTriplets = noTriplets && (d.ratchet == 1 || d.ratchet == 2 || d.ratchet == 4);
            if (d.ratchet > 1)
            {
                const int beatInBar = (int) std::floor (std::fmod (j * sl, 4.0) + 1.0e-9);
                ratchetsLastBeat += beatInBar == 3 ? 1 : 0;
                ratchetsLastTwo += beatInBar >= 2 ? 1 : 0;
                ratchetsGroove += std::fmod (j * sl, 4.0) < 1.5 - 1.0e-9 ? 1 : 0; // beat 1 + first half of beat 2
            }
        }
    }
    std::cout << "  ratcheted slices seen " << ratchets << ", on beat 4: " << ratchetsLastBeat
              << ", on beats 3-4: " << ratchetsLastTwo << ", in the groove zone: " << ratchetsGroove << "\n";
    check (noTriplets && ratchets > 100, "ratchet uses only x2 / x4, never triplets");
    check (ratchetsLastBeat >= ratchets / 2 && ratchetsGroove <= ratchets / 10,
           "hitech style puts the rolls at the end of the bar");

    // 3) Freeze (quantised to loop) then silence the input: frozen loop must keep playing.
    setParam (engine, "s1_ratchet", 0.0f);
    setParam (engine, "s1_arrange", 0.0f);
    setParam (engine, "s1_freeze", 1.0f);
    run (8.0);
    silenceInput = true;
    const auto silentFrom = outLog.size();
    run (8.0);
    double rms = 0.0;
    double errFrozen = 0.0, worstPos = 0.0;
    for (auto s = silentFrom + (size_t) D + 2000; s < outLog.size(); ++s)
    {
        rms += (double) outLog[s] * outLog[s];
        // frozen bar should match the source at the same bar phase
        const double e = std::abs (outLog[s] - source ((juce::int64) s - D));
        if (((juce::int64) s - D) % (juce::int64) (4 * kSpb) < 160) // loop-seam declick
            continue;
        if (e > 1.0e-6 && std::getenv ("RIQ_DEBUG") != nullptr)
            std::cout << "    frozen err " << e << " sample " << s << " barbeat "
                      << std::fmod ((double) ((juce::int64) s - D) / kSpb, 4.0) << "\n";
        if (e > errFrozen)
        {
            errFrozen = e;
            worstPos = std::fmod ((double) ((juce::int64) s - D) / kSpb, 4.0);
        }
    }
    rms = std::sqrt (rms / (double) (outLog.size() - silentFrom));
    std::cout << "  frozen rms " << rms << "  max error vs source phase " << errFrozen << " at bar beat " << worstPos << "\n";
    check (rms > 0.01, "frozen loop keeps playing after the input goes silent");
    check (errFrozen < 1.0e-3, "frozen loop stays aligned to the bar grid");

    // 4) Back to IN with silent input: output must follow the (silent) live input.
    setParam (engine, "s1_freeze", 0.0f);
    const auto inFrom = outLog.size();
    run (4.0);
    double peakIn = 0.0;
    for (auto s = inFrom + (size_t) D + 2000; s < outLog.size(); ++s)
        peakIn = juce::jmax (peakIn, (double) std::abs (outLog[s]));
    check (peakIn < 1.0e-4, "IN mode follows live input again");

    // 5) Separate outputs: with Out = Own the stem leaves the Mix and plays on its own bus.
    silenceInput = false;
    setParam (engine, "s1_out", 1.0f);
    const auto ownFrom = outLog.size();
    run (4.0);
    double mixPeak = 0.0, ownErr = 0.0;
    for (auto s = ownFrom + (size_t) D + 2000; s < outLog.size(); ++s)
    {
        mixPeak = juce::jmax (mixPeak, (double) std::abs (outLog[s]));
        ownErr = juce::jmax (ownErr, (double) std::abs (stemLog[s] - source ((juce::int64) s - D)));
    }
    check (mixPeak < 1.0e-6, "Out=Own removes the stem from the Mix bus");
    check (ownErr < 1.0e-4, "Out=Own plays the stem sample-exact on its own bus");

    // 5b) True-peak limiter: driven hard it must hold the ceiling, quiet audio must pass bit-exact.
    {
        auto truePeak = [] (const std::vector<float>& v, size_t from, size_t to)
        {
            // 8x oversampled peak with a long windowed sinc (reference, independent of the limiter's own estimator)
            double pk = 0.0;
            constexpr int H = 32, OS = 8;
            for (size_t i = from + H; i + H < to; ++i)
            {
                pk = juce::jmax (pk, (double) std::abs (v[i]));
                for (int p = 1; p < OS; ++p)
                {
                    const double f = (double) p / OS;
                    double y = 0.0;
                    for (int k = -H + 1; k <= H; ++k)
                    {
                        const double x = k - f;
                        const double s = std::sin (juce::MathConstants<double>::pi * x) / (juce::MathConstants<double>::pi * x);
                        const double w = 0.5 * (1.0 + std::cos (juce::MathConstants<double>::pi * x / (H + 0.5)));
                        y += v[i + (size_t) k] * s * w;
                    }
                    pk = juce::jmax (pk, std::abs (y));
                }
            }
            return 20.0 * std::log10 (pk + 1.0e-12);
        };

        setParam (engine, "s1_out", 0.0f); // back on the Mix
        setParam (engine, "limiter", 1.0f);
        setParam (engine, "limCeiling", 0.0f);
        setParam (engine, "limDrive", 18.0f);
        run (1.0);
        const auto limFrom = outLog.size();
        run (8.0);
        double samplePeak = 0.0;
        for (auto s = limFrom; s < outLog.size(); ++s)
            samplePeak = juce::jmax (samplePeak, (double) std::abs (outLog[s]));
        const double tpDb = truePeak (outLog, limFrom, outLog.size());
        std::cout << "  limiter +18 dB drive: sample peak " << 20.0 * std::log10 (samplePeak) << " dB, true peak " << tpDb << " dBTP\n";
        check (samplePeak <= 1.0 && tpDb <= 0.15, "limiter holds 0 dBTP with +18 dB drive");

        setParam (engine, "limDrive", 0.0f);
        run (2.0);
        const auto quietFrom = outLog.size();
        run (4.0);
        double quietErr = 0.0;
        for (auto s = quietFrom; s < outLog.size(); ++s)
            quietErr = juce::jmax (quietErr, (double) std::abs (outLog[s] - source ((juce::int64) s - D)));
        std::cout << "  limiter quiet max error " << quietErr << "\n";
        check (quietErr == 0.0, "limiter passes audio under the ceiling bit-exact");
        setParam (engine, "limiter", 0.0f);
    }

    // 5c) Presets: save, change, load, rename, delete.
    {
        auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("autopilot_preset_test");
        dir.deleteRecursively();
        _putenv_s ("AUTOPILOT_PRESET_DIR", dir.getFullPathName().toRawUTF8());
        setParam (engine, "s2_gain", -6.0f);
        const bool saved = engine.savePreset ("Test Preset");
        setParam (engine, "s2_gain", 0.0f);
        const bool loaded = engine.loadPreset ("Test Preset");
        const float g = engine.apvts.getRawParameterValue ("s2_gain")->load();
        const bool renamed = engine.renamePreset ("Test Preset", "Renamed");
        const auto names = RiqLiveProcessor::getPresetNames();
        const bool deleted = engine.deletePreset ("Renamed");
        check (saved && loaded && std::abs (g + 6.0f) < 1.0e-3 && renamed && names.contains ("Renamed")
                   && deleted && RiqLiveProcessor::getPresetNames().isEmpty() && engine.getCurrentPresetName().isEmpty(),
               "presets save / load / rename / delete");
        dir.deleteRecursively();
        setParam (engine, "s2_gain", 0.0f);
    }

    // 5d) Host loops 2 bars: the autopilot phrase must still run through all 8 bars,
    //     and IQ Evolve (every 4 bars, chance 100%) must fire every 4 played bars.
    {
        setParam (engine, "iqArrange", 0.5f);
        setParam (engine, "iqEvolve", 1.0f);
        setParam (engine, "apPhrase", 1.0f);       // 8 bars
        setParam (engine, "apEvolveEvery", 2.0f);  // every 4 bars
        setParam (engine, "apEvolveChance", 1.0f);
        const auto evolveBefore = engine.evolveCount[0].load();
        head.loopLen = (juce::int64) (8 * kSpb);   // 2 bars of 4/4
        int maxPos = 0;
        std::set<int> seen;
        for (int b = 0; b < (int) (64 * kSpb / kBlock); ++b) // 16 played bars
        {
            run (kBlock / kSpb);
            maxPos = juce::jmax (maxPos, engine.phrasePos.load());
            seen.insert (engine.phrasePos.load());
        }
        head.loopLen = 0;
        const auto evolves = engine.evolveCount[0].load() - evolveBefore;
        std::cout << "  host loop 2 bars: phrase positions seen " << seen.size() << ", max " << maxPos + 1
                  << ", evolves in 16 bars " << evolves << "\n";
        check (seen.size() == 8 && maxPos == 7, "autopilot phrase runs all 8 bars inside a 2-bar host loop");
        check (evolves >= 3 && evolves <= 5, "IQ Evolve every 4 bars keeps firing inside a host loop");
        setParam (engine, "iqArrange", 0.0f);
        setParam (engine, "iqEvolve", 0.0f);
        run (1.0);
    }

    // 6) Autopilot running for 32 bars: output keeps flowing and stays finite.
    setParam (engine, "s1_out", 0.0f);
    setParam (engine, "iqArrange", 0.8f);
    setParam (engine, "iqEvolve", 1.0f);
    setParam (engine, "iqReverse", 1.0f);
    setParam (engine, "apDrop", 0.5f);
    const auto apFrom = outLog.size();
    run (128.0);
    double apRms = 0.0;
    bool finite = true;
    for (auto s = apFrom; s < outLog.size(); ++s)
    {
        finite = finite && std::isfinite (outLog[s]);
        apRms += (double) outLog[s] * outLog[s];
    }
    apRms = std::sqrt (apRms / (double) (outLog.size() - apFrom));
    std::cout << "  autopilot rms " << apRms << "\n";
    check (finite && apRms > 0.01, "autopilot runs 32 bars with signal");

    // Render both editors to PNG for a visual check.
    if (auto dir = juce::SystemStats::getEnvironmentVariable ("RIQ_SNAPSHOT_DIR", {}); dir.isNotEmpty())
    {
        auto snap = [&] (RiqLiveProcessor& p, const juce::String& name)
        {
            std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
            auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
            juce::File f = juce::File (dir).getChildFile (name);
            f.deleteFile();
            juce::FileOutputStream os (f);
            juce::PNGImageFormat().writeImageToStream (img, os);
        };
        snap (engine, "engine.png");
        snap (send, "send.png");
    }

    std::cout << (failures == 0 ? "ALL PASSED\n" : "SOME TESTS FAILED\n");
    return failures;
}
