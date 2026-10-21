#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include "SharedBus.h"
#include "StemEngine.h"
#include "Autopilot.h"
#include "Limiter.h"

namespace riq
{

constexpr int kNumStems = 3;
constexpr int kNumScenes = 9; // 0 = Init, 1..8 user
constexpr int kSceneParamsPerStem = 9;

namespace ID
{
    inline const juce::String slot = "slot", sendThru = "sendThru", loopLen = "loopLen", protect = "protect",
                              freezeQ = "freezeQ", sceneQ = "sceneQ", fillSize = "fillSize", fillNote = "fillNote",
                              masterGain = "masterGain", limiter = "limiter", iqArrange = "iqArrange",
                              iqEvolve = "iqEvolve", iqReverse = "iqReverse", scene = "scene",
                              apPhrase = "apPhrase", apFill = "apFill", apMotion = "apMotion", apRatchet = "apRatchet",
                              apEvolveEvery = "apEvolveEvery", apEvolveChance = "apEvolveChance",
                              apRevDepth = "apRevDepth", apDrop = "apDrop",
                              style = "style", limCeiling = "limCeiling", limDrive = "limDrive", limRelease = "limRelease";

    inline juce::String stem (int s, const char* name) { return "s" + juce::String (s + 1) + "_" + name; }
}

// Per-stem parameter names stored in scenes.
inline const char* const kSceneStemParams[kSceneParamsPerStem] =
    { "arrange", "ratchet", "reverse", "silence", "slice", "on", "solo", "pan", "gain" };

} // namespace riq

// One class, two plug-ins:
//  - engine (instrument, "AUTOPILOT"): 3 stem engines, outputs Mix + Stem 1..3
//  - send   (effect, "AUTOPILOT Send"): streams its track into bus slot A/B/C
class RiqLiveProcessor : public juce::AudioProcessor
{
public:
    explicit RiqLiveProcessor (bool sendMode);
    ~RiqLiveProcessor() override;

    void updateTrackProperties (const TrackProperties& properties) override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return sendMode ? "AUTOPILOT Send" : "AUTOPILOT"; }
    bool acceptsMidi() const override { return ! sendMode; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // ---- editor API ----
    const bool sendMode;
    juce::AudioProcessorValueTreeState apvts;
    riq::StemEngine& getStem (int i) { return stems[(size_t) i]; }
    bool isEngineMode() const { return ! sendMode; }
    int getSendSlot() const { return sendMode ? (int) apvts.getRawParameterValue (riq::ID::slot)->load() : -1; }

    void requestScene (int index) { pendingScene.store (index); }
    void saveScene (int index);
    bool isSceneValid (int index) const;
    int getActiveScene() const { return activeScene.load(); }

    std::atomic<float> sendLevel { 0.0f };
    std::atomic<bool> poolFull { false }; // send: no free entry in the shared pool

    // Which Sends feed a slot, readable from both editors.
    static int getSlotSendCount (int slot);
    static bool isSlotFed (int slot) { return getSlotSendCount (slot) > 0; }
    static juce::String getSlotTrackName (int slot);
    std::atomic<bool> transportPlaying { false };
    std::atomic<double> hostBpm { 120.0 };
    std::atomic<int> sourceOk[riq::kNumStems] {};
    std::atomic<juce::uint32> fillMask { 0 };

    // Beat Assistant state for the editor
    std::atomic<float> effArrange[riq::kNumStems] {}, effReverse[riq::kNumStems] {}, effRatchet[riq::kNumStems] {};
    std::atomic<int> dropped[riq::kNumStems] {}, evolveCount[riq::kNumStems] {};
    std::atomic<int> phrasePos { 0 }, phraseLen { 8 };
    std::atomic<juce::int64> currentBar { 0 };

    // Output loudness (Mix bus, after the limiter) and limiter activity.
    std::atomic<float> mixLufs { -70.0f }, mixPeakDb { -60.0f }, limiterGrDb { 0.0f };

    // Autopilot inputs from the current parameter values (also used by the phrase map).
    riq::AutopilotInput makeAutopilotInput() const;

    // ---- presets: whole plugin state (parameters, scenes, variations) as files ----
    static juce::File getPresetFolder();
    static juce::StringArray getPresetNames();
    bool savePreset (const juce::String& name);
    bool loadPreset (const juce::String& name);
    bool deletePreset (const juce::String& name);
    bool renamePreset (const juce::String& from, const juce::String& to);
    void loadInitPreset();
    juce::String getCurrentPresetName() const;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout (bool sendMode);
    static BusesProperties createBuses (bool sendMode);

    struct Scene
    {
        bool valid = false;
        std::array<float, riq::kNumStems * riq::kSceneParamsPerStem> values {};
        std::array<juce::uint32, riq::kNumStems * 2> seeds {}; // arrange seeds, then reverse seeds
    };

    void processSend (juce::AudioBuffer<float>&, bool playing, juce::int64 t);
    std::unique_ptr<juce::XmlElement> createStateXml();
    void applyStateXml (const juce::XmlElement& xml);
    static bool readSlot (int slot, juce::int64 t, float* l, float* r, float* tmpL, float* tmpR, int n);
    void applyScene (int index);
    void handleMidi (const juce::MidiBuffer& midi);
    float param (const juce::String& id) const { return apvts.getRawParameterValue (id)->load(); }

    std::array<riq::StemEngine, riq::kNumStems> stems;
    std::array<juce::RangedAudioParameter*, riq::kNumStems * riq::kSceneParamsPerStem> sceneParams {};

    std::array<Scene, riq::kNumScenes> scenes;
    juce::SpinLock sceneLock;
    std::atomic<int> pendingScene { -1 }, activeScene { 0 };
    double sceneAt = -1.0;
    int sceneQueued = -1;
    int lastSceneParam = 0;

    // [0..2] arrange seeds, [3..5] reverse seeds
    std::array<std::atomic<juce::uint32>, riq::kNumStems * 2> seeds;
    std::array<bool, riq::kNumStems * 2> lastEvolve {};

    juce::int64 lastIqBar = std::numeric_limits<juce::int64>::min();
    juce::int64 hostBarPrev = std::numeric_limits<juce::int64>::min();
    juce::int64 apBar = 0; // bars actually played (keeps counting through host loops)

    juce::uint32 heldFill = 0;
    double fillStartSlot = 0.0;
    bool fillWasActive = false;

    double sr = 44100.0;
    int delaySamples = 512;
    juce::int64 expectedTime = 0;
    int jumpMute = 0;

    // send side
    const juce::uint64 instanceId;
    int ownedEntry = -1;
    char trackName[64] {};
    std::atomic<int> trackNameVersion { 0 };
    int publishedNameVersion = -1;
    bool wasPlaying = false;

    juce::AudioBuffer<float> srcBuf, tmpBuf, mixBuf;
    std::array<juce::AudioBuffer<float>, riq::kNumStems> stemBufs;
    std::array<riq::TruePeakLimiter, riq::kNumStems + 1> limiters; // [0] Mix, [1..3] stem outs
    riq::LoudnessMeter loudness;
    juce::String presetName;
    mutable juce::CriticalSection presetLock;
    juce::SmoothedValue<float> masterGain;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RiqLiveProcessor)
};
