// snd::midi -- CoreMIDI on macOS (modern MIDIEventList API), ALSA sequencer
// on Linux, stubs on Windows until the WinMM/WinRT backend lands.

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

#elif defined(__linux__) // ALSA sequencer backend

#include <alsa/asoundlib.h>

#include <atomic>
#include <poll.h>
#include <thread>

namespace snd::midi {

namespace {

// display name = "client:port"; match against the whole thing, the client
// name alone, or the port name alone (so virtual ports find themselves)
bool nameMatches(const std::string& want, const char* client, const char* port)
{
    std::string c = client ? client : "", p = port ? port : "";
    return want == c + ":" + p || want == c || want == p;
}

std::vector<std::string> enumeratePorts(unsigned requiredCaps)
{
    std::vector<std::string> out;
    snd_seq_t* seq = nullptr;
    if (snd_seq_open(&seq, "default", SND_SEQ_OPEN_DUPLEX, 0) < 0)
        return out;
    snd_seq_client_info_t* cinfo;
    snd_seq_client_info_alloca(&cinfo);
    snd_seq_port_info_t* pinfo;
    snd_seq_port_info_alloca(&pinfo);
    snd_seq_client_info_set_client(cinfo, -1);
    while (snd_seq_query_next_client(seq, cinfo) >= 0) {
        int client = snd_seq_client_info_get_client(cinfo);
        if (client == snd_seq_client_id(seq))
            continue;
        snd_seq_port_info_set_client(pinfo, client);
        snd_seq_port_info_set_port(pinfo, -1);
        while (snd_seq_query_next_port(seq, pinfo) >= 0) {
            unsigned caps = snd_seq_port_info_get_capability(pinfo);
            if ((caps & requiredCaps) == requiredCaps)
                out.push_back(std::string(snd_seq_client_info_get_name(cinfo)) + ":" +
                              snd_seq_port_info_get_name(pinfo));
        }
    }
    snd_seq_close(seq);
    return out;
}

bool findPort(snd_seq_t* seq, const std::string& name, unsigned requiredCaps,
              snd_seq_addr_t& addr)
{
    snd_seq_client_info_t* cinfo;
    snd_seq_client_info_alloca(&cinfo);
    snd_seq_port_info_t* pinfo;
    snd_seq_port_info_alloca(&pinfo);
    snd_seq_client_info_set_client(cinfo, -1);
    while (snd_seq_query_next_client(seq, cinfo) >= 0) {
        int client = snd_seq_client_info_get_client(cinfo);
        if (client == snd_seq_client_id(seq))
            continue;
        snd_seq_port_info_set_client(pinfo, client);
        snd_seq_port_info_set_port(pinfo, -1);
        while (snd_seq_query_next_port(seq, pinfo) >= 0) {
            unsigned caps = snd_seq_port_info_get_capability(pinfo);
            if ((caps & requiredCaps) != requiredCaps)
                continue;
            if (nameMatches(name, snd_seq_client_info_get_name(cinfo),
                            snd_seq_port_info_get_name(pinfo))) {
                addr.client = (unsigned char)client;
                addr.port = (unsigned char)snd_seq_port_info_get_port(pinfo);
                return true;
            }
        }
    }
    return false;
}

} // namespace

std::vector<std::string> inputDevices()
{
    return enumeratePorts(SND_SEQ_PORT_CAP_READ | SND_SEQ_PORT_CAP_SUBS_READ);
}

std::vector<std::string> outputDevices()
{
    return enumeratePorts(SND_SEQ_PORT_CAP_WRITE | SND_SEQ_PORT_CAP_SUBS_WRITE);
}

// --- Input ------------------------------------------------------------------

struct Input::Impl {
    snd_seq_t* seq = nullptr;
    int port = -1;
    std::thread thread;
    std::atomic<bool> run{false};
    Callback cb;

    void loop()
    {
        int nfds = snd_seq_poll_descriptors_count(seq, POLLIN);
        std::vector<pollfd> fds((size_t)std::max(1, nfds));
        snd_seq_poll_descriptors(seq, fds.data(), (unsigned)fds.size(), POLLIN);
        while (run.load(std::memory_order_relaxed)) {
            if (poll(fds.data(), (nfds_t)fds.size(), 100) <= 0)
                continue;
            snd_seq_event_t* ev = nullptr;
            while (snd_seq_event_input(seq, &ev) >= 0 && ev) {
                Message m;
                bool known = true;
                switch (ev->type) {
                case SND_SEQ_EVENT_NOTEON:
                    m = Message::noteOn(ev->data.note.channel, ev->data.note.note,
                                        ev->data.note.velocity);
                    break;
                case SND_SEQ_EVENT_NOTEOFF:
                    m = Message::noteOff(ev->data.note.channel, ev->data.note.note);
                    break;
                case SND_SEQ_EVENT_CONTROLLER:
                    m = Message::control(ev->data.control.channel,
                                         (uint8_t)ev->data.control.param,
                                         (uint8_t)ev->data.control.value);
                    break;
                case SND_SEQ_EVENT_PITCHBEND:
                    m = Message::pitchBend(ev->data.control.channel,
                                           (uint16_t)(ev->data.control.value + 8192));
                    break;
                default:
                    known = false;
                }
                if (known && cb)
                    cb(m);
                if (snd_seq_event_input_pending(seq, 0) <= 0)
                    break;
            }
        }
    }

    void stop()
    {
        run.store(false);
        if (thread.joinable())
            thread.join();
        if (seq) {
            snd_seq_close(seq);
            seq = nullptr;
        }
        port = -1;
        cb = nullptr;
    }
};

Input::Input() : impl(new Impl) {}
Input::~Input() { close(); }

bool Input::open(const std::string& name, Callback cb, const std::string& virtualName)
{
    close();
    if (snd_seq_open(&impl->seq, "default", SND_SEQ_OPEN_DUPLEX, SND_SEQ_NONBLOCK) < 0)
        return false;
    snd_seq_set_client_name(impl->seq,
                            name.empty() ? virtualName.c_str() : "SND input");
    impl->port = snd_seq_create_simple_port(
        impl->seq, name.empty() ? virtualName.c_str() : "in",
        SND_SEQ_PORT_CAP_WRITE | SND_SEQ_PORT_CAP_SUBS_WRITE,
        SND_SEQ_PORT_TYPE_MIDI_GENERIC | SND_SEQ_PORT_TYPE_APPLICATION);
    if (impl->port < 0) {
        snd_seq_close(impl->seq);
        impl->seq = nullptr;
        return false;
    }
    if (!name.empty()) {
        snd_seq_addr_t src{};
        if (!findPort(impl->seq, name,
                      SND_SEQ_PORT_CAP_READ | SND_SEQ_PORT_CAP_SUBS_READ, src) ||
            snd_seq_connect_from(impl->seq, impl->port, src.client, src.port) < 0) {
            snd_seq_close(impl->seq);
            impl->seq = nullptr;
            return false;
        }
    }
    impl->cb = std::move(cb);
    impl->run.store(true);
    impl->thread = std::thread([im = impl.get()] { im->loop(); });
    return true;
}

void Input::close() { impl->stop(); }

// --- Output -----------------------------------------------------------------

struct Output::Impl {
    snd_seq_t* seq = nullptr;
    int port = -1;
};

Output::Output() : impl(new Impl) {}
Output::~Output() { close(); }

bool Output::open(const std::string& name)
{
    close();
    if (snd_seq_open(&impl->seq, "default", SND_SEQ_OPEN_DUPLEX, 0) < 0)
        return false;
    snd_seq_set_client_name(impl->seq, "SND output");
    impl->port = snd_seq_create_simple_port(
        impl->seq, "out", SND_SEQ_PORT_CAP_READ | SND_SEQ_PORT_CAP_SUBS_READ,
        SND_SEQ_PORT_TYPE_MIDI_GENERIC | SND_SEQ_PORT_TYPE_APPLICATION);
    snd_seq_addr_t dest{};
    if (impl->port < 0 ||
        !findPort(impl->seq, name,
                  SND_SEQ_PORT_CAP_WRITE | SND_SEQ_PORT_CAP_SUBS_WRITE, dest) ||
        snd_seq_connect_to(impl->seq, impl->port, dest.client, dest.port) < 0) {
        snd_seq_close(impl->seq);
        impl->seq = nullptr;
        impl->port = -1;
        return false;
    }
    return true;
}

void Output::close()
{
    if (impl->seq) {
        snd_seq_close(impl->seq);
        impl->seq = nullptr;
        impl->port = -1;
    }
}

bool Output::send(const Message& m)
{
    if (!impl->seq)
        return false;
    snd_seq_event_t ev;
    snd_seq_ev_clear(&ev);
    switch (m.type()) {
    case 0x90:
        snd_seq_ev_set_noteon(&ev, m.channel(), m.data1, m.data2);
        break;
    case 0x80:
        snd_seq_ev_set_noteoff(&ev, m.channel(), m.data1, m.data2);
        break;
    case 0xB0:
        snd_seq_ev_set_controller(&ev, m.channel(), m.data1, m.data2);
        break;
    case 0xE0:
        snd_seq_ev_set_pitchbend(&ev, m.channel(),
                                 (m.data1 | (m.data2 << 7)) - 8192);
        break;
    default:
        return false;
    }
    snd_seq_ev_set_source(&ev, (unsigned char)impl->port);
    snd_seq_ev_set_subs(&ev);
    snd_seq_ev_set_direct(&ev);
    bool ok = snd_seq_event_output(impl->seq, &ev) >= 0 &&
              snd_seq_drain_output(impl->seq) >= 0;
    return ok;
}

} // namespace snd::midi

#elif defined(_WIN32) // WinMM backend

#include <windows.h>
#include <mmsystem.h>

#include <cctype>

namespace snd::midi {

namespace {

// Case-insensitive "does the device name contain the query" (query "" matches
// the first device). WinMM addresses devices by index; we select by name.
bool nameMatches(const std::string& devName, const std::string& query)
{
    if (query.empty())
        return true;
    auto lower = [](std::string s) {
        for (auto& c : s)
            c = (char)std::tolower((unsigned char)c);
        return s;
    };
    return lower(devName).find(lower(query)) != std::string::npos;
}

} // namespace

std::vector<std::string> inputDevices()
{
    std::vector<std::string> out;
    const UINT n = midiInGetNumDevs();
    for (UINT i = 0; i < n; ++i) {
        MIDIINCAPSA caps{};
        if (midiInGetDevCapsA(i, &caps, sizeof(caps)) == MMSYSERR_NOERROR)
            out.emplace_back(caps.szPname);
    }
    return out;
}

std::vector<std::string> outputDevices()
{
    std::vector<std::string> out;
    const UINT n = midiOutGetNumDevs();
    for (UINT i = 0; i < n; ++i) {
        MIDIOUTCAPSA caps{};
        if (midiOutGetDevCapsA(i, &caps, sizeof(caps)) == MMSYSERR_NOERROR)
            out.emplace_back(caps.szPname);
    }
    return out;
}

// ── Input ──────────────────────────────────────────────────────────────────
// A namespace-level context (the WinMM callback proc, a free function, cannot
// name Input's private nested Impl).
namespace {
struct WinMidiIn {
    HMIDIIN handle = nullptr;
    Input::Callback cb;
};
} // namespace

struct Input::Impl {
    WinMidiIn in;
};

// WinMM delivers on its own thread. MIM_DATA packs the message in dwParam1
// (byte0 status, byte1 data1, byte2 data2). Sysex (MIM_LONGDATA) is ignored.
static void CALLBACK midiInProc(HMIDIIN, UINT msg, DWORD_PTR inst,
                                DWORD_PTR p1, DWORD_PTR)
{
    if (msg != MIM_DATA)
        return;
    auto* ctx = reinterpret_cast<WinMidiIn*>(inst);
    if (!ctx || !ctx->cb)
        return;
    Message m;
    m.status = (uint8_t)(p1 & 0xFF);
    m.data1 = (uint8_t)((p1 >> 8) & 0xFF);
    m.data2 = (uint8_t)((p1 >> 16) & 0xFF);
    m.frame = 0;
    ctx->cb(m);
}

Input::Input() : impl(new Impl) {}
Input::~Input() { close(); }

bool Input::open(const std::string& name, Callback cb, const std::string&)
{
    // WinMM cannot create virtual destinations; open a real input by name.
    close();
    impl->in.cb = std::move(cb);
    const UINT n = midiInGetNumDevs();
    for (UINT i = 0; i < n; ++i) {
        MIDIINCAPSA caps{};
        if (midiInGetDevCapsA(i, &caps, sizeof(caps)) != MMSYSERR_NOERROR)
            continue;
        if (!nameMatches(caps.szPname, name))
            continue;
        if (midiInOpen(&impl->in.handle, i, (DWORD_PTR)midiInProc,
                       (DWORD_PTR)&impl->in, CALLBACK_FUNCTION) !=
            MMSYSERR_NOERROR)
            return false;
        if (midiInStart(impl->in.handle) != MMSYSERR_NOERROR) {
            midiInClose(impl->in.handle);
            impl->in.handle = nullptr;
            return false;
        }
        return true;
    }
    return false;
}

void Input::close()
{
    if (impl->in.handle) {
        midiInStop(impl->in.handle);
        midiInReset(impl->in.handle);
        midiInClose(impl->in.handle);
        impl->in.handle = nullptr;
    }
}

// ── Output ─────────────────────────────────────────────────────────────────
struct Output::Impl {
    HMIDIOUT handle = nullptr;
};

Output::Output() : impl(new Impl) {}
Output::~Output() { close(); }

bool Output::open(const std::string& name)
{
    close();
    const UINT n = midiOutGetNumDevs();
    for (UINT i = 0; i < n; ++i) {
        MIDIOUTCAPSA caps{};
        if (midiOutGetDevCapsA(i, &caps, sizeof(caps)) != MMSYSERR_NOERROR)
            continue;
        if (!nameMatches(caps.szPname, name))
            continue;
        return midiOutOpen(&impl->handle, i, 0, 0, CALLBACK_NULL) ==
               MMSYSERR_NOERROR;
    }
    return false;
}

void Output::close()
{
    if (impl->handle) {
        midiOutReset(impl->handle);
        midiOutClose(impl->handle);
        impl->handle = nullptr;
    }
}

bool Output::send(const Message& m)
{
    if (!impl->handle)
        return false;
    DWORD packed = (DWORD)m.status | ((DWORD)m.data1 << 8) |
                   ((DWORD)m.data2 << 16);
    return midiOutShortMsg(impl->handle, packed) == MMSYSERR_NOERROR;
}

} // namespace snd::midi

#else // stubs on any other platform

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
