// snd-example -- consumes only snd:: APIs. Two jobs:
//   default:     open a window + audio device, prove the stack runs
//   --selftest:  headless pass/fail checks of the real behaviour of every
//                snd capability (decode+play, record, VST3 hosting, AU hosting)

#include "snd/audio.h"
#include "snd/plugin_host.h"
#include "snd/ui.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#ifndef SND_TEST_ASSETS_DIR
#define SND_TEST_ASSETS_DIR "."
#endif
#ifndef SND_TEST_VST3_PATH
#define SND_TEST_VST3_PATH ""
#endif

// ---------------------------------------------------------------------------
// selftest helpers
// ---------------------------------------------------------------------------

static double rms(const std::vector<float>& v)
{
    if (v.empty())
        return 0.0;
    double acc = 0;
    for (float s : v)
        acc += (double)s * s;
    return std::sqrt(acc / v.size());
}

static std::vector<float> makeSine(double freq, double sampleRate, uint32_t frames, float amp)
{
    std::vector<float> out(frames);
    for (uint32_t i = 0; i < frames; ++i)
        out[i] = amp * (float)std::sin(2.0 * M_PI * freq * i / sampleRate);
    return out;
}

// Run a full buffer through a plugin instance in blocks. Returns output.
static bool processThrough(snd::plugin::Instance& plugin, const std::vector<float>& mono,
                           double sampleRate, std::vector<float>& outMono)
{
    const uint32_t block = 512;
    if (!plugin.prepare(sampleRate, block))
        return false;

    outMono.assign(mono.size(), 0.0f);
    std::vector<float> inL(block), inR(block), outL(block), outR(block);

    for (size_t pos = 0; pos < mono.size(); pos += block) {
        uint32_t n = (uint32_t)std::min<size_t>(block, mono.size() - pos);
        std::memcpy(inL.data(), mono.data() + pos, n * sizeof(float));
        std::memcpy(inR.data(), mono.data() + pos, n * sizeof(float));

        const float* ins[2] = {inL.data(), inR.data()};
        float* outs[2] = {outL.data(), outR.data()};
        if (!plugin.process(ins, 2, outs, 2, n)) {
            plugin.unprepare();
            return false;
        }
        std::memcpy(outMono.data() + pos, outL.data(), n * sizeof(float));
    }
    plugin.unprepare();
    return true;
}

// ---------------------------------------------------------------------------
// selftest checks
// ---------------------------------------------------------------------------

static bool selftestDecodeAndPlay()
{
    snd::audio::Buffer buf;
    std::string err;
    std::string path = std::string(SND_TEST_ASSETS_DIR) + "/tone.wav";
    if (!snd::audio::load(path, buf, &err)) {
        printf("FAIL (%s)\n", err.c_str());
        return false;
    }

    snd::audio::Player player;
    if (!player.open(buf.sampleRate, 2)) {
        printf("FAIL (could not open playback device)\n");
        return false;
    }
    player.setBuffer(&buf);
    player.play();

    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(2500);
    while (player.isPlaying() && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(20));

    uint64_t played = player.positionFrames();
    player.close();

    if (played >= buf.frames()) {
        printf("PASS (%llu frames decoded and played)\n", (unsigned long long)played);
        return true;
    }
    printf("FAIL (playback stalled at frame %llu of %llu)\n",
           (unsigned long long)played, (unsigned long long)buf.frames());
    return false;
}

static bool selftestRecord()
{
    std::atomic<uint64_t> captured{0};
    snd::audio::CaptureDevice cap;
    if (!cap.open(48000, 1, [&](const float*, uint32_t frames, uint32_t) {
            captured.fetch_add(frames, std::memory_order_relaxed);
        })) {
        printf("FAIL (could not open capture device)\n");
        return false;
    }
    if (!cap.start()) {
        printf("FAIL (could not start capture device)\n");
        return false;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    cap.close();

    if (captured.load() > 0) {
        printf("PASS (%llu frames captured)\n", (unsigned long long)captured.load());
        return true;
    }
    printf("FAIL (no frames captured)\n");
    return false;
}

static bool selftestVST3()
{
    snd::plugin::HostManager manager;
    manager.addDefaultFormats();

    // Scan exactly our own TestGain bundle -- nothing machine-dependent.
    snd::plugin::Format* vst3 = nullptr;
    for (auto& f : manager.formats())
        if (std::string(f->name()) == "VST3")
            vst3 = f.get();
    if (!vst3) {
        printf("FAIL (VST3 format missing)\n");
        return false;
    }

    auto found = vst3->scan(SND_TEST_VST3_PATH);
    if (found.empty()) {
        printf("FAIL (could not scan TestGain at %s)\n", SND_TEST_VST3_PATH);
        return false;
    }

    auto plugin = manager.create(found[0]);
    if (!plugin) {
        printf("FAIL (could not instantiate %s)\n", found[0].name.c_str());
        return false;
    }

    auto* gain = plugin->parameterById("100");
    if (!gain) {
        printf("FAIL (gain parameter not found by stable ID)\n");
        return false;
    }

    const double sr = 48000.0;
    auto sine = makeSine(440.0, sr, 48000, 0.5f);
    double inRms = rms(sine);

    // 1) audio actually passes through and is changed by the parameter
    gain->setValue(0.25);
    std::vector<float> processed;
    if (!processThrough(*plugin, sine, sr, processed)) {
        printf("FAIL (processing failed)\n");
        return false;
    }
    double ratio = rms(processed) / inRms;
    if (std::abs(ratio - 0.25) > 0.02) {
        printf("FAIL (gain 0.25 gave RMS ratio %.3f, expected ~0.25)\n", ratio);
        return false;
    }

    // 2) state save/restore round-trips through the stable parameter ID
    std::vector<uint8_t> state;
    if (!plugin->saveState(state) || state.empty()) {
        printf("FAIL (saveState)\n");
        return false;
    }
    gain->setValue(1.0);
    if (!plugin->loadState(state.data(), state.size())) {
        printf("FAIL (loadState)\n");
        return false;
    }
    if (std::abs(plugin->parameterById("100")->value() - 0.25) > 0.001) {
        printf("FAIL (state restore did not bring gain back to 0.25)\n");
        return false;
    }

    std::vector<float> processed2;
    if (!processThrough(*plugin, sine, sr, processed2)) {
        printf("FAIL (processing after restore failed)\n");
        return false;
    }
    double ratio2 = rms(processed2) / inRms;
    if (std::abs(ratio2 - 0.25) > 0.02) {
        printf("FAIL (restored gain gave RMS ratio %.3f, expected ~0.25)\n", ratio2);
        return false;
    }

    printf("PASS (%s: processed, gain param + state verified by stable ID)\n",
           found[0].name.c_str());
    return true;
}

#if defined(__APPLE__)
static bool selftestAU()
{
    snd::plugin::HostManager manager;
    manager.addDefaultFormats();

    snd::plugin::Format* au = nullptr;
    for (auto& f : manager.formats())
        if (std::string(f->name()) == "AU")
            au = f.get();
    if (!au) {
        printf("FAIL (AU format missing)\n");
        return false;
    }

    // Apple's built-in lowpass ships with macOS: type aufx, subtype lpas, manu appl
    snd::plugin::Description desc;
    desc.format = "AU";
    desc.identifier = "aufx,lpas,appl";
    desc.name = "AULowpass";

    auto plugin = manager.create(desc);
    if (!plugin) {
        printf("FAIL (could not instantiate Apple AULowpass)\n");
        return false;
    }

    // Cutoff is the first parameter (id 0). Set it near the bottom of its
    // range, then run an 8kHz sine through: it should be heavily attenuated.
    auto* cutoff = plugin->parameterById("0");
    if (!cutoff) {
        printf("FAIL (cutoff parameter not found)\n");
        return false;
    }
    cutoff->setValue(0.05);

    const double sr = 48000.0;
    auto sine = makeSine(8000.0, sr, 48000, 0.5f);
    std::vector<float> processed;
    if (!processThrough(*plugin, sine, sr, processed)) {
        printf("FAIL (AU processing failed)\n");
        return false;
    }

    double ratio = rms(processed) / rms(sine);
    if (ratio > 0.5) {
        printf("FAIL (lowpass barely attenuated: RMS ratio %.3f)\n", ratio);
        return false;
    }

    printf("PASS (AULowpass hosted; 8kHz attenuated to %.1f%% RMS)\n", ratio * 100.0);
    return true;
}
#endif

static int runSelftest()
{
    printf("=== SND self-test ===\n");

    printf("[1/4] decode + playback: ");
    fflush(stdout);
    bool ok1 = selftestDecodeAndPlay();

    printf("[2/4] capture/record:    ");
    fflush(stdout);
    bool ok2 = selftestRecord();

    printf("[3/4] VST3 hosting:      ");
    fflush(stdout);
    bool ok3 = selftestVST3();

    printf("[4/4] AU hosting:        ");
    fflush(stdout);
#if defined(__APPLE__)
    bool ok4 = selftestAU();
#else
    bool ok4 = true;
    printf("skipped (AU is macOS-only)\n");
#endif

    bool all = ok1 && ok2 && ok3 && ok4;
    printf("=== %s ===\n", all ? "ALL PASS" : "FAILED");
    return all ? 0 : 1;
}

// ---------------------------------------------------------------------------
// default: window + silent device
// ---------------------------------------------------------------------------

int main(int argc, char** argv)
{
    for (int i = 1; i < argc; ++i)
        if (std::string(argv[i]) == "--selftest")
            return runSelftest();

    snd::audio::Device device;
    if (!device.open(48000, 2)) {
        fprintf(stderr, "Failed to open audio device\n");
        return 1;
    }
    device.start();

    snd::ui::Window window;
    if (!window.create(800, 600, "SND")) {
        fprintf(stderr, "Failed to create window\n");
        return 1;
    }

    while (!window.shouldClose()) {
        if (!window.beginFrame())
            break;

        ImGui::Begin("SND");
        ImGui::Text("snd::audio device: %s", device.name().c_str());
        ImGui::Text("This example consumes only snd:: APIs.");
        snd::ui::gradientPanel(ImVec2(260, 40),
                               IM_COL32(90, 60, 160, 255), IM_COL32(60, 120, 200, 255),
                               IM_COL32(30, 30, 60, 255), IM_COL32(60, 30, 90, 255));
        if (snd::ui::gradientButton("gradient button", ImVec2(260, 32),
                                    IM_COL32(70, 70, 90, 255), IM_COL32(35, 35, 50, 255)))
            printf("clicked\n");
        ImGui::End();

        window.endFrame();
    }

    device.close();
    return 0;
}
