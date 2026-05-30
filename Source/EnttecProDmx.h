#pragma once

#include <array>
#include <atomic>
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
// The send loop runs on a juce::Timer at the device refresh rate.
class EnttecProDmx : private juce::Timer
{
public:
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

private:
    void timerCallback() override;

    bool openPort (const std::string& devicePath);
    bool sendDmxFrame();
    int  sendPacket (int label, const unsigned char* data, int length);
    int  receivePacket (int label, unsigned char* data, unsigned int expectedLength);
    bool readByte (unsigned char& out);
    void closePort();

    std::atomic<bool> connected { false };
    std::atomic<bool> blackout  { false };
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
