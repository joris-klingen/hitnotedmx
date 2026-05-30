#pragma once

#include <juce_core/juce_core.h>

#include <array>
#include <atomic>
#include <optional>

namespace hitnotedmx
{

// A tiny lock-free single-producer / single-consumer ring buffer that
// the audio thread (producer) drops MIDI events into and the GUI thread
// (consumer) drains for display. Capacity is intentionally small —
// we only need to show recent activity, not every byte.
//
// The audio thread MUST NOT allocate, lock, or print; this is the safe
// way to hand events to anything else. See:
//   https://docs.juce.com/master/tutorial_audio_processor.html
//   "Audio thread safety"

struct MidiLogEntry
{
    enum class Kind : juce::uint8 { NoteOn, NoteOff };

    Kind kind { Kind::NoteOn };
    juce::uint8 channel { 1 };   // 1..16
    juce::uint8 pitch   { 0 };   // 0..127
    juce::uint8 velocity { 0 };  // 0..127 (release velocity for NoteOff)
    double samplePos { 0.0 };    // sample offset into the block
};

class MidiLog
{
public:
    static constexpr int kCapacity = 256;  // power-of-two-ish, generous

    MidiLog() = default;
    ~MidiLog() = default;

    // Called from the audio thread. Returns immediately; drops the
    // event if the ring is full.
    void push (const MidiLogEntry& e) noexcept
    {
        const int w = writeIndex.load (std::memory_order_relaxed);
        const int next = (w + 1) % kCapacity;
        if (next == readIndex.load (std::memory_order_acquire))
            return;  // full — drop
        buffer[w] = e;
        writeIndex.store (next, std::memory_order_release);
    }

    // Called from the GUI thread.
    std::optional<MidiLogEntry> pop() noexcept
    {
        const int r = readIndex.load (std::memory_order_relaxed);
        if (r == writeIndex.load (std::memory_order_acquire))
            return std::nullopt;  // empty
        const auto e = buffer[r];
        readIndex.store ((r + 1) % kCapacity, std::memory_order_release);
        return e;
    }

private:
    std::array<MidiLogEntry, kCapacity> buffer {};
    std::atomic<int> readIndex  { 0 };
    std::atomic<int> writeIndex { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MidiLog)
};

}  // namespace hitnotedmx
