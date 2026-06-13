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

    // True once a connect succeeds, until disconnect(): "this instance owns /
    // wants the port". Stays true across an auto-reconnect gap (when
    // isConnected() is briefly false). Drives the editor's Connect/Disconnect
    // toggle so only the owning instance shows "Disconnect".
    bool isRunning() const                                  { return shouldRun.load(); }

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

    // Retry cadence for the auto-reconnect loop after the link drops mid-run.
    // ~1 s between attempts (expressed in emitted frames at the send rate).
    static constexpr int kReconnectFrames = kSendRateHz;

private:
    void hiResTimerCallback() override;

    // `recordError` is suppressed on the auto-reconnect path: that runs on the
    // timer thread, and writing `lastError` there would race the message-thread
    // getStatusText() read. The first (user-initiated) connect records freely.
    bool openPort (const std::string& devicePath, bool recordError = true);
    bool setNonBlocking();         // switch the open fd to O_NONBLOCK for sends
    void attemptReconnect();       // timer-thread: throttled re-open of the port
    int  sendDmxFrame();           // 1 = sent, 0 = would-block (dropped), -1 = hard error
    int  sendPacket (int label, const unsigned char* data, int length);
    int  receivePacket (int label, unsigned char* data, unsigned int expectedLength);
    bool readByte (unsigned char& out);
    void closePort();

    std::atomic<bool>  connected { false };  // a live port we are sending on
    std::atomic<bool>  shouldRun  { false };  // user wants output (drives reconnect)
    int                reconnectFramesLeft { 0 };  // send-thread only: retry countdown
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
