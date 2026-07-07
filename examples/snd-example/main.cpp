// snd-example -- consumes only snd:: APIs. Two jobs:
//   default:     open a window + audio device, prove the stack runs
//   --selftest:  headless pass/fail checks of the real behaviour of every
//                snd capability (decode+play, record, VST3 hosting, AU hosting)

#include "snd/audio.h"
#include "snd/dsp.h"
#include "snd/platform.h"
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

    // 3) editor API degrades gracefully on a GUI-less plugin
    if (plugin->hasEditor() || plugin->openEditor("x") || plugin->editorOpen()) {
        printf("FAIL (TestGain has no GUI but the editor API says otherwise)\n");
        return false;
    }
    plugin->closeEditor(); // no-op, must not crash

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

    // Every AU must report an editor (generic parameter view at minimum).
    if (!plugin->hasEditor()) {
        printf("FAIL (AU reports no editor; generic view should always exist)\n");
        return false;
    }

    printf("PASS (AULowpass hosted; 8kHz attenuated to %.1f%% RMS)\n", ratio * 100.0);
    return true;
}
#endif

static bool selftestResample()
{
    snd::audio::Buffer in;
    in.channels = 2;
    in.sampleRate = 48000;
    auto mono = makeSine(1000.0, 48000, 48000, 0.5f);
    for (float s : mono) {
        in.samples.push_back(s);
        in.samples.push_back(s);
    }

    snd::audio::Buffer out;
    std::string err;
    if (!snd::audio::resample(in, 44100, out, &err)) {
        printf("FAIL (%s)\n", err.c_str());
        return false;
    }

    double expected = 48000.0 * 44100.0 / 48000.0;
    double got = (double)out.frames();
    if (std::abs(got - expected) > 64) {
        printf("FAIL (expected ~%.0f frames at 44100, got %.0f)\n", expected, got);
        return false;
    }
    // Content sanity: a sine stays a sine of similar level.
    std::vector<float> outMono(out.frames());
    for (uint64_t f = 0; f < out.frames(); ++f)
        outMono[f] = out.samples[f * out.channels];
    double ratio = rms(outMono) / rms(mono);
    if (ratio < 0.9 || ratio > 1.1) {
        printf("FAIL (RMS ratio after resample %.3f)\n", ratio);
        return false;
    }
    printf("PASS (48k -> 44.1k, %llu frames, RMS ratio %.3f)\n",
           (unsigned long long)out.frames(), ratio);
    return true;
}

static bool selftestStft()
{
    // Round-trip transparency: analyze + resynthesize a sine, compare.
    const double sr = 48000.0;
    auto sine = makeSine(440.0, sr, 48000, 0.5f);

    auto data = snd::dsp::stftAnalyze(sine.data(), sine.size());
    auto back = snd::dsp::stftResynthesize(data);
    if (back.size() != sine.size()) {
        printf("FAIL (length changed: %zu -> %zu)\n", sine.size(), back.size());
        return false;
    }

    // Error RMS relative to signal RMS, in dB. Skip the outermost edge
    // samples where the first/last window taper.
    const size_t guard = 4096;
    double errAcc = 0, sigAcc = 0;
    size_t n = 0;
    for (size_t i = guard; i + guard < sine.size(); ++i) {
        double e = (double)back[i] - sine[i];
        errAcc += e * e;
        sigAcc += (double)sine[i] * sine[i];
        ++n;
    }
    double errDb = 10.0 * std::log10((errAcc / n) / (sigAcc / n));
    if (errDb > -60.0) {
        printf("FAIL (round-trip error %.1f dB, need < -60)\n", errDb);
        return false;
    }
    printf("PASS (STFT round-trip error %.1f dB)\n", errDb);
    return true;
}

static bool selftestLooping()
{
    // 0.25s tone, looped: after 0.8s it must still be playing inside the range.
    snd::audio::Buffer buf;
    buf.channels = 2;
    buf.sampleRate = 48000;
    auto mono = makeSine(440.0, 48000, 12000, 0.25f);
    for (float s : mono) {
        buf.samples.push_back(s);
        buf.samples.push_back(s);
    }

    snd::audio::Player player;
    if (!player.open(48000, 2)) {
        printf("FAIL (could not open playback device)\n");
        return false;
    }
    player.setBuffer(&buf);
    player.setLooping(true);
    player.playRange(0, buf.frames());

    std::this_thread::sleep_for(std::chrono::milliseconds(800));
    bool stillPlaying = player.isPlaying();
    uint64_t pos = player.positionFrames();
    player.stop();
    bool stopped = !player.isPlaying();
    player.close();

    if (stillPlaying && pos <= buf.frames() && stopped) {
        printf("PASS (looped past buffer length, stop() sticks)\n");
        return true;
    }
    printf("FAIL (playing %d, pos %llu, stopped %d)\n", stillPlaying,
           (unsigned long long)pos, stopped);
    return false;
}

static int runSelftest()
{
    printf("=== SND self-test ===\n");

    printf("[1/9] decode + playback: ");
    fflush(stdout);
    bool ok1 = selftestDecodeAndPlay();

    printf("[2/9] capture/record:    ");
    fflush(stdout);
    bool ok2 = selftestRecord();

    printf("[3/9] VST3 hosting:      ");
    fflush(stdout);
    bool ok3 = selftestVST3();

    printf("[4/9] AU hosting:        ");
    fflush(stdout);
#if defined(__APPLE__)
    bool ok4 = selftestAU();
#else
    bool ok4 = true;
    printf("skipped (AU is macOS-only)\n");
#endif

    printf("[5/9] resample:          ");
    fflush(stdout);
    bool ok5 = selftestResample();

    printf("[6/9] STFT round-trip:   ");
    fflush(stdout);
    bool ok6 = selftestStft();

    printf("[7/9] player looping:    ");
    fflush(stdout);
    bool ok7 = selftestLooping();

    printf("[8/9] insert hook:       ");
    fflush(stdout);
    bool ok8;
    {
        // hook halves the output; post-insert peak should read ~half
        snd::audio::Buffer buf;
        buf.channels = 2;
        buf.sampleRate = 48000;
        auto mono = makeSine(440.0, 48000, 48000, 0.8f);
        for (float s : mono) {
            buf.samples.push_back(s);
            buf.samples.push_back(s);
        }
        snd::audio::Player player;
        ok8 = player.open(48000, 2);
        if (ok8) {
            player.setInsert([](float* out, uint32_t frames, uint32_t channels) {
                for (uint32_t i = 0; i < frames * channels; ++i)
                    out[i] *= 0.5f;
            });
            player.setBuffer(&buf);
            player.play();
            std::this_thread::sleep_for(std::chrono::milliseconds(400));
            float pk = player.outputPeak(0);
            player.stop();
            player.close();
            ok8 = pk > 0.3f && pk < 0.5f;
            if (ok8)
                printf("PASS (post-insert peak %.2f of 0.8 raw)\n", pk);
            else
                printf("FAIL (post-insert peak %.2f)\n", pk);
        } else
            printf("FAIL (no playback device)\n");
    }

    printf("[9/9] OOP plugin scan:   ");
    fflush(stdout);
    bool ok9;
    {
        // this same executable is the worker (see main); a real child
        // process scans TestGain and reports back through the out-file
        auto exe = snd::platform::executablePath();
        auto found = snd::plugin::scanViaWorker(exe, "--snd-scan-plugin", "VST3",
                                                SND_TEST_VST3_PATH);
        // garbage never harms the parent: empty result, no crash
        auto junk = snd::plugin::scanViaWorker(exe, "--snd-scan-plugin", "VST3",
                                               "/nonexistent/junk.vst3");
        ok9 = !exe.empty() && found.size() == 1 && found[0].name == "SND TestGain" &&
              !found[0].identifier.empty() && junk.empty();
        if (ok9)
            printf("PASS (%s found via child process; junk path harmless)\n",
                   found[0].name.c_str());
        else
            printf("FAIL (exe='%s', found=%zu, junk=%zu)\n", exe.c_str(), found.size(),
                   junk.size());
    }

    bool all = ok1 && ok2 && ok3 && ok4 && ok5 && ok6 && ok7 && ok8 && ok9;
    printf("=== %s ===\n", all ? "ALL PASS" : "FAILED");
    return all ? 0 : 1;
}

// ---------------------------------------------------------------------------
// default: window + silent device
// ---------------------------------------------------------------------------

int main(int argc, char** argv)
{
    // scan-worker mode: enumerate one plugin and exit, before any GUI/audio
    // init -- a crashing plugin takes down only this child process
    if (argc == 5 && std::string(argv[1]) == "--snd-scan-plugin")
        return snd::plugin::runScanWorker(argv[2], argv[3], argv[4]);

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
