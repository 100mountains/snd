// SND Demo Filter -- the reference snd::plugin::client plugin: a one-pole
// lowpass + output gain with an ImGui editor. Ships as VST3 (and standalone)
// via snd_add_plugin(); doubles as the SDK's selftest subject.

#include "snd/plugin_client.h"
#include "snd/plugin_client_ui.h"

#include <cmath>
#include <memory>

using namespace snd::plugin::client;

namespace {
constexpr uint32_t kCutoff = 1;
constexpr uint32_t kGain = 2;
} // namespace

const PluginSpec& sndPluginSpec()
{
    static const PluginSpec spec = [] {
        PluginSpec s;
        s.name = "SND Demo Filter";
        s.vendor = "SND";
        s.version = "1.0.0";
        s.subCategories = "Fx|Filter";
        s.uid[0] = 0x5EDDF117;
        s.uid[1] = 0x0A1B2C3D;
        s.uid[2] = 0x4E5F6072;
        s.uid[3] = 0x8293A5B7;
        s.hasUi = true;
        s.uiWidth = 340;
        s.uiHeight = 200;
        s.params = {{kCutoff, "cutoff", 1.0, true}, {kGain, "gain", 1.0, true}};
        return s;
    }();
    return spec;
}

namespace {

class DemoFilter final : public Processor {
public:
    void prepare(double sampleRate, uint32_t) override
    {
        sr_ = sampleRate > 0 ? sampleRate : 48000.0;
        z_[0] = z_[1] = 0.0;
    }

    void reset() override { z_[0] = z_[1] = 0.0; }

    void process(const float* const* in, float* const* out, uint32_t frames,
                 const snd::midi::Buffer&, snd::midi::Buffer&) override
    {
        // 40 Hz .. 20 kHz on a log sweep
        const double cut = param(kCutoff);
        const double gain = param(kGain);
        const double hz = 40.0 * std::pow(500.0, cut);
        const double a = 1.0 - std::exp(-2.0 * M_PI * hz / sr_);

        for (int c = 0; c < 2; ++c) {
            const float* i = in ? in[c] : nullptr;
            float* o = out[c];
            double z = z_[c];
            for (uint32_t f = 0; f < frames; ++f) {
                const double x = i ? i[f] : 0.0;
                z += a * (x - z);
                o[f] = (float)(z * gain);
            }
            z_[c] = z;
        }
    }

    void drawUi(UiHost& ui) override
    {
        snd::ui::sectionHeader("snd demo filter");
        uiKnob(ui, "cutoff", kCutoff);
        ImGui::SameLine();
        uiKnob(ui, "gain", kGain);
        snd::ui::badge("VST3");
        ImGui::SameLine();
        ImGui::TextDisabled("one pole, 40 Hz - 20 kHz");
    }

private:
    double sr_ = 48000.0;
    double z_[2] = {0.0, 0.0};
};

} // namespace

std::unique_ptr<Processor> sndCreateProcessor()
{
    return std::make_unique<DemoFilter>();
}
