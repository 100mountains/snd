// snd::midi -- CoreMIDI on macOS (modern MIDIEventList API), stubs elsewhere
// until Windows/Linux backends are needed.

#include "snd/midi.h"

#if defined(__APPLE__)

#include <CoreFoundation/CoreFoundation.h>
#include <CoreMIDI/CoreMIDI.h>

namespace snd::midi {

namespace {

MIDIClientRef client()
{
    static MIDIClientRef ref = [] {
        MIDIClientRef c = 0;
        MIDIClientCreateWithBlock(CFSTR("SND"), &c, ^(const MIDINotification*) {
        });
        return c;
    }();
    return ref;
}

std::string endpointName(MIDIEndpointRef ep)
{
    CFStringRef name = nullptr;
    if (MIDIObjectGetStringProperty(ep, kMIDIPropertyDisplayName, &name) != noErr ||
        !name)
        return {};
    char buf[256] = {};
    CFStringGetCString(name, buf, sizeof(buf), kCFStringEncodingUTF8);
    CFRelease(name);
    return buf;
}

// Parse MIDI 1.0 channel-voice words out of a UMP event list and forward
// them as Messages. Runs on the MIDI thread.
void deliverEventList(const MIDIEventList* events, const Input::Callback& cb)
{
    if (!cb)
        return;
    const MIDIEventPacket* p = &events->packet[0];
    for (UInt32 i = 0; i < events->numPackets; ++i) {
        for (UInt32 w = 0; w < p->wordCount; ++w) {
            UInt32 word = p->words[w];
            if ((word >> 28) == 0x2) { // UMP message type 2: MIDI 1.0 voice
                Message m;
                m.status = (uint8_t)((word >> 16) & 0xFF);
                m.data1 = (uint8_t)((word >> 8) & 0x7F);
                m.data2 = (uint8_t)(word & 0x7F);
                cb(m);
            }
        }
        p = MIDIEventPacketNext(p);
    }
}

} // namespace

std::vector<std::string> inputDevices()
{
    std::vector<std::string> out;
    ItemCount n = MIDIGetNumberOfSources();
    for (ItemCount i = 0; i < n; ++i)
        out.push_back(endpointName(MIDIGetSource(i)));
    return out;
}

std::vector<std::string> outputDevices()
{
    std::vector<std::string> out;
    ItemCount n = MIDIGetNumberOfDestinations();
    for (ItemCount i = 0; i < n; ++i)
        out.push_back(endpointName(MIDIGetDestination(i)));
    return out;
}

// --- Input ------------------------------------------------------------------

struct Input::Impl {
    MIDIPortRef port = 0;
    MIDIEndpointRef virtualDest = 0;
    Callback cb;

    ~Impl()
    {
        if (port)
            MIDIPortDispose(port);
        if (virtualDest)
            MIDIEndpointDispose(virtualDest);
    }
};

Input::Input() : impl(new Impl) {}
Input::~Input() = default;

bool Input::open(const std::string& name, Callback cb, const std::string& virtualName)
{
    close();
    if (!client())
        return false;
    impl->cb = std::move(cb);
    Impl* im = impl.get();

    if (name.empty()) {
        // virtual destination: other apps (and our own Output) send to us
        CFStringRef cfName = CFStringCreateWithCString(
            nullptr, virtualName.c_str(), kCFStringEncodingUTF8);
        OSStatus err = MIDIDestinationCreateWithProtocol(
            client(), cfName, kMIDIProtocol_1_0, &impl->virtualDest,
            ^(const MIDIEventList* events, void*) {
                deliverEventList(events, im->cb);
            });
        CFRelease(cfName);
        return err == noErr;
    }

    if (MIDIInputPortCreateWithProtocol(client(), CFSTR("SND input"),
                                        kMIDIProtocol_1_0, &impl->port,
                                        ^(const MIDIEventList* events, void*) {
                                            deliverEventList(events, im->cb);
                                        }) != noErr)
        return false;

    ItemCount n = MIDIGetNumberOfSources();
    for (ItemCount i = 0; i < n; ++i) {
        MIDIEndpointRef src = MIDIGetSource(i);
        if (endpointName(src) == name)
            return MIDIPortConnectSource(impl->port, src, nullptr) == noErr;
    }
    close();
    return false;
}

void Input::close()
{
    if (impl->port) {
        MIDIPortDispose(impl->port);
        impl->port = 0;
    }
    if (impl->virtualDest) {
        MIDIEndpointDispose(impl->virtualDest);
        impl->virtualDest = 0;
    }
    impl->cb = nullptr;
}

// --- Output -----------------------------------------------------------------

struct Output::Impl {
    MIDIPortRef port = 0;
    MIDIEndpointRef dest = 0;

    ~Impl()
    {
        if (port)
            MIDIPortDispose(port);
    }
};

Output::Output() : impl(new Impl) {}
Output::~Output() = default;

bool Output::open(const std::string& name)
{
    close();
    if (!client())
        return false;
    if (MIDIOutputPortCreate(client(), CFSTR("SND output"), &impl->port) != noErr)
        return false;

    ItemCount n = MIDIGetNumberOfDestinations();
    for (ItemCount i = 0; i < n; ++i) {
        MIDIEndpointRef d = MIDIGetDestination(i);
        if (endpointName(d) == name) {
            impl->dest = d;
            return true;
        }
    }
    close();
    return false;
}

void Output::close()
{
    if (impl->port) {
        MIDIPortDispose(impl->port);
        impl->port = 0;
    }
    impl->dest = 0;
}

bool Output::send(const Message& m)
{
    if (!impl->port || !impl->dest)
        return false;
    MIDIEventList events;
    MIDIEventPacket* packet = MIDIEventListInit(&events, kMIDIProtocol_1_0);
    UInt32 word = (0x2u << 28) | ((UInt32)m.status << 16) | ((UInt32)m.data1 << 8) |
                  (UInt32)m.data2;
    packet = MIDIEventListAdd(&events, sizeof(events), packet, 0, 1, &word);
    if (!packet)
        return false;
    return MIDISendEventList(impl->port, impl->dest, &events) == noErr;
}

} // namespace snd::midi

#else // !__APPLE__ -- stubs until a Windows/Linux backend exists

namespace snd::midi {

std::vector<std::string> inputDevices() { return {}; }
std::vector<std::string> outputDevices() { return {}; }

struct Input::Impl {};
Input::Input() : impl(new Impl) {}
Input::~Input() = default;
bool Input::open(const std::string&, Callback, const std::string&) { return false; }
void Input::close() {}

struct Output::Impl {};
Output::Output() : impl(new Impl) {}
Output::~Output() = default;
bool Output::open(const std::string&) { return false; }
void Output::close() {}
bool Output::send(const Message&) { return false; }

} // namespace snd::midi

#endif
