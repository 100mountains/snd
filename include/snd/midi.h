// snd::midi -- MIDI events and device I/O.
//
// Charter note: MIDI entered SND on 2026-07-07, owner-blessed, because the
// bob/Murk surface requires it (53 of 57 Murfy files touched MIDI). Apps that
// don't need it (WaveBob) simply never call it.
//
// Device callbacks run on the MIDI system's thread: treat them like audio
// callbacks -- no allocation, no blocking, hand off through a queue.
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace snd::midi {

// One short MIDI 1.0 message with a sample-frame offset into the current
// audio block (0 = start of block / "now" for device I/O).
struct Message {
    uint8_t status = 0; // status byte incl. channel
    uint8_t data1 = 0;
    uint8_t data2 = 0;
    uint32_t frame = 0;

    uint8_t type() const { return status & 0xF0; }
    uint8_t channel() const { return status & 0x0F; }
    bool isNoteOn() const { return type() == 0x90 && data2 > 0; }
    bool isNoteOff() const
    {
        return type() == 0x80 || (type() == 0x90 && data2 == 0);
    }
    bool isControl() const { return type() == 0xB0; }
    bool isPitchBend() const { return type() == 0xE0; }

    static Message noteOn(uint8_t ch, uint8_t note, uint8_t vel, uint32_t frame = 0)
    {
        return {(uint8_t)(0x90 | (ch & 0x0F)), note, vel, frame};
    }
    static Message noteOff(uint8_t ch, uint8_t note, uint32_t frame = 0)
    {
        return {(uint8_t)(0x80 | (ch & 0x0F)), note, 0, frame};
    }
    static Message control(uint8_t ch, uint8_t cc, uint8_t value, uint32_t frame = 0)
    {
        return {(uint8_t)(0xB0 | (ch & 0x0F)), cc, value, frame};
    }
    static Message pitchBend(uint8_t ch, uint16_t value14, uint32_t frame = 0)
    {
        return {(uint8_t)(0xE0 | (ch & 0x0F)), (uint8_t)(value14 & 0x7F),
                (uint8_t)((value14 >> 7) & 0x7F), frame};
    }
};

// A block's worth of events, ordered by frame.
using Buffer = std::vector<Message>;

// Endpoint names for settings UIs. Select by NAME, not index.
std::vector<std::string> inputDevices();
std::vector<std::string> outputDevices();

// MIDI input port. Callback runs on the MIDI thread.
class Input {
public:
    using Callback = std::function<void(const Message&)>;

    Input();
    ~Input();
    Input(const Input&) = delete;
    Input& operator=(const Input&) = delete;

    // name from inputDevices(). Empty name = create a VIRTUAL destination
    // (named virtualName) that other apps and our own Output can send to.
    bool open(const std::string& name, Callback cb,
              const std::string& virtualName = "SND In");
    void close();

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

// MIDI output port.
class Output {
public:
    Output();
    ~Output();
    Output(const Output&) = delete;
    Output& operator=(const Output&) = delete;

    // name from outputDevices(); virtual destinations match too.
    bool open(const std::string& name);
    void close();
    bool send(const Message& m);

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

} // namespace snd::midi
