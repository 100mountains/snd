// SND Demo Synth -- the reference snd::plugin::client INSTRUMENT: 8-voice
// sine synth driven by host MIDI and/or its own on-screen keyboard. Proves
// the whole M2 chain: keyboard widget -> note queue -> voices -> sound,
// shipped as VST3 + standalone by snd_add_plugin().

#include "snd/plugin_client.h"
#include "snd/plugin_client_ui.h"

#include <cmath>
#include <cstring>
#include <memory>
#include <mutex>

using namespace snd::plugin::client;

namespace {
constexpr uint32_t kGain = 1;
constexpr uint32_t kDecay = 2;
} // namespace

const PluginSpec& sndPluginSpec()
{
    static const PluginSpec spec = [] {
        PluginSpec s;
        s.name = "SND Demo Synth";
        s.vendor = "SND";
        s.version = "1.0.0";
        s.subCategories = "Instrument|Synth";
        s.uid[0] = 0x5EDD5717;
        s.uid[1] = 0x1B2C3D4E;
        s.uid[2] = 0x5F60718A;
        s.uid[3] = 0x93A5B7C1;
        s.isInstrument = true;
        s.wantsMidi = true;
        s.hasUi = true;
        s.uiWidth = 560;
        s.uiHeight = 260;
        s.params = {{kGain, "gain", 0.7, true}, {kDecay, "decay", 0.4, true}};
        return s;
    }();
    return spec;
}

namespace {

class DemoSynth final : public Processor {
public:
    void prepare(double sampleRate, uint32_t) override
    {
        sr_ = sampleRate > 0 ? sampleRate : 48000.0;
        for (auto& v : voices_)
            v = {};
        std::memset(held_, 0, sizeof(held_));
    }

    void reset() override { prepare(sr_, 0); }

    void process(const float* const*, float* const* out, uint32_t frames,
                 const snd::midi::Buffer& midiIn, snd::midi::Buffer&) override
    {
        // events from the host + events from our own keyboard UI
        for (auto& m : midiIn)
            handle(m);
        if (uiLock_.try_lock()) { // never block the audio thread on the UI
            for (auto& m : uiQueue_)
                handle(m);
            uiQueue_.clear();
            uiLock_.unlock();
        }

        const float gain = (float)param(kGain);
        // decay knob 0..1 -> 60dB decay time ~40ms..4s
        const double t60 = 0.04 * std::pow(100.0, param(kDecay));
        const float decay = (float)std::exp(-6.9078 / (t60 * sr_));

        for (uint32_t f = 0; f < frames; ++f) {
            double s = 0.0;
            for (auto& v : voices_) {
                if (!v.active)
                    continue;
                s += std::sin(v.phase) * v.env;
                v.phase += 2.0 * M_PI * v.freq / sr_;
                v.env *= v.releasing ? decay * 0.985f : decay;
                if (v.env < 1e-4f)
                    v.active = false;
            }
            float o = (float)(s * 0.25) * gain;
            out[0][f] = o;
            out[1][f] = o;
        }
    }

    void drawUi(UiHost& ui) override
    {
        snd::ui::sectionHeader("snd demo synth");
        uiKnob(ui, "gain", kGain);
        ImGui::SameLine();
        uiKnob(ui, "decay", kDecay);
        snd::ui::sectionHeader("keyboard");
        snd::ui::keyboard(
            "kbd", kbd_, ImVec2(ImGui::GetContentRegionAvail().x, 110), 48, 3,
            [this](uint8_t note, uint8_t vel) {
                std::lock_guard<std::mutex> g(uiLock_);
                uiQueue_.push_back(snd::midi::Message::noteOn(0, note, vel));
            },
            [this](uint8_t note) {
                std::lock_guard<std::mutex> g(uiLock_);
                uiQueue_.push_back(snd::midi::Message::noteOff(0, note));
            },
            held_);
    }

private:
    struct Voice {
        double phase = 0.0, freq = 0.0;
        float env = 0.0f;
        uint8_t note = 0;
        bool active = false, releasing = false;
    };

    void handle(const snd::midi::Message& m)
    {
        if (m.isNoteOn()) {
            Voice* v = freeVoice();
            v->note = m.data1;
            v->freq = 440.0 * std::pow(2.0, (m.data1 - 69) / 12.0);
            v->phase = 0.0;
            v->env = (float)m.data2 / 127.0f;
            v->active = true;
            v->releasing = false;
            if (m.data1 < 128)
                held_[m.data1] = true;
        } else if (m.isNoteOff()) {
            for (auto& v : voices_)
                if (v.active && v.note == m.data1)
                    v.releasing = true;
            if (m.data1 < 128)
                held_[m.data1] = false;
        }
    }

    Voice* freeVoice()
    {
        for (auto& v : voices_)
            if (!v.active)
                return &v;
        Voice* steal = &voices_[0]; // quietest loses
        for (auto& v : voices_)
            if (v.env < steal->env)
                steal = &v;
        return steal;
    }

    double sr_ = 48000.0;
    Voice voices_[8];
    bool held_[128] = {};

    std::mutex uiLock_;
    snd::midi::Buffer uiQueue_;
    snd::ui::KeyboardState kbd_;
};

} // namespace

std::unique_ptr<Processor> sndCreateProcessor()
{
    return std::make_unique<DemoSynth>();
}
