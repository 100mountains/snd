// Standalone shell for snd::plugin::client -- the same Processor that ships
// as a VST3 runs as its own app: SND window + ImGui editor + audio device.
// Effects run on silence (v1 -- live input routing comes later); instruments
// are driven by MIDI input devices later too. Compiled into the optional
// <target>-app executable by snd_add_plugin(... STANDALONE).

#include "snd/audio.h"
#include "snd/plugin_client.h"
#include "snd/ui.h"

#include <algorithm>
#include <atomic>
#include <memory>
#include <vector>

const snd::plugin::client::PluginSpec& sndPluginSpec();
std::unique_ptr<snd::plugin::client::Processor> sndCreateProcessor();

namespace {

using namespace snd::plugin::client;

class LocalHost final : public UiHost {
public:
    explicit LocalHost(const PluginSpec& spec)
    {
        values_.reset(new std::atomic<double>[spec.params.size()]);
        for (size_t i = 0; i < spec.params.size(); ++i) {
            values_[i].store(spec.params[i].defaultValue);
            ids_.push_back(spec.params[i].id);
        }
    }

    double get(uint32_t id) const override
    {
        for (size_t i = 0; i < ids_.size(); ++i)
            if (ids_[i] == id)
                return values_[i].load(std::memory_order_relaxed);
        return 0.0;
    }

    void set(uint32_t id, double v) override
    {
        for (size_t i = 0; i < ids_.size(); ++i)
            if (ids_[i] == id)
                values_[i].store(std::clamp(v, 0.0, 1.0),
                                 std::memory_order_relaxed);
    }

    const std::atomic<double>* values() const { return values_.get(); }
    const uint32_t* ids() const { return ids_.data(); }
    size_t count() const { return ids_.size(); }

private:
    std::unique_ptr<std::atomic<double>[]> values_;
    std::vector<uint32_t> ids_;
};

} // namespace

int main()
{
    auto& spec = sndPluginSpec();
    auto proc = sndCreateProcessor();
    LocalHost host(spec);
    proc->_bindParams(host.values(), host.ids(), host.count());
    proc->prepare(48000.0, 512);

    static snd::midi::Buffer noMidi;
    std::vector<float> inL(4096, 0.0f), inR(4096, 0.0f), outL(4096), outR(4096);

    snd::audio::Device device;
    Processor* p = proc.get();
    device.open(48000, 2, [&, p](float* out, uint32_t frames, uint32_t channels) {
        uint32_t done = 0;
        while (done < frames) {
            uint32_t n = std::min<uint32_t>(4096, frames - done);
            const float* ins[2] = {inL.data(), inR.data()};
            float* outs[2] = {outL.data(), outR.data()};
            p->process(spec.isInstrument ? nullptr : ins, outs, n, noMidi);
            for (uint32_t f = 0; f < n; ++f) {
                out[(done + f) * channels] = outL[f];
                if (channels > 1)
                    out[(done + f) * channels + 1] = outR[f];
            }
            done += n;
        }
    });
    device.start();

    snd::ui::Window window;
    if (!window.create(spec.uiWidth + 24, spec.uiHeight + 24, spec.name))
        return 1;

    while (!window.shouldClose()) {
        if (!window.beginFrame())
            break;
        ImGuiViewport* vp = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(vp->WorkPos);
        ImGui::SetNextWindowSize(vp->WorkSize);
        ImGui::Begin("##standalone", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                         ImGuiWindowFlags_NoSavedSettings);
        proc->drawUi(host);
        ImGui::End();
        window.endFrame();
    }

    device.close();
    return 0;
}
