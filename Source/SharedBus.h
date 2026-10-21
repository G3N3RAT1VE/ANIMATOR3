#pragma once

#include <juce_core/juce_core.h>
#include <atomic>
#include <cstring>
#include <memory>

#if JUCE_WINDOWS
 #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
 #endif
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <windows.h>
#endif

namespace riq
{

// Stereo ring buffer addressed by absolute host timeline position (samples).
// Senders write at their block's timeline position and the engine reads a fixed
// delay behind, so the order in which the host processes tracks doesn't matter.
// Plain data only: it lives in shared memory and all-zero bytes is a valid empty state.
struct TimelineRing
{
    static constexpr int kSize = 1 << 17;
    static constexpr int kMask = kSize - 1;

    void write (juce::int64 t, const float* l, const float* r, int n) noexcept
    {
        for (int i = 0; i < n; ++i)
        {
            const auto idx = (size_t) ((t + i) & kMask);
            data[0][idx] = l[i];
            data[1][idx] = r != nullptr ? r[i] : l[i];
        }
        writtenUntil.store (t + n, std::memory_order_release);
        lastWriteMs.store (juce::Time::getMillisecondCounter(), std::memory_order_release);
    }

    // Returns false (and outputs silence) when nobody has fed this ring recently.
    bool read (juce::int64 t, float* l, float* r, int n) const noexcept
    {
        const auto w = writtenUntil.load (std::memory_order_acquire);
        const auto last = lastWriteMs.load (std::memory_order_acquire);
        const auto age = juce::Time::getMillisecondCounter() - last;

        if (last == 0 || w < t + n || w - t > kSize / 2 || age > 500)
        {
            std::fill (l, l + n, 0.0f);
            std::fill (r, r + n, 0.0f);
            return false;
        }

        for (int i = 0; i < n; ++i)
        {
            const auto idx = (size_t) ((t + i) & kMask);
            l[i] = data[0][idx];
            r[i] = data[1][idx];
        }
        return true;
    }

    float data[2][kSize];
    std::atomic<juce::int64> writtenUntil;
    std::atomic<juce::uint32> lastWriteMs;
};

// One entry per AUTOPILOT Send instance. Every Send writes its own ring, and the engine
// sums all entries that point at a slot - like routing several tracks into one audio
// track - so any number of tracks can feed the same stem.
struct SendEntry
{
    static constexpr juce::uint32 kStaleMs = 300;

    TimelineRing ring;
    std::atomic<juce::uint64> owner;   // instance id of the Send using this entry, 0 = free
    std::atomic<juce::uint32> claimMs; // owner's heartbeat (every block, playing or not)
    std::atomic<int> slot;             // 0..2 = A..C
    char name[64];                     // owner's track name

    bool isActive (juce::uint32 now) const noexcept
    {
        return owner.load() != 0 && now - claimMs.load() <= kStaleMs;
    }

    // Heartbeat; returns true if `id` holds the entry after the call.
    bool claim (juce::uint64 id, juce::uint32 now) noexcept
    {
        auto cur = owner.load();
        if (cur != id)
        {
            if (cur != 0 && now - claimMs.load() <= kStaleMs)
                return false;
            if (! owner.compare_exchange_strong (cur, id))
                return false;
        }
        claimMs.store (now);
        return true;
    }

    juce::String getName() const
    {
        char copy[sizeof (name)];
        std::memcpy (copy, name, sizeof (name));
        copy[sizeof (name) - 1] = 0;
        return juce::String::fromUTF8 (copy);
    }
};

struct SharedBusData
{
    static constexpr int kMaxSends = 16;
    SendEntry sends[kMaxSends];
};

// One bus per host process, shared by the AUTOPILOT instrument and the AUTOPILOT Send
// effect. They are separate DLLs, so a static would not be shared: on Windows the bus
// lives in a named, process-local file mapping instead.
class SharedBus
{
public:
    static SharedBusData& get()
    {
        static SharedBus bus;
        return *bus.data;
    }

private:
    SharedBus()
    {
       #if JUCE_WINDOWS
        const auto name = "Local\\RiQLiveBus_v3_" + juce::String ((juce::int64) GetCurrentProcessId());
        mapping = CreateFileMappingW (INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0,
                                      (DWORD) sizeof (SharedBusData), name.toWideCharPointer());
        if (mapping != nullptr)
            data = static_cast<SharedBusData*> (MapViewOfFile (mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof (SharedBusData)));
       #endif
        if (data == nullptr)
        {
            fallback.reset (new SharedBusData()); // value-initialised: all zero
            data = fallback.get();
        }
    }

    ~SharedBus()
    {
       #if JUCE_WINDOWS
        if (fallback == nullptr && data != nullptr)
            UnmapViewOfFile (data);
        if (mapping != nullptr)
            CloseHandle (mapping);
       #endif
    }

   #if JUCE_WINDOWS
    HANDLE mapping = nullptr;
   #endif
    SharedBusData* data = nullptr;
    std::unique_ptr<SharedBusData> fallback;
};

} // namespace riq
