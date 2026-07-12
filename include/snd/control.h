// snd::control -- the inter-plugin control bus.
//
// A second wire alongside audio and MIDI: a plugin can broadcast stable
// parameter values and pitched "control voices" to OTHER plugins in the same
// in-process graph. This is how a modular rig's nodes talk without audio --
// e.g. a Scale node broadcasting the musical key to every synth, a Performer
// driving voices, a controller retuning targets. Modelled on the Bob rig's
// BobControlTarget (setParam + controlNoteOn/SetFreq/NoteOff/AllOff).
//
// Scope: this is an IN-PROCESS-GRAPH concept. When a plugin is hosted by a
// third-party DAW there is no control bus, so the control in/out buffers are
// simply empty -- which is correct. Routing lives in the graph, not the
// VST3/AU wrappers.
//
// Emitted from the audio thread. ParamSet carries an application-defined
// stable numeric target id; display names stay outside the realtime event.
#pragma once

#include "snd/event_buffer.h"

#include <cstdint>

namespace snd::control {

// One control event with a sample-frame offset into the current audio block.
struct Event {
    enum class Type : uint8_t {
        ParamSet, // set a stable parameter target on the target(s)
        NoteOn,   // start a pitched control voice (id, hz, velocity)
        SetFreq,  // retune an existing control voice (id, hz) -- no re-trigger
        NoteOff,  // stop a control voice (id)
        AllOff    // stop every control voice
    };

    Type type = Type::ParamSet;
    uint32_t frame = 0;
    int id = 0;         // control-voice id (NoteOn / SetFreq / NoteOff)
    float value = 0.0f; // ParamSet value, or NoteOn velocity
    float hz = 0.0f;    // NoteOn / SetFreq frequency
    uint32_t target = 0; // stable application-defined ParamSet target id

    static Event paramSet(uint32_t target, float value, uint32_t frame = 0)
    {
        Event e;
        e.type = Type::ParamSet;
        e.target = target;
        e.value = value;
        e.frame = frame;
        return e;
    }
    static Event noteOn(int id, float hz, float vel, uint32_t frame = 0)
    {
        Event e;
        e.type = Type::NoteOn;
        e.id = id;
        e.hz = hz;
        e.value = vel;
        e.frame = frame;
        return e;
    }
    static Event setFreq(int id, float hz, uint32_t frame = 0)
    {
        Event e;
        e.type = Type::SetFreq;
        e.id = id;
        e.hz = hz;
        e.frame = frame;
        return e;
    }
    static Event noteOff(int id, uint32_t frame = 0)
    {
        Event e;
        e.type = Type::NoteOff;
        e.id = id;
        e.frame = frame;
        return e;
    }
    static Event allOff(uint32_t frame = 0)
    {
        Event e;
        e.type = Type::AllOff;
        e.frame = frame;
        return e;
    }
};

// A block's worth of events, ordered by frame. Stable numeric target ids keep
// events compact and allocation-free.
inline constexpr std::size_t kMaxBlockEvents = 1024;
using Buffer = snd::EventBuffer<Event, kMaxBlockEvents>;

} // namespace snd::control
