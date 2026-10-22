#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace riq;

namespace
{
    constexpr double kSliceBeats[] = { 1.0, 0.5, 0.25, 0.125 };
    constexpr double kLoopBars[] = { 1.0, 2.0, 4.0 };

    float dbToGain (float db) { return juce::Decibels::decibelsToGain (db, -60.0f); }
}

juce::AudioProcessorValueTreeState::ParameterLayout RiqLiveProcessor::createLayout (bool sendMode)
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;

    auto choice = [&] (const String& id, const String& name, StringArray items, int def)
    { layout.add (std::make_unique<AudioParameterChoice> (ParameterID { id, 1 }, name, items, def)); };
    auto toggle = [&] (const String& id, const String& name, bool def)
    { layout.add (std::make_unique<AudioParameterBool> (ParameterID { id, 1 }, name, def)); };
    auto knob = [&] (const String& id, const String& name, float lo, float hi, float def, const String& label = {})
    { layout.add (std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, name, NormalisableRange<float> (lo, hi, 0.0f), def, AudioParameterFloatAttributes().withLabel (label))); };

    const StringArray slices { "1/4", "1/8", "1/16", "1/32" };

    if (sendMode)
    {
        choice (ID::slot, "Slot", { "A", "B", "C" }, 0);
        toggle (ID::sendThru, "Send Thru", false);
        return layout;
    }

    choice (ID::loopLen, "Loop Length", { "1 Bar", "2 Bars", "4 Bars" }, 0);
    choice (ID::protect, "Protect", { "Off", "Downbeat", "1 & 3", "All Beats" }, 2);
    choice (ID::freezeQ, "Freeze Quantize", { "Off", "Beat", "Bar", "Loop" }, 3);
    choice (ID::sceneQ, "Scene Quantize", { "Off", "Beat", "Bar" }, 2);
    choice (ID::fillSize, "Fill Size", slices, 1);
    layout.add (std::make_unique<AudioParameterInt> (ParameterID { ID::fillNote, 1 }, "Fill Base Note", 0, 112, 36));
    knob (ID::masterGain, "Master Gain", -24.0f, 12.0f, 0.0f, "dB");
    toggle (ID::limiter, "Limiter", true);
    knob (ID::iqArrange, "IQ Arrange", 0.0f, 1.0f, 0.0f);
    toggle (ID::iqEvolve, "IQ Evolve", false);
    toggle (ID::iqReverse, "IQ Reverse", false);
    choice (ID::apPhrase, "Autopilot Phrase", { "4 Bars", "8 Bars", "16 Bars" }, 1);
    knob (ID::apFill, "Autopilot Fill", 0.0f, 1.0f, 0.7f);
    knob (ID::apMotion, "Autopilot Motion", 0.0f, 1.0f, 0.5f);
    knob (ID::apRatchet, "Autopilot Fill Ratchet", 0.0f, 1.0f, 0.5f);
    choice (ID::apEvolveEvery, "Autopilot Evolve Every", { "1", "2", "4", "8", "16" }, 2);
    knob (ID::apEvolveChance, "Autopilot Evolve Chance", 0.0f, 1.0f, 0.5f);
    knob (ID::apRevDepth, "Autopilot Reverse Depth", 0.0f, 1.0f, 0.3f);
    knob (ID::apDrop, "Autopilot Drop", 0.0f, 1.0f, 0.0f);
    choice (ID::scene, "Scene", { "-", "Init", "1", "2", "3", "4", "5", "6", "7", "8" }, 0);

    for (int s = 0; s < kNumStems; ++s)
    {
        const auto n = "Stem " + String (s + 1) + " ";
        choice (ID::stem (s, "src"), n + "Source", { "Send A", "Send B", "Send C" }, s);
        choice (ID::stem (s, "out"), n + "Output", { "Mix", "Own" }, 0);
        toggle (ID::stem (s, "freeze"), n + "Buffer Frozen", false);
        choice (ID::stem (s, "slice"), n + "Slice Size", slices, 2);
        choice (ID::stem (s, "rzone"), n + "Ratchet Zone", { "Weak", "Offbeat", "3 & 4" }, 0);
        knob (ID::stem (s, "arrange"), n + "Arrange", 0.0f, 1.0f, 0.0f);
        knob (ID::stem (s, "ratchet"), n + "Ratchet", 0.0f, 1.0f, 0.0f);
        knob (ID::stem (s, "reverse"), n + "Reverse", 0.0f, 1.0f, 0.0f);
        knob (ID::stem (s, "silence"), n + "Silence", 0.0f, 1.0f, 0.0f);
        toggle (ID::stem (s, "evolveArr"), n + "Arrange Evolve", false);
        toggle (ID::stem (s, "evolveRev"), n + "Reverse Evolve", false);
        toggle (ID::stem (s, "roll"), n + "Freeze (hold)", false);
        toggle (ID::stem (s, "on"), n + "On", true);
        toggle (ID::stem (s, "solo"), n + "Solo", false);
        toggle (ID::stem (s, "link"), n + "IQ Link", true);
        knob (ID::stem (s, "pan"), n + "Pan", -1.0f, 1.0f, 0.0f);
        knob (ID::stem (s, "gain"), n + "Gain", -24.0f, 12.0f, 0.0f, "dB");
    }

    return layout;
}

juce::AudioProcessor::BusesProperties RiqLiveProcessor::createBuses (bool sendMode)
{
    if (sendMode)
        return BusesProperties()
            .withInput ("Input", juce::AudioChannelSet::stereo(), true)
            .withOutput ("Output", juce::AudioChannelSet::stereo(), true);

    return BusesProperties()
        .withOutput ("Mix", juce::AudioChannelSet::stereo(), true)
        .withOutput ("Stem 1", juce::AudioChannelSet::stereo(), true)
        .withOutput ("Stem 2", juce::AudioChannelSet::stereo(), true)
        .withOutput ("Stem 3", juce::AudioChannelSet::stereo(), true);
}

RiqLiveProcessor::RiqLiveProcessor (bool isSend)
    : AudioProcessor (createBuses (isSend)),
      sendMode (isSend),
      apvts (*this, nullptr, "RIQ", createLayout (isSend)),
      instanceId ((juce::uint64) juce::Random::getSystemRandom().nextInt64() | 1u)
{
    for (size_t s = 0; s < seeds.size(); ++s)
        seeds[s].store ((juce::uint32) s * 7919u + 1u);

    if (sendMode)
        return;

    for (int s = 0; s < kNumStems; ++s)
        for (int k = 0; k < kSceneParamsPerStem; ++k)
            sceneParams[(size_t) (s * kSceneParamsPerStem + k)] = apvts.getParameter (ID::stem (s, kSceneStemParams[k]));

    auto& init = scenes[0];
    init.valid = true;
    for (size_t k = 0; k < sceneParams.size(); ++k)
        init.values[k] = sceneParams[k]->getDefaultValue();
    for (size_t s = 0; s < seeds.size(); ++s)
        init.seeds[s] = seeds[s].load();
}

bool RiqLiveProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    if (sendMode)
        return layouts.getMainInputChannelSet() == juce::AudioChannelSet::stereo();

    for (int b = 1; b < layouts.outputBuses.size(); ++b)
        if (! layouts.outputBuses[b].isDisabled() && layouts.outputBuses[b] != juce::AudioChannelSet::stereo())
            return false;
    return true;
}

void RiqLiveProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    sr = sampleRate;
    delaySamples = juce::jmax (64, samplesPerBlock);
    setLatencySamples (isEngineMode() ? delaySamples : 0);

    if (sendMode)
        return;

    for (auto& s : stems)
        s.prepare (sampleRate);

    const int maxBlock = juce::jmax (samplesPerBlock, 4096);
    srcBuf.setSize (2, maxBlock);
    tmpBuf.setSize (2, maxBlock);
    mixBuf.setSize (2, maxBlock);
    for (auto& b : stemBufs)
        b.setSize (2, maxBlock);

    juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32) maxBlock, 2 };
    limiter.prepare (spec);
    limiter.setThreshold (-0.3f);
    limiter.setRelease (80.0f);

    masterGain.reset (sampleRate, 0.02);
    masterGain.setCurrentAndTargetValue (dbToGain (param (ID::masterGain)));

    wasPlaying = false;
}

RiqLiveProcessor::~RiqLiveProcessor()
{
    if (sendMode && ownedEntry >= 0)
    {
        auto id = instanceId;
        SharedBus::get().sends[ownedEntry].owner.compare_exchange_strong (id, 0);
    }
}

void RiqLiveProcessor::updateTrackProperties (const TrackProperties& properties)
{
    const auto name = properties.name.value_or (juce::String());
    name.copyToUTF8 (trackName, sizeof (trackName) - 1);
    trackName[sizeof (trackName) - 1] = 0;
    trackNameVersion.fetch_add (1);
}

int RiqLiveProcessor::getSlotSendCount (int slot)
{
    const auto now = juce::Time::getMillisecondCounter();
    int count = 0;
    for (const auto& e : SharedBus::get().sends)
        if (e.isActive (now) && e.slot.load() == slot)
            ++count;
    return count;
}

juce::String RiqLiveProcessor::getSlotTrackName (int slot)
{
    const auto now = juce::Time::getMillisecondCounter();
    juce::StringArray names;
    for (const auto& e : SharedBus::get().sends)
        if (e.isActive (now) && e.slot.load() == slot)
            names.add (e.getName().isNotEmpty() ? e.getName() : juce::String ("track"));
    return names.joinIntoString (" + ");
}

bool RiqLiveProcessor::readSlot (int slot, juce::int64 t, float* l, float* r, float* tmpL, float* tmpR, int n)
{
    // Sum every Send pointed at this slot, exactly like several tracks routed into one.
    const auto now = juce::Time::getMillisecondCounter();
    bool any = false;
    for (const auto& e : SharedBus::get().sends)
    {
        if (! e.isActive (now) || e.slot.load() != slot)
            continue;
        if (! e.ring.read (t, any ? tmpL : l, any ? tmpR : r, n))
            continue;
        if (any)
        {
            juce::FloatVectorOperations::add (l, tmpL, n);
            juce::FloatVectorOperations::add (r, tmpR, n);
        }
        any = true;
    }
    if (! any)
    {
        std::fill (l, l + n, 0.0f);
        std::fill (r, r + n, 0.0f);
    }
    return any;
}

void RiqLiveProcessor::processSend (juce::AudioBuffer<float>& buffer, bool playing, juce::int64 t)
{
    const int n = buffer.getNumSamples();
    auto* L = buffer.getWritePointer (0);
    auto* R = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : L;

    float pk = 0.0f;
    for (int i = 0; i < n; ++i)
        pk = juce::jmax (pk, std::abs (L[i]), std::abs (R[i]));
    sendLevel.store (juce::jmax (pk, sendLevel.load() * 0.85f));

    auto& bus = SharedBus::get();
    const int slot = juce::jlimit (0, 2, getSendSlot());
    const auto now = juce::Time::getMillisecondCounter();

    // Each Send keeps its own ring in the pool (heartbeat every block, playing or not).
    if (ownedEntry >= 0 && ! bus.sends[ownedEntry].claim (instanceId, now))
        ownedEntry = -1;
    for (int i = 0; ownedEntry < 0 && i < SharedBusData::kMaxSends; ++i)
        if (bus.sends[i].claim (instanceId, now))
            ownedEntry = i;

    poolFull.store (ownedEntry < 0);
    if (ownedEntry < 0)
        return; // more than kMaxSends Sends: leave this track audible

    auto& entry = bus.sends[ownedEntry];
    entry.slot.store (slot);
    if (publishedNameVersion != trackNameVersion.load())
    {
        publishedNameVersion = trackNameVersion.load();
        std::memcpy (entry.name, trackName, sizeof (trackName));
    }

    if (playing)
    {
        entry.ring.write (t, L, R, n);
        // While playing, the engine plays this track; stopped, it passes through.
        if (param (ID::sendThru) < 0.5f)
            buffer.clear();
    }
}

void RiqLiveProcessor::handleMidi (const juce::MidiBuffer& midi)
{
    const int base = (int) param (ID::fillNote);

    for (const auto meta : midi)
    {
        const auto m = meta.getMessage();
        if (m.isNoteOn())
        {
            const int k = m.getNoteNumber() - base;
            if (k >= 0 && k < 16)
                heldFill |= (1u << k);
            else if (m.getNoteNumber() >= 60 && m.getNoteNumber() < 60 + kNumScenes)
                pendingScene.store (m.getNoteNumber() - 60);
        }
        else if (m.isNoteOff())
        {
            const int k = m.getNoteNumber() - base;
            if (k >= 0 && k < 16)
                heldFill &= ~(1u << k);
        }
        else if (m.isAllNotesOff() || m.isAllSoundOff())
        {
            heldFill = 0;
        }
    }

    fillMask.store (heldFill);
}

void RiqLiveProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    if (n == 0)
        return;

    // ---- host transport ----
    bool playing = false;
    double bpm = 120.0, ppq = 0.0, bpb = 4.0;
    juce::int64 t = 0;

    if (auto* ph = getPlayHead())
    {
        if (auto pos = ph->getPosition())
        {
            playing = pos->getIsPlaying();
            bpm = pos->getBpm().orFallback (120.0);
            ppq = pos->getPpqPosition().orFallback (0.0);
            if (auto ts = pos->getTimeSignature())
                bpb = juce::jmax (1.0, ts->numerator * 4.0 / juce::jmax (1, ts->denominator));
            t = pos->getTimeInSamples().orFallback ((juce::int64) std::llround (ppq * 60.0 / bpm * sr));
        }
    }
    bpm = juce::jlimit (20.0, 999.0, bpm);
    transportPlaying.store (playing);
    hostBpm.store (bpm);

    if (sendMode)
    {
        midi.clear();
        processSend (buffer, playing, t);
        return;
    }

    handleMidi (midi);
    midi.clear();

    const double spb = sr * 60.0 / bpm;
    const double pps = 1.0 / spb;
    const double ppqT = ppq - delaySamples * pps;
    const juce::int64 readT = t - delaySamples;

    // ---- scenes ----
    int req = pendingScene.exchange (-1);
    const int sp = dynamic_cast<juce::AudioParameterChoice*> (apvts.getParameter (ID::scene))->getIndex();
    if (sp != lastSceneParam)
    {
        lastSceneParam = sp;
        if (sp > 0)
            req = sp - 1;
    }
    if (req >= 0)
    {
        const int q = (int) param (ID::sceneQ);
        if (! playing || q == 0)
        {
            applyScene (req);
            sceneQueued = -1;
        }
        else
        {
            const double grid = q == 1 ? 1.0 : bpb;
            sceneAt = std::ceil (ppqT / grid - 1.0e-9) * grid;
            sceneQueued = req;
        }
    }
    if (sceneQueued >= 0 && ppqT + n * pps >= sceneAt)
    {
        applyScene (sceneQueued);
        sceneQueued = -1;
    }

    // ---- evolve buttons (rising edge) ----
    for (int k = 0; k < kNumStems * 2; ++k)
    {
        const bool ev = param (ID::stem (k % kNumStems, k < kNumStems ? "evolveArr" : "evolveRev")) > 0.5f;
        if (ev && ! lastEvolve[(size_t) k])
            seeds[(size_t) k].store (hash32 (seeds[(size_t) k].load(), 77u, (juce::uint32) k) | 1u);
        lastEvolve[(size_t) k] = ev;
    }

    if (! playing)
    {
        buffer.clear();
        if (wasPlaying)
            for (auto& s : stems)
                s.resetPlayback();
        wasPlaying = false;
        return;
    }

    // Hosts may report positions that wobble by a few samples; only a real relocation counts as a jump.
    if (! wasPlaying || std::abs (t - expectedTime) > 64)
    {
        // Transport started or jumped: the delayed region of the rings is stale.
        jumpMute = delaySamples;
        for (auto& s : stems)
            s.resetPlayback();
    }
    wasPlaying = true;
    expectedTime = t + n;

    // ---- shared per-block settings ----
    const double loopBeats = kLoopBars[juce::jlimit (0, 2, (int) param (ID::loopLen))] * bpb;
    const auto protect = (Protect) juce::jlimit (0, 3, (int) param (ID::protect));
    const int freezeQ = (int) param (ID::freezeQ);
    const float iq = param (ID::iqArrange);
    const bool iqEvolve = param (ID::iqEvolve) > 0.5f;
    const bool iqReverse = param (ID::iqReverse) > 0.5f;

    const auto barIdx = (juce::int64) std::floor (ppqT / bpb);

    // ---- Beat Assistant autopilot ----
    constexpr int kPhraseBars[] = { 4, 8, 16 };
    constexpr int kEvolveBars[] = { 1, 2, 4, 8, 16 };
    const int phraseBars = kPhraseBars[juce::jlimit (0, 2, (int) param (ID::apPhrase))];
    const int phrasePosNow = (int) (((barIdx % phraseBars) + phraseBars) % phraseBars);
    const bool fillBar = phrasePosNow == phraseBars - 1;
    const bool halfFillBar = phraseBars >= 8 && phrasePosNow == phraseBars / 2 - 1;
    const float apFill = param (ID::apFill), apMotion = param (ID::apMotion), apRatchet = param (ID::apRatchet);
    const float apRevDepth = param (ID::apRevDepth), apDrop = param (ID::apDrop);
    phrasePos.store (phrasePosNow);
    phraseLen.store (phraseBars);

    std::array<bool, kNumStems> linkedArr {};
    for (int s = 0; s < kNumStems; ++s)
        linkedArr[(size_t) s] = param (ID::stem (s, "link")) > 0.5f;

    if (barIdx != lastIqBar)
    {
        // IQ Evolve presses the stem Evolve buttons by itself at chosen bars.
        const int every = kEvolveBars[juce::jlimit (0, 4, (int) param (ID::apEvolveEvery))];
        if (iqEvolve && lastIqBar != std::numeric_limits<juce::int64>::min() && barIdx % every == 0)
        {
            const float chance = param (ID::apEvolveChance);
            for (int s = 0; s < kNumStems; ++s)
            {
                if (! linkedArr[(size_t) s] || hash01 ((juce::uint32) barIdx, (juce::uint32) s, 31u) >= chance)
                    continue;
                for (int k : { s, s + kNumStems })
                    seeds[(size_t) k].store (hash32 (seeds[(size_t) k].load(), (juce::uint32) barIdx, 33u) | 1u);
                evolveCount[s].fetch_add (1);
            }
        }
        lastIqBar = barIdx;
    }

    // Drops: a linked stem sits out a bar now and then, never on the phrase start,
    // and at least one linked stem always keeps playing.
    std::array<bool, kNumStems> drop {};
    if (iq > 0.0f && apDrop > 0.0f && phrasePosNow != 0)
    {
        int keep = -1, linkedCount = 0, droppedCount = 0;
        float best = 2.0f;
        for (int s = 0; s < kNumStems; ++s)
        {
            if (! linkedArr[(size_t) s])
                continue;
            ++linkedCount;
            const float h = hash01 ((juce::uint32) barIdx, (juce::uint32) s, 41u);
            drop[(size_t) s] = h < apDrop * 0.5f;
            droppedCount += drop[(size_t) s] ? 1 : 0;
            if (h < best) { best = h; keep = s; }
        }
        if (linkedCount > 0 && droppedCount == linkedCount)
            drop[(size_t) keep] = false;
    }

    bool anySolo = false;
    for (int s = 0; s < kNumStems; ++s)
        anySolo = anySolo || param (ID::stem (s, "solo")) > 0.5f;

    // ---- keyboard fills ----
    FillSettings fill;
    fill.sliceBeats = kSliceBeats[juce::jlimit (0, 3, (int) param (ID::fillSize))];
    if (heldFill != 0 && ! fillWasActive)
        fillStartSlot = std::ceil (ppqT / fill.sliceBeats - 1.0e-9);
    fillWasActive = heldFill != 0;
    if (heldFill != 0)
    {
        const int nFill = juce::jmax (1, (int) std::round (loopBeats / fill.sliceBeats));
        fill.active = true;
        fill.startSlot = fillStartSlot;
        for (int k = 0; k < 16; ++k)
            if (heldFill & (1u << k))
                fill.keys[(size_t) fill.numKeys++] = k % nFill;
    }

    // ---- render stems ----
    auto* iL = srcBuf.getWritePointer (0);
    auto* iR = srcBuf.getWritePointer (1);

    for (int s = 0; s < kNumStems; ++s)
    {
        const int src = juce::jlimit (0, 2, (int) param (ID::stem (s, "src")));
        const bool ok = readSlot (src, readT, iL, iR, tmpBuf.getWritePointer (0), tmpBuf.getWritePointer (1), n);
        sourceOk[s].store (ok ? 1 : 0);

        if (jumpMute > 0)
        {
            const int m = juce::jmin (n, jumpMute);
            std::fill (iL, iL + m, 0.0f);
            std::fill (iR, iR + m, 0.0f);
        }

        const bool linked = linkedArr[(size_t) s];
        StemSettings st;
        st.loopBeats = loopBeats;
        st.sliceBeats = kSliceBeats[juce::jlimit (0, 3, (int) param (ID::stem (s, "slice")))];
        st.beatsPerBar = bpb;
        st.protect = protect;
        st.ratchetZone = juce::jlimit (0, 2, (int) param (ID::stem (s, "rzone")));
        st.arrange = param (ID::stem (s, "arrange"));
        st.ratchet = param (ID::stem (s, "ratchet"));
        st.reverse = param (ID::stem (s, "reverse"));
        st.silence = param (ID::stem (s, "silence"));
        st.seed = seeds[(size_t) s].load();
        st.seedRev = seeds[(size_t) (s + kNumStems)].load();

        if (linked && iq > 0.0f)
        {
            // IQ Arrange takes over the stem Arrange knob: gentle motion from bar to bar,
            // building into a fill at the end of the phrase (and a smaller one half way).
            const float wobble = hash01 ((juce::uint32) barIdx, (juce::uint32) s, 11u);
            float a = iq * (0.55f + apMotion * (wobble - 0.5f) * 0.9f);
            float r = iq * apRatchet * 0.15f * wobble;
            if (fillBar)
            {
                a = juce::jmax (a, iq * (0.5f + 0.5f * apFill) + apFill * 0.25f);
                r = juce::jmax (r, apRatchet * apFill * (0.4f + 0.6f * iq));
            }
            else if (halfFillBar)
            {
                a = juce::jmax (a, iq * (0.45f + 0.35f * apFill));
                r = juce::jmax (r, apRatchet * apFill * 0.5f * iq);
            }
            st.arrange = juce::jlimit (0.0f, 1.0f, a);
            st.ratchet = juce::jlimit (0.0f, 1.0f, juce::jmax (st.ratchet, r));
        }
        if (linked && iqReverse)
        {
            const float base = (barIdx & 1) ? apRevDepth : apRevDepth * 0.35f;
            st.reverse = juce::jmin (1.0f, juce::jmax (st.reverse * (1.0f + apRevDepth), base));
        }
        effArrange[s].store (st.arrange);
        effReverse[s].store (st.reverse);
        effRatchet[s].store (st.ratchet);
        dropped[s].store (drop[(size_t) s] ? 1 : 0);

        st.freeze = param (ID::stem (s, "freeze")) > 0.5f;
        st.freezeQuant = freezeQ;
        st.roll = param (ID::stem (s, "roll")) > 0.5f;
        const bool solo = param (ID::stem (s, "solo")) > 0.5f;
        st.audible = param (ID::stem (s, "on")) > 0.5f && (! anySolo || solo) && ! drop[(size_t) s];

        const float g = dbToGain (param (ID::stem (s, "gain")));
        const float pan = param (ID::stem (s, "pan"));
        st.gainL = g * juce::jmin (1.0f, 1.0f - pan);
        st.gainR = g * juce::jmin (1.0f, 1.0f + pan);

        auto& sb = stemBufs[(size_t) s];
        sb.clear (0, 0, n);
        sb.clear (1, 0, n);
        stems[(size_t) s].process (iL, iR, sb.getWritePointer (0), sb.getWritePointer (1), n, ppqT, pps, spb, st, fill);
    }

    jumpMute = juce::jmax (0, jumpMute - n);

    // ---- outputs: every stem on its own bus; the Mix bus gets the stems set to "Mix" ----
    masterGain.setTargetValue (dbToGain (param (ID::masterGain)));
    const float g0 = masterGain.getCurrentValue();
    masterGain.skip (n);
    const float g1 = masterGain.getCurrentValue();

    buffer.clear();
    mixBuf.clear (0, 0, n);
    mixBuf.clear (1, 0, n);

    for (int s = 0; s < kNumStems; ++s)
    {
        auto& sb = stemBufs[(size_t) s];
        for (int ch = 0; ch < 2; ++ch)
            sb.applyGainRamp (ch, 0, n, g0, g1);

        if (param (ID::stem (s, "out")) < 0.5f)
            for (int ch = 0; ch < 2; ++ch)
                mixBuf.addFrom (ch, 0, sb, ch, 0, n);

        if (s + 1 < getBusCount (false) && getBus (false, s + 1)->isEnabled())
        {
            auto out = getBusBuffer (buffer, false, s + 1);
            for (int ch = 0; ch < juce::jmin (2, out.getNumChannels()); ++ch)
                out.copyFrom (ch, 0, sb, ch, 0, n);
        }
    }

    if (param (ID::limiter) > 0.5f)
    {
        juce::dsp::AudioBlock<float> block (mixBuf);
        auto sub = block.getSubBlock (0, (size_t) n);
        limiter.process (juce::dsp::ProcessContextReplacing<float> (sub));
    }

    auto mainOut = getBusBuffer (buffer, false, 0);
    for (int ch = 0; ch < juce::jmin (2, mainOut.getNumChannels()); ++ch)
        mainOut.copyFrom (ch, 0, mixBuf, ch, 0, n);
}

void RiqLiveProcessor::applyScene (int index)
{
    if (index < 0 || index >= kNumScenes)
        return;

    Scene sc;
    {
        const juce::SpinLock::ScopedTryLockType lock (sceneLock);
        if (! lock.isLocked())
            return;
        sc = scenes[(size_t) index];
    }
    if (! sc.valid)
        return;

    for (size_t k = 0; k < sceneParams.size(); ++k)
        sceneParams[k]->setValueNotifyingHost (sc.values[k]);
    for (size_t s = 0; s < seeds.size(); ++s)
        seeds[s].store (sc.seeds[s]);

    activeScene.store (index);
}

void RiqLiveProcessor::saveScene (int index)
{
    if (index <= 0 || index >= kNumScenes)
        return;

    Scene sc;
    sc.valid = true;
    for (size_t k = 0; k < sceneParams.size(); ++k)
        sc.values[k] = sceneParams[k]->getValue();
    for (size_t s = 0; s < seeds.size(); ++s)
        sc.seeds[s] = seeds[s].load();

    const juce::SpinLock::ScopedLockType lock (sceneLock);
    scenes[(size_t) index] = sc;
    activeScene.store (index);
}

bool RiqLiveProcessor::isSceneValid (int index) const
{
    const juce::SpinLock::ScopedLockType lock (sceneLock);
    return index >= 0 && index < kNumScenes && scenes[(size_t) index].valid;
}

void RiqLiveProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();

    juce::ValueTree sceneTree ("SCENES");
    {
        const juce::SpinLock::ScopedLockType lock (sceneLock);
        for (int i = 1; i < kNumScenes; ++i)
        {
            const auto& sc = scenes[(size_t) i];
            if (! sc.valid)
                continue;
            juce::ValueTree node ("SCENE");
            node.setProperty ("index", i, nullptr);
            juce::StringArray vals;
            for (auto v : sc.values)
                vals.add (juce::String (v, 6));
            node.setProperty ("values", vals.joinIntoString (","), nullptr);
            juce::StringArray sd;
            for (auto v : sc.seeds)
                sd.add (juce::String ((juce::int64) v));
            node.setProperty ("seeds", sd.joinIntoString (","), nullptr);
            sceneTree.appendChild (node, nullptr);
        }
    }

    juce::StringArray sd;
    for (auto& v : seeds)
        sd.add (juce::String ((juce::int64) v.load()));
    state.setProperty ("seeds", sd.joinIntoString (","), nullptr);
    state.appendChild (sceneTree, nullptr);

    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void RiqLiveProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr)
        return;

    auto state = juce::ValueTree::fromXml (*xml);
    if (! state.hasType (apvts.state.getType()))
        return;

    auto parseSeeds = [] (const juce::String& s, auto& dest)
    {
        const auto parts = juce::StringArray::fromTokens (s, ",", {});
        for (int i = 0; i < juce::jmin (parts.size(), (int) dest.size()); ++i)
            dest[(size_t) i] = (juce::uint32) parts[i].getLargeIntValue();
    };

    {
        std::array<juce::uint32, kNumStems * 2> sd {};
        for (size_t s = 0; s < seeds.size(); ++s)
            sd[s] = seeds[s].load();
        parseSeeds (state.getProperty ("seeds").toString(), sd);
        for (size_t s = 0; s < seeds.size(); ++s)
            seeds[s].store (sd[s]);
    }

    auto sceneTree = state.getChildWithName ("SCENES");
    {
        const juce::SpinLock::ScopedLockType lock (sceneLock);
        for (int i = 1; i < kNumScenes; ++i)
            scenes[(size_t) i].valid = false;

        for (auto node : sceneTree)
        {
            const int i = node.getProperty ("index");
            if (i <= 0 || i >= kNumScenes)
                continue;
            auto& sc = scenes[(size_t) i];
            const auto vals = juce::StringArray::fromTokens (node.getProperty ("values").toString(), ",", {});
            for (int k = 0; k < juce::jmin (vals.size(), (int) sc.values.size()); ++k)
                sc.values[(size_t) k] = vals[k].getFloatValue();
            parseSeeds (node.getProperty ("seeds").toString(), sc.seeds);
            sc.valid = true;
        }
    }

    state.removeChild (sceneTree, nullptr);
    apvts.replaceState (state);
}

juce::AudioProcessorEditor* RiqLiveProcessor::createEditor()
{
    if (sendMode)
        return new RiqSendEditor (*this);
    return new RiqLiveEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
   #ifndef RIQ_SEND
    #define RIQ_SEND 0
   #endif
    return new RiqLiveProcessor (RIQ_SEND != 0);
}
