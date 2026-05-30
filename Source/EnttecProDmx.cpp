#include "EnttecProDmx.h"

#include <cerrno>
#include <cstring>

#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#include <sys/ioctl.h>

#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/IOBSD.h>
#include <IOKit/serial/IOSerialKeys.h>
#include <IOKit/serial/ioss.h>

namespace hitnotedmx
{

// ENTTEC USB Pro packet labels (see ENTTEC's "DMX USB Pro Widget API Specification").
namespace
{
    constexpr int GET_WIDGET_PARAMS       = 3;
    constexpr int GET_WIDGET_PARAMS_REPLY = 3;
    constexpr int SET_DMX_TX_MODE         = 6;
    constexpr unsigned char MSG_START_CODE = 0x7E;
    constexpr unsigned char MSG_END_CODE   = 0xE7;
    constexpr int HEADER_LENGTH = 4;
    constexpr int MAX_PACKET_SIZE = 512;

    // The widget runs on an FTDI bridge; the host-side baud is effectively
    // ignored by the device, but a sane value must still be configured.
    constexpr speed_t kBaudRate = 57600;

    // The ENTTEC DMX USB Pro enumerates its USB serial callout device as
    // /dev/cu.usbserial-EN<serial>. We match on that prefix.
    bool isEnttecCalloutPath (const std::string& path)
    {
        return path.find ("usbserial-EN") != std::string::npos;
    }

    #pragma pack(push, 1)
    struct DmxUsbProParams
    {
        unsigned char FirmwareLSB;
        unsigned char FirmwareMSB;
        unsigned char BreakTime;
        unsigned char MaBTime;
        unsigned char RefreshRate;
    };
    #pragma pack(pop)

    // Enumerate all serial callout devices (/dev/cu.*) via IOKit.
    std::vector<std::string> listSerialCalloutDevices()
    {
        std::vector<std::string> paths;

        CFMutableDictionaryRef match = IOServiceMatching (kIOSerialBSDServiceValue);
        if (match == nullptr)
            return paths;

        CFDictionarySetValue (match,
                              CFSTR (kIOSerialBSDTypeKey),
                              CFSTR (kIOSerialBSDAllTypes));

        io_iterator_t iterator = IO_OBJECT_NULL;
        if (IOServiceGetMatchingServices (kIOMasterPortDefault, match, &iterator) != KERN_SUCCESS)
            return paths;

        io_object_t service = IO_OBJECT_NULL;
        while ((service = IOIteratorNext (iterator)) != IO_OBJECT_NULL)
        {
            if (auto cfPath = (CFStringRef) IORegistryEntryCreateCFProperty (
                    service, CFSTR (kIOCalloutDeviceKey), kCFAllocatorDefault, 0))
            {
                char buffer[1024] = { 0 };
                if (CFStringGetCString (cfPath, buffer, sizeof (buffer), kCFStringEncodingUTF8))
                    paths.emplace_back (buffer);

                CFRelease (cfPath);
            }

            IOObjectRelease (service);
        }

        IOObjectRelease (iterator);
        return paths;
    }
}

EnttecProDmx::EnttecProDmx()
{
    dmxData.fill (0);
    blackoutData.fill (0);
    scanDevices();
}

EnttecProDmx::~EnttecProDmx()
{
    stopTimer();
    disconnect();
}

int EnttecProDmx::scanDevices()
{
    int count = 0;
    std::string firstMatch;

    for (const auto& path : listSerialCalloutDevices())
    {
        if (isEnttecCalloutPath (path))
        {
            if (firstMatch.empty())
                firstMatch = path;
            ++count;
        }
    }

    selectedDevicePath = firstMatch;
    numDevicesDetected = count;
    return numDevicesDetected;
}

bool EnttecProDmx::openPort (const std::string& devicePath)
{
    // O_NONBLOCK so the open itself never stalls waiting on modem control
    // lines (a known FTDI-on-Apple-Silicon hang); we clear it again below so
    // the handshake reads can honour VMIN/VTIME timeouts.
    int fd = ::open (devicePath.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd < 0)
    {
        lastError = "Could not open " + juce::String (devicePath)
                  + " (" + juce::String (std::strerror (errno)) + ").";
        return false;
    }

    if (fcntl (fd, F_SETFL, 0) == -1)   // back to blocking I/O
    {
        lastError = "Could not configure the serial port (fcntl).";
        ::close (fd);
        return false;
    }

    struct termios options {};
    if (tcgetattr (fd, &options) != 0)
    {
        lastError = "tcgetattr failed on the serial port.";
        ::close (fd);
        return false;
    }

    // 8 data bits, no parity, one stop bit, no flow control, raw mode.
    options.c_cflag &= ~(tcflag_t) (PARENB | CSTOPB | CSIZE | CRTSCTS);
    options.c_cflag |= (tcflag_t) (CS8 | CREAD | CLOCAL);

    options.c_lflag &= ~(tcflag_t) (ICANON | ECHO | ECHOE | ECHONL | ISIG);
    options.c_iflag &= ~(tcflag_t) (IXON | IXOFF | IXANY);
    options.c_iflag &= ~(tcflag_t) (IGNBRK | BRKINT | PARMRK | ISTRIP
                                    | INLCR | IGNCR | ICRNL);
    options.c_oflag &= ~(tcflag_t) (OPOST | ONLCR);

    // Block up to 0.1s per read() returning whatever bytes have arrived.
    options.c_cc[VTIME] = 1;
    options.c_cc[VMIN]  = 0;

    if (tcsetattr (fd, TCSANOW, &options) != 0)
    {
        lastError = "tcsetattr failed on the serial port.";
        ::close (fd);
        return false;
    }

    // Set an arbitrary baud rate via the macOS-specific IOSSIOSPEED ioctl.
    speed_t baud = kBaudRate;
    if (ioctl (fd, IOSSIOSPEED, &baud) == -1)
    {
        lastError = "Could not set the serial baud rate.";
        ::close (fd);
        return false;
    }

    // Drop the FTDI latency timer to its minimum (1 ms). The default 16 ms
    // buffering is the knob behind much of the FTDI-on-Apple-Silicon lag and
    // the long drains on close. Best-effort: ignore if the driver rejects it.
    unsigned long latency = 1;
    ioctl (fd, IOSSDATALAT, &latency);

    serialFd = fd;
    return true;
}

bool EnttecProDmx::connect()
{
    if (connected.load())
        return true;

    if (selectedDevicePath.empty())
        scanDevices();

    if (selectedDevicePath.empty())
    {
        lastError = "No ENTTEC DMX USB Pro serial port found.";
        return false;
    }

    if (! openPort (selectedDevicePath))
        return false;

    tcflush (serialFd, TCIOFLUSH);

    int size = 0;
    if (sendPacket (GET_WIDGET_PARAMS, reinterpret_cast<unsigned char*> (&size), 2) <= 0)
    {
        if (sendPacket (GET_WIDGET_PARAMS, reinterpret_cast<unsigned char*> (&size), 2) <= 0)
        {
            closePort();
            lastError = "ENTTEC widget did not respond to GET_WIDGET_PARAMS.";
            return false;
        }
    }

    DmxUsbProParams params {};
    if (receivePacket (GET_WIDGET_PARAMS_REPLY,
                       reinterpret_cast<unsigned char*> (&params),
                       sizeof (params)) <= 0)
    {
        if (receivePacket (GET_WIDGET_PARAMS_REPLY,
                           reinterpret_cast<unsigned char*> (&params),
                           sizeof (params)) <= 0)
        {
            closePort();
            lastError = "ENTTEC widget did not return parameter reply.";
            return false;
        }
    }

    firmwareMajor = params.FirmwareMSB;
    firmwareMinor = params.FirmwareLSB;
    refreshRate   = params.RefreshRate;

    connected.store (true);
    lastError = {};
    startTimerHz (40);
    return true;
}

void EnttecProDmx::disconnect()
{
    stopTimer();
    closePort();
    connected.store (false);
    scanDevices();
}

void EnttecProDmx::closePort()
{
    if (serialFd >= 0)
    {
        // Discard any queued I/O and switch to non-blocking *before* close().
        // On Apple Silicon, close() on an FTDI VCP port drains pending output
        // through the kernel tty layer, which can stall on the USB pipe and
        // hang the calling thread (i.e. freeze the host on quit). With nothing
        // left to drain, close() returns immediately.
        tcflush (serialFd, TCIOFLUSH);
        fcntl (serialFd, F_SETFL, O_NONBLOCK);
        ::close (serialFd);
        serialFd = -1;
    }
}

juce::String EnttecProDmx::getStatusText() const
{
    if (connected.load())
    {
        return "Connected. Firmware "
             + juce::String (firmwareMajor) + "." + juce::String (firmwareMinor)
             + "\nRefresh rate: " + juce::String (refreshRate);
    }

    if (! lastError.isEmpty())
        return lastError;

    if (numDevicesDetected == 0)
        return "No ENTTEC DMX USB Pro found. Plug one in and retry.";
    if (numDevicesDetected == 1)
        return "Found an ENTTEC DMX USB Pro. Click \"Connect USB\" to open it.";
    return "Found " + juce::String (numDevicesDetected)
         + " ENTTEC devices. Please leave only one connected.";
}

void EnttecProDmx::setChannel (int channel, juce::uint8 value)
{
    if (channel < 1 || channel > kDmxUniverseSize)
        return;
    const juce::ScopedLock lock (dataLock);
    dmxData[(size_t) channel] = value;
}

void EnttecProDmx::timerCallback()
{
    if (! connected.load())
        return;

    if (! sendDmxFrame())
    {
        closePort();
        connected.store (false);
    }
}

bool EnttecProDmx::sendDmxFrame()
{
    const bool useBlackout = blackout.load();
    std::array<unsigned char, kDataLen> snapshot;
    {
        const juce::ScopedLock lock (dataLock);
        snapshot = useBlackout ? blackoutData : dmxData;
    }
    return sendPacket (SET_DMX_TX_MODE, snapshot.data(), kDataLen) > 0;
}

int EnttecProDmx::sendPacket (int label, const unsigned char* data, int length)
{
    if (serialFd < 0)
        return 0;

    // Assemble the full ENTTEC frame: start, label, len LSB/MSB, data, end.
    std::vector<unsigned char> packet;
    packet.reserve ((size_t) (HEADER_LENGTH + length + 1));
    packet.push_back (MSG_START_CODE);
    packet.push_back ((unsigned char) label);
    packet.push_back ((unsigned char) (length & 0xFF));
    packet.push_back ((unsigned char) ((length >> 8) & 0xFF));
    packet.insert (packet.end(), data, data + length);
    packet.push_back (MSG_END_CODE);

    size_t total = packet.size();
    size_t sent  = 0;
    while (sent < total)
    {
        ssize_t n = ::write (serialFd, packet.data() + sent, total - sent);
        if (n < 0)
        {
            if (errno == EINTR)
                continue;
            return 0;
        }
        if (n == 0)
            return 0;
        sent += (size_t) n;
    }

    return 1;
}

bool EnttecProDmx::readByte (unsigned char& out)
{
    if (serialFd < 0)
        return false;

    for (;;)
    {
        ssize_t n = ::read (serialFd, &out, 1);
        if (n == 1)
            return true;
        if (n < 0 && errno == EINTR)
            continue;
        return false;   // n == 0 (timeout) or hard error
    }
}

int EnttecProDmx::receivePacket (int label, unsigned char* data, unsigned int expectedLength)
{
    if (serialFd < 0)
        return 0;

    unsigned char byte = 0;
    unsigned char buffer[600];

    while (byte != (unsigned char) label)
    {
        while (byte != MSG_START_CODE)
        {
            if (! readByte (byte))
                return 0;
        }
        if (! readByte (byte))   // label byte
            return 0;
    }

    unsigned int length = 0;
    if (! readByte (byte))
        return 0;
    length = byte;
    if (! readByte (byte))
        return 0;
    length += ((unsigned int) byte) << 8;

    if (length > (unsigned int) MAX_PACKET_SIZE)
        return 0;

    for (unsigned int i = 0; i < length; ++i)
    {
        if (! readByte (buffer[i]))
            return 0;
    }

    if (! readByte (byte) || byte != MSG_END_CODE)
        return 0;

    std::memcpy (data, buffer, expectedLength);
    return 1;
}

}
