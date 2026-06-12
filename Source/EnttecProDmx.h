#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <string>
#include <vector>
#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>

namespace hitnotedmx
{

inline constexpr int kDmxUniverseSize = 512;

// Talks to an ENTTEC DMX USB Pro over its USB serial (VCP) port using only
// macOS system frameworks (IOKit for discovery, POSIX termios for I/O).
// No FTDI D2XX SDK required, and the resulting plugin is self-contained.
// The send loop runs on a juce::HighResolutionTimer (its own high-priority
// thread, NOT the message thread) so frames emit at a steady cadence
// regardless of GUI/host load.
class EnttecProDmx : private juce::HighResolutionTimer
{
public:
    // DMX frames emitted per second. This is the real output clock — the
    // DMX line is refreshed once per call here, so any time-based output
    // effect (e.g. the strobe shutter) is quantised to this grid. 40 Hz is
    // near the DMX512 protocol ceiling for a full 512-channel universe.
    static constexpr int kSendRateHz = 40;

    EnttecProDmx();
    ~EnttecProDmx() override;

    bool connect();
    void disconnect();
    bool isConnected() const                                { return connected.load(); }

    int  scanDevices();
    int  numDevices() const                                 { return numDevicesDetected; }

    juce::String getStatusText() const;

    void setChannel (int channel1to512, juce::uint8 value);
    void setBlackout (bool enabled)                         { blackout.store (enabled); }

    // Global strobe shutter, applied at send time so it is perfectly
    // synced to the DMX output clock and decoupled from the audio block
    // rate. hz <= 0 disables it (frames pass through unmodified). When
    // enabled, exactly one frame per period is lit and the rest are black:
    // `hz` is the REPEAT rate, so the lit flash stays one send tick (the
    // shortest, ~20 Hz half-cycle) while the black gap grows as hz drops
    // (1 Hz = 1 lit / 39 black … 20 Hz = 1 lit / 1 black = max).
    void  setStrobeHz (float hz)                            { strobeHz.store (hz); }
    float getStrobeHz() const                               { return strobeHz.load(); }

private:
    void hiResTimerCallback() override;

    bool openPort (const std::string& devicePath);
    bool sendDmxFrame();
    int  sendPacket (int label, const unsigned char* data, int length);
    int  receivePacket (int label, unsigned char* data, unsigned int expectedLength);
    bool readByte (unsigned char& out);
    void closePort();

    std::atomic<bool>  connected { false };
    std::atomic<bool>  blackout  { false };
    std::atomic<float> strobeHz  { 0.0f };   // 0 = no strobe shutter
    std::uint64_t      txFrame   { 0 };       // emitted-frame counter (send thread only)
    int numDevicesDetected { 0 };

    // Index 0 is the DMX start code (kept 0). Channels 1..512 follow.
    static constexpr int kDataLen = 513;
    std::array<unsigned char, kDataLen> dmxData {};
    std::array<unsigned char, kDataLen> blackoutData {};

    int serialFd { -1 };
    std::string selectedDevicePath;        // /dev/cu.usbserial-EN...
    int firmwareMajor { 0 }, firmwareMinor { 0 };
    int refreshRate { 0 };
    juce::String lastError;
    juce::CriticalSection dataLock;
};

}
