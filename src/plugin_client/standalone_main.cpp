// Standalone shell for snd::plugin::client -- the same Processor that ships
// as a VST3 runs as its own app: SND window + ImGui editor + audio device.
// Effects process LIVE INPUT (default capture device -> ring -> processor);
// instruments run on their own (MIDI devices come later). Compiled into the
// optional <target>-app executable by snd_add_plugin(... STANDALONE).

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

// capture thread -> playback thread, interleaved stereo, lock-free SPSC
class InputRing {
public:
    void init(size_t frames)
    {
        buf_.assign(frames * 2, 0.0f);
        frames_ = frames;
    }

    void push(const float* in, uint32_t frames, uint32_t channels)
    {
        size_t w = write_.load(std::memory_order_relaxed);
        for (uint32_t f = 0; f < frames; ++f) {
            size_t slot = (w + f) % frames_;
            buf_[slot * 2] = in[f * channels];
            buf_[slot * 2 + 1] = channels > 1 ? in[f * channels + 1] : in[f * channels];
        }
        write_.store(w + frames, std::memory_order_release);
    }

    void pop(float* L, float* R, uint32_t frames)
    {
        size_t w = write_.load(std::memory_order_acquire);
        size_t r = read_.load(std::memory_order_relaxed);
        size_t avail = w - r;
        if (avail > frames_) { // overrun: jump close to live
            r = w - frames_ / 2;
            avail = frames_ / 2;
        }
        for (uint32_t f = 0; f < frames; ++f) {
            if (f < avail) {
                size_t slot = (r + f) % frames_;
                L[f] = buf_[slot * 2];
                R[f] = buf_[slot * 2 + 1];
            } else {
                L[f] = R[f] = 0.0f; // underrun: silence
            }
        }
        read_.store(r + std::min<size_t>(frames, avail), std::memory_order_relaxed);
    }

private:
    std::vector<float> buf_;
    size_t frames_ = 0;
    std::atomic<size_t> write_{0}, read_{0};
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
    static snd::midi::Buffer midiOutScratch;
    std::vector<float> inL(4096, 0.0f), inR(4096, 0.0f), outL(4096), outR(4096);

    // effects eat the default input device live; ring decouples the clocks
    InputRing ring;
    ring.init(48000 / 4); // 250 ms of headroom
    snd::audio::CaptureDevice capture;
    bool haveInput = false;
    if (!spec.isInstrument)
        haveInput = capture.open(48000, 2,
                                 [&](const float* in, uint32_t frames,
                                     uint32_t channels) {
                                     ring.push(in, frames, channels);
                                 }) &&
                    capture.start();

    snd::audio::Device device;
    Processor* p = proc.get();
    device.open(48000, 2, [&, p](float* out, uint32_t frames, uint32_t channels) {
        uint32_t done = 0;
        while (done < frames) {
            uint32_t n = std::min<uint32_t>(4096, frames - done);
            if (haveInput)
                ring.pop(inL.data(), inR.data(), n);
            const float* ins[2] = {inL.data(), inR.data()};
            float* outs[2] = {outL.data(), outR.data()};
            midiOutScratch.clear();
            p->process(spec.isInstrument ? nullptr : ins, outs, n, noMidi,
                       midiOutScratch);
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

    capture.close();
    device.close();
    return 0;
}
