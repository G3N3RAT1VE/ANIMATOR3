#pragma once

#include <juce_core/juce_core.h>
#include <array>
#include <atomic>
#include <cmath>
#include <limits>
#include <vector>

namespace riq
{

inline double wrapBeat (double b, double len) noexcept
{
    const auto r = std::fmod (b, len);
    return r < 0.0 ? r + len : r;
}

inline juce::uint32 hash32 (juce::uint32 a, juce::uint32 b, juce::uint32 c) noexcept
{
    juce::uint32 h = a * 0x9E3779B1u ^ (b + 0x7F4A7C15u) * 0x85EBCA77u ^ (c + 0x165667B1u) * 0xC2B2AE3Du;
    h ^= h >> 15; h *= 0x2C1B3C6Du;
    h ^= h >> 12; h *= 0x297A2D39u;
    h ^= h >> 15;
    return h;
}

inline float hash01 (juce::uint32 a, juce::uint32 b, juce::uint32 c) noexcept
{
    return (float) (hash32 (a, b, c) >> 8) * (1.0f / 16777216.0f);
}

// Where in the capture buffer (in beats) the material for loop phase `phase` lives:
// the most recent recorded moment with that phase, not later than `ref`.
inline double srcBeatFor (double phase, double ref, double loop) noexcept
{
    auto s = std::floor (ref / loop) * loop + phase;
    if (s > ref + 1.0e-9)
        s -= loop;
    return s;
}

struct SliceDecision
{
    int src = 0;
    int ratchet = 1;
    bool rev = false;
    bool silent = false;
    bool altered = false;

    juce::uint32 pack() const noexcept
    {
        return (juce::uint32) (src & 0xff) | (rev ? 0x100u : 0u) | (silent ? 0x200u : 0u)
             | (altered ? 0x400u : 0u) | ((juce::uint32) (ratchet & 7) << 11);
    }

    static SliceDecision unpack (juce::uint32 v) noexcept
    {
        SliceDecision d;
        d.src = (int) (v & 0xff);
        d.rev = (v & 0x100u) != 0;
        d.silent = (v & 0x200u) != 0;
        d.altered = (v & 0x400u) != 0;
        d.ratchet = juce::jmax (1, (int) ((v >> 11) & 7));
        return d;
    }
};

enum class Protect { off = 0, downbeat, oneAndThree, allBeats };
enum class CaptureState { live = 0, armed, frozen };

struct StemSettings
{
    double loopBeats = 4.0;
    double sliceBeats = 0.25;
    double beatsPerBar = 4.0;
    Protect protect = Protect::oneAndThree;
    float arrange = 0.0f, ratchet = 0.0f, reverse = 0.0f, silence = 0.0f;
    juce::uint32 seed = 1;    // arrange / ratchet variation
    juce::uint32 seedRev = 2; // reverse variation
    bool freeze = false;
    int freezeQuant = 3; // 0 off, 1 beat, 2 bar, 3 loop
    bool roll = false;
    bool audible = true;
    float gainL = 1.0f, gainR = 1.0f;
};

struct FillSettings
{
    bool active = false;
    double sliceBeats = 0.5;
    double startSlot = 0.0;
    std::array<int, 16> keys {};
    int numKeys = 0;
};

class StemEngine
{
public:
    static constexpr int kCaptureBeats = 16;
    static constexpr int kBinsPerBeat = 32;
    static constexpr int kBins = kCaptureBeats * kBinsPerBeat;
    static constexpr int kMaxSlices = 128;

    StemEngine();

    void prepare (double sampleRate);
    void resetPlayback() noexcept;

    // Adds this stem's output into outL/outR. ppqStart is the (latency-delayed)
    // musical position of the first sample of the block.
    void process (const float* inL, const float* inR, float* outL, float* outR, int numSamples,
                  double ppqStart, double ppqPerSample, double samplesPerBeat,
                  const StemSettings& s, const FillSettings& fill) noexcept;

    // ---- state published for the editor ----
    std::array<std::atomic<float>, kBins> peaks;
    std::array<std::atomic<juce::uint32>, kMaxSlices> sliceInfo;
    std::atomic<double> dispRef { 0.0 }, dispLoop { 4.0 }, dispSlice { 0.25 }, dispPhase { 0.0 }, dispBeatsPerBar { 4.0 };
    std::atomic<int> dispSlices { 16 }, dispPlaying { -1 }, dispState { 0 };
    std::atomic<float> dispLevel { 0.0f };

private:
    void updateCaptureState (const StemSettings& s, double ppq) noexcept;
    void writeCapture (double ppq, float l, float r, double spb) noexcept;
    void readInterp (double idx, float& l, float& r) const noexcept;
    void buildTable (const StemSettings& s, double ref) noexcept;
    SliceDecision decide (int j, int n, const StemSettings& s) const noexcept;

    double sr = 44100.0;
    std::array<std::vector<float>, 2> cap;
    int capacity = 0;
    int capLen = 0;
    double capSpb = 0.0;
    int lastWrite = -1;

    int curBin = -1;
    float binPeak = 0.0f;
    double binSq = 0.0;
    int binCount = 0;
    std::array<float, kBins> binEnergy {};

    CaptureState state = CaptureState::live;
    double freezeAt = 0.0, frozenRef = 0.0;

    std::array<SliceDecision, kMaxSlices> table {};
    std::array<float, kMaxSlices> rank {};
    int numSlices = 16;

    int curSlice = -1;
    juce::int64 curInstance = std::numeric_limits<juce::int64>::min();
    SliceDecision cur;
    bool rollHeld = false, rollLatched = false;
    SliceDecision rollDec;

    bool havePrev = false, prevLive = false;
    double prevIdx = 0.0, prevStep = 0.0;
    int xfadeLen = 128, xfadeRemain = 0;
    double oldIdx = 0.0, oldStep = 0.0;
    bool oldLive = false;

    float gateGain = 1.0f, gateCoef = 0.01f;
    float gL = 1.0f, gR = 1.0f;
};

} // namespace riq
