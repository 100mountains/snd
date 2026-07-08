// snd-example -- consumes only snd:: APIs. Two jobs:
//   default:     open a window + audio device, prove the stack runs
//   --selftest:  headless pass/fail checks of the real behaviour of every
//                snd capability (decode+play, record, VST3 hosting, AU hosting)

#if defined(__APPLE__)
#include <AudioToolbox/AudioToolbox.h>
#include <dlfcn.h>
#endif

#include "snd/audio.h"
#include "snd/dsp.h"
#include "snd/midi.h"
#include "snd/platform.h"
#include "snd/plugin_graph.h"
#include "snd/plugin_host.h"
#include "snd/state.h"
#include "snd/ui.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
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

// Portable temp dir (Windows has no /tmp; TMPDIR is unset there).
static std::string tmpDir()
{
    std::error_code ec;
    auto p = std::filesystem::temp_directory_path(ec).string();
    if (ec || p.empty())
        p = getenv("TMPDIR") ? getenv("TMPDIR") : ".";
    while (!p.empty() && (p.back() == '/' || p.back() == '\\'))
        p.pop_back();
    return p;
}

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
        printf("PASS (%llu frames decoded; playback skipped -- no device)\n",
               (unsigned long long)buf.frames());
        return true;
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
        printf("skipped (no capture device)\n");
        return true;
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
        printf("skipped (no playback device)\n");
        return true;
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

    printf("[1/22] decode + playback: ");
    fflush(stdout);
    bool ok1 = selftestDecodeAndPlay();

    printf("[2/22] capture/record:    ");
    fflush(stdout);
    bool ok2 = selftestRecord();

    printf("[3/22] VST3 hosting:      ");
    fflush(stdout);
    bool ok3 = selftestVST3();

    printf("[4/22] AU hosting:        ");
    fflush(stdout);
#if defined(__APPLE__)
    bool ok4 = selftestAU();
#else
    bool ok4 = true;
    printf("skipped (AU is macOS-only)\n");
#endif

    printf("[5/22] resample:          ");
    fflush(stdout);
    bool ok5 = selftestResample();

    printf("[6/22] STFT round-trip:   ");
    fflush(stdout);
    bool ok6 = selftestStft();

    printf("[7/22] player looping:    ");
    fflush(stdout);
    bool ok7 = selftestLooping();

    printf("[8/22] insert hook:       ");
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
        } else {
            ok8 = true;
            printf("skipped (no playback device)\n");
        }
    }

    printf("[9/22] OOP plugin scan:   ");
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

    printf("[10/22] widget set:      ");
    fflush(stdout);
    bool ok10;
    {
        // one headless ImGui frame exercising every snd::ui widget: no GPU,
        // no window -- proves the widget layer stands on its own
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.DisplaySize = ImVec2(800, 600);
        unsigned char* pixels = nullptr;
        int tw = 0, th = 0;
        io.Fonts->GetTexDataAsRGBA32(&pixels, &tw, &th); // build atlas CPU-side

        snd::ui::Palette pal;
        snd::ui::setPalette(pal);

        float k = 0.5f, db = -6.0f, fad = 0.7f;
        bool sw = true;
        snd::ui::MeterState ms;

        // two frames: ImGui hides a fresh auto-sizing window on its first
        // frame while it measures, so the draw check needs frame two
        bool interacted = false;
        int drawLists = 0;
        for (int frame = 0; frame < 2; ++frame) {
            ImGui::NewFrame();
            ImGui::Begin("w");
            interacted = snd::ui::knob("k", &k) || interacted;
            interacted = snd::ui::knobDb("g", &db, -60.0f, 12.0f) || interacted;
            interacted = snd::ui::toggle("t", &sw) || interacted;
            interacted = snd::ui::led("l", true, 5.0f, true) || interacted;
            snd::ui::meter("m", ms, 0.5f, ImVec2(10, 80));
            interacted = snd::ui::fader("f", &fad, ImVec2(24, 100)) || interacted;
            snd::ui::badge("VST3");
            snd::ui::sectionHeader("murk-parity widgets");
        static bool cells[4 * 16] = {};
        static int step = 0;
        step = (step + 1) % (16 * 8);
        snd::ui::patternGrid("grid", cells, 4, 16, ImVec2(320, 72), step / 8);
        static std::vector<snd::ui::EnvPoint> env;
        snd::ui::envelopeEditor("env", env, ImVec2(200, 80));
        ImGui::SameLine();
        static float px = 0.5f, py = 0.5f;
        snd::ui::xyPad("xy", &px, &py, ImVec2(80, 80));
        static snd::ui::KeyboardState kbst;
        snd::ui::keyboard("kb", kbst, ImVec2(320, 60), 48, 2,
                          [](uint8_t, uint8_t) {}, [](uint8_t) {});
        static float dn = 0.3f;
        snd::ui::dragNumber("drag me", &dn, 0.01f, 0.0f, 1.0f);
        snd::ui::sectionHeader("levels");
            ImGui::End();
            ImGui::Render();
            drawLists = ImGui::GetDrawData()->CmdListsCount;
        }
        ImGui::DestroyContext();

        // no input was fed, so nothing may report interaction; values intact;
        // the meter must have integrated the level; something got drawn
        ok10 = !interacted && k == 0.5f && db == -6.0f && sw && fad == 0.7f &&
               ms.shown > 0.4f && drawLists > 0;
        if (ok10)
            printf("PASS (knob/toggle/led/meter/fader/badge drew headless)\n");
        else
            printf("FAIL (interacted=%d shown=%.2f lists=%d)\n", (int)interacted,
                   ms.shown, drawLists);
    }

    printf("[11/22] MIDI loopback:   ");
    fflush(stdout);
    bool ok11;
    {
        // real CoreMIDI round trip: our own virtual destination receives
        // what our own output port sends
        std::atomic<int> got{0};
        std::atomic<uint32_t> payload{0};
        snd::midi::Input in;
        ok11 = in.open(
            "",
            [&](const snd::midi::Message& m) {
                payload.store(((uint32_t)m.status << 16) | ((uint32_t)m.data1 << 8) |
                              m.data2);
                got.fetch_add(1);
            },
            "SND Selftest In");

        snd::midi::Output out;
        bool skipped = false;
        if (!ok11) {
            // No virtual destination available here (WinMM can't create one;
            // a headless box may have no MIDI hardware). Confirm the backend is
            // live by enumerating devices instead of a full loopback.
            auto ins = snd::midi::inputDevices();
            auto outs = snd::midi::outputDevices();
            if (ins.empty() && outs.empty())
                printf("skipped (no MIDI backend / no devices)\n");
            else
                printf("skipped loopback (no virtual port); backend sees "
                       "%zu in / %zu out\n",
                       ins.size(), outs.size());
            ok11 = true;
            skipped = true;
        } else {
            bool opened = false;
            for (int tries = 0; tries < 50 && !opened; ++tries) {
                opened = out.open("SND Selftest In");
                if (!opened)
                    std::this_thread::sleep_for(std::chrono::milliseconds(20));
            }
            ok11 = opened && out.send(snd::midi::Message::noteOn(0, 60, 100));
        }
        if (ok11 && !skipped) {
            auto deadline =
                std::chrono::steady_clock::now() + std::chrono::milliseconds(2000);
            while (got.load() == 0 && std::chrono::steady_clock::now() < deadline)
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            ok11 = got.load() > 0 &&
                   payload.load() == ((0x90u << 16) | (60u << 8) | 100u);
        }
        if (!skipped) {
            if (ok11)
                printf("PASS (note 60 vel 100 round-tripped through the OS MIDI system)\n");
            else
                printf("FAIL\n");
        }
    }

    printf("[12/22] AU instrument:   ");
    fflush(stdout);
#if defined(__APPLE__)
    bool ok12;
    {
        // MIDI drives sound: Apple's DLS synth must produce audio from a
        // note-on delivered through processMidi()
        snd::plugin::HostManager manager;
        manager.addDefaultFormats();
        snd::plugin::Description d;
        d.format = "AU";
        d.identifier = "aumu,dls ,appl";
        auto synth = manager.create(d);
        if (!synth) {
            d.identifier = "aumu,samp,appl"; // AUSampler fallback
            synth = manager.create(d);
        }
        ok12 = synth != nullptr && synth->prepare(44100.0, 512);
        if (ok12) {
            const uint32_t block = 512;
            std::vector<float> L(block), R(block);
            float* outs[2] = {L.data(), R.data()};
            float peak = 0.0f;
            snd::midi::Buffer noteOn = {snd::midi::Message::noteOn(0, 60, 127)};
            snd::midi::Buffer silence;
            for (int i = 0; i < 40 && ok12; ++i) { // ~0.5 s
                ok12 = synth->processMidi(nullptr, 0, outs, 2, block,
                                          i == 0 ? noteOn : silence);
                for (uint32_t f = 0; f < block; ++f)
                    peak = std::max(peak, std::fabs(L[f]));
            }
            ok12 = ok12 && peak > 0.01f;
            if (ok12)
                printf("PASS (note-on made the DLS synth sing, peak %.3f)\n", peak);
            else
                printf("FAIL (peak %.4f)\n", peak);
            synth->unprepare();
        } else
            printf("FAIL (no Apple instrument available)\n");
    }
#else
    bool ok12 = true;
    printf("skipped (AU is macOS-only)\n");
#endif

    printf("[13/22] client SDK:      ");
    fflush(stdout);
    bool ok13;
    {
        // DemoFilter was built by OUR plugin SDK; host it through the normal
        // hosting path and verify it filters, saves state, and offers a UI
        snd::plugin::HostManager manager;
        manager.addDefaultFormats();
        snd::plugin::Format* vst3 = nullptr;
        for (auto& f : manager.formats())
            if (std::string(f->name()) == "VST3")
                vst3 = f.get();
        auto found = vst3 ? vst3->scan(SND_TEST_DEMOFILTER_PATH)
                          : std::vector<snd::plugin::Description>{};
        auto plugin = found.empty() ? nullptr : manager.create(found[0]);
        ok13 = plugin != nullptr && found[0].name == "SND Demo Filter";

        double passRatio = 0, cutRatio = 0;
        if (ok13) {
            const double sr = 48000.0;
            auto sine = makeSine(8000.0, sr, 48000, 0.5f);
            double inRms = rms(sine);

            std::vector<float> outOpen, outClosed;
            plugin->parameterById("1")->setValue(1.0); // cutoff wide open
            ok13 = processThrough(*plugin, sine, sr, outOpen);
            plugin->parameterById("1")->setValue(0.0); // cutoff at 40 Hz
            ok13 = ok13 && processThrough(*plugin, sine, sr, outClosed);
            passRatio = rms(outOpen) / inRms;
            cutRatio = rms(outClosed) / inRms;
            ok13 = ok13 && passRatio > 0.5 && cutRatio < 0.1;

            // state round-trip through the SDK's chunk format (idle() flushes
            // the queued host-side edit into the plugin before saving)
            plugin->parameterById("1")->setValue(0.3);
            plugin->idle();
            std::vector<uint8_t> st;
            ok13 = ok13 && plugin->saveState(st) && !st.empty();
            plugin->parameterById("1")->setValue(0.9);
            plugin->idle();
            ok13 = ok13 && plugin->loadState(st.data(), st.size()) &&
                   std::abs(plugin->parameterById("1")->value() - 0.3) < 0.001;

#if defined(__APPLE__)
            ok13 = ok13 && plugin->hasEditor(); // ImGui editor advertised
#endif
        }
        if (ok13)
            printf("PASS (SDK plugin hosted: 8kHz pass %.2f / cut %.3f, state + "
                   "editor OK)\n",
                   passRatio, cutRatio);
        else
            printf("FAIL (found=%zu pass=%.2f cut=%.3f)\n", found.size(), passRatio,
                   cutRatio);
    }

    printf("[14/22] FLAC encode:     ");
    fflush(stdout);
    bool ok14;
    {
        // 6-channel tone -> FLAC -> decode (miniaudio) -> identical shape
        snd::audio::Buffer b;
        b.channels = 6;
        b.sampleRate = 48000;
        auto mono = makeSine(440.0, 48000, 24000, 0.5f);
        for (float s : mono)
            for (int c = 0; c < 6; ++c)
                b.samples.push_back(s * (0.2f + 0.1f * c));
        auto tmp = tmpDir() +
                   "/snd-selftest.flac";
        std::string err;
        snd::audio::Buffer back;
        ok14 = snd::audio::saveFlac(tmp, b, &err) && snd::audio::load(tmp, back, &err);
        double r1 = rms(b.samples), r2 = rms(back.samples);
        ok14 = ok14 && back.channels == 6 && back.sampleRate == 48000 &&
               back.frames() == b.frames() && std::abs(r1 - r2) < 0.001;
        remove(tmp.c_str());
        if (ok14)
            printf("PASS (6ch 24-bit round-trip, RMS %.4f -> %.4f)\n", r1, r2);
        else
            printf("FAIL (%s)\n", err.c_str());
    }

    printf("[15/22] MP3 encode:      ");
    fflush(stdout);
    bool ok15 = true;
    if (!snd::audio::mp3EncoderAvailable()) {
        printf("skipped (libmp3lame not installed -- brew install lame)\n");
    } else {
        snd::audio::Buffer b;
        b.channels = 2;
        b.sampleRate = 44100;
        auto mono = makeSine(440.0, 44100, 44100, 0.5f);
        for (float s : mono) {
            b.samples.push_back(s);
            b.samples.push_back(s);
        }
        auto tmp = tmpDir() +
                   "/snd-selftest.mp3";
        std::string err;
        snd::audio::Buffer back;
        ok15 = snd::audio::saveMp3(tmp, b, &err) && snd::audio::load(tmp, back, &err);
        double r1 = rms(b.samples), r2 = rms(back.samples);
        ok15 = ok15 && back.channels == 2 && std::abs(r1 - r2) / r1 < 0.15;
        remove(tmp.c_str());
        if (ok15)
            printf("PASS (440Hz survived lossy round-trip, RMS %.3f -> %.3f)\n", r1, r2);
        else
            printf("FAIL (%s)\n", err.c_str());
    }

    printf("[16/22] media extract:   ");
    fflush(stdout);
#if defined(__APPLE__) || defined(__linux__)
    bool ok16;
    {
        // wav -> AAC in an .m4a via the OS tool (afconvert on mac, ffmpeg on
        // Linux), then pull the audio back out through loadMediaAudio --
        // the same path video files take
        auto tmpw = tmpDir() +
                    "/snd-selftest-src.wav";
        auto tmpm = tmpDir() +
                    "/snd-selftest.m4a";
        snd::audio::Buffer b;
        b.channels = 2;
        b.sampleRate = 44100;
        auto mono = makeSine(440.0, 44100, 44100, 0.5f);
        for (float s : mono) {
            b.samples.push_back(s);
            b.samples.push_back(s);
        }
        std::string err;
        ok16 = snd::audio::saveWav(tmpw, b, &err);
#if defined(__APPLE__)
        std::string cmd = "afconvert -f m4af -d aac '" + tmpw + "' '" + tmpm + "' 2>/dev/null";
#else
        std::string cmd = "ffmpeg -v error -y -i '" + tmpw + "' -c:a aac '" + tmpm + "' 2>/dev/null";
#endif
        bool haveTool = ok16 && system(cmd.c_str()) == 0;
        if (!haveTool) {
            printf("skipped (no media conversion tool available)\n");
            remove(tmpw.c_str());
            ok16 = true;
            goto media_done;
        }
        ok16 = haveTool;
        snd::audio::Buffer media;
        ok16 = ok16 && snd::audio::loadMediaAudio(tmpm, media, &err);
        double r1 = rms(b.samples), r2 = rms(media.samples);
        ok16 = ok16 && media.channels == 2 && media.sampleRate == 44100 &&
               media.frames() > b.frames() / 2 && std::abs(r1 - r2) / r1 < 0.2;
        remove(tmpw.c_str());
        remove(tmpm.c_str());
        if (ok16)
            printf("PASS (AAC track extracted: %uch @ %u Hz, RMS %.3f -> %.3f)\n",
                   media.channels, media.sampleRate, r1, r2);
        else
            printf("FAIL (%s)\n", err.c_str());
    }
media_done:;
#elif defined(_WIN32)
    bool ok16;
    {
        // Media Foundation decodes media containers; verify the pipeline by
        // round-tripping a WAV through loadMediaAudio (MF has a native WAV
        // source), so no external converter is needed.
        auto tmpw = tmpDir() + "/snd-selftest-mf.wav";
        snd::audio::Buffer b;
        b.channels = 2;
        b.sampleRate = 44100;
        auto mono = makeSine(440.0, 44100, 44100, 0.5f);
        for (float s : mono) {
            b.samples.push_back(s);
            b.samples.push_back(s);
        }
        std::string err;
        ok16 = snd::audio::saveWav(tmpw, b, &err);
        snd::audio::Buffer media;
        ok16 = ok16 && snd::audio::loadMediaAudio(tmpw, media, &err);
        double r1 = rms(b.samples), r2 = rms(media.samples);
        ok16 = ok16 && media.channels == 2 && media.sampleRate == 44100 &&
               media.frames() > b.frames() / 2 && std::abs(r1 - r2) / r1 < 0.2;
        remove(tmpw.c_str());
        if (ok16)
            printf("PASS (Media Foundation decoded %uch @ %u Hz, RMS %.3f -> "
                   "%.3f)\n",
                   media.channels, media.sampleRate, r1, r2);
        else
            printf("FAIL (%s)\n", err.c_str());
    }
#else
    bool ok16 = true;
    printf("skipped (no media backend on this platform yet)\n");
#endif

    printf("[17/22] state tree:      ");
    fflush(stdout);
    bool ok17;
    {
        // properties, children, listeners, undo, and XML round-trip
        snd::state::UndoManager um;
        snd::state::Tree root("Session");
        int propEvents = 0, childEvents = 0;
        root.addListener({[&](snd::state::Tree, const std::string&) { ++propEvents; },
                          [&](snd::state::Tree, snd::state::Tree) { ++childEvents; },
                          nullptr});

        um.beginTransaction();
        root.set("name", std::string("take one"), &um);
        root.set("bpm", 128.0, &um);
        auto track = root.getOrCreateChild("Track", &um);
        track.set("gain", 0.5, &um);

        bool ok = root.getString("name") == "take one" &&
                  root.getDouble("bpm") == 128.0 && propEvents == 3 &&
                  childEvents == 1 && root.childCount() == 1;

        // undo unwinds the whole transaction; redo replays it
        ok = ok && um.undo() && !root.hasProperty("name") && root.childCount() == 0;
        ok = ok && um.redo() && root.getDouble("bpm") == 128.0 &&
             root.child(0).getDouble("gain") == 0.5;

        // XML round-trip is structurally identical
        auto xml = root.toXml();
        auto back = snd::state::Tree::fromXml(xml);
        ok = ok && back.valid() && back.equivalent(root);

        ok17 = ok;
        if (ok17)
            printf("PASS (props/children/listeners/undo/XML all behave)\n");
        else
            printf("FAIL (prop=%d child=%d xml=%zu)\n", propEvents, childEvents,
                   xml.size());
    }

    printf("[18/22] stream reader:   ");
    fflush(stdout);
    bool ok18;
    {
        // seek+read random chunks and compare against the full decode
        auto tmp = tmpDir() +
                   "/snd-selftest-stream.wav";
        snd::audio::Buffer b;
        b.channels = 2;
        b.sampleRate = 48000;
        auto mono = makeSine(440.0, 48000, 96000, 0.5f);
        for (float s : mono) {
            b.samples.push_back(s);
            b.samples.push_back(-s);
        }
        std::string err;
        ok18 = snd::audio::saveWav(tmp, b, &err);

        snd::audio::StreamReader sr;
        ok18 = ok18 && sr.open(tmp, &err) && sr.channels() == 2 &&
               sr.sampleRate() == 48000 && sr.frames() == 96000;
        if (ok18) {
            std::vector<float> chunk(4096 * 2);
            for (uint64_t at : {0ull, 48000ull, 91904ull, 12345ull}) {
                ok18 = ok18 && sr.seek(at);
                uint64_t got = sr.read(chunk.data(), 4096);
                uint64_t expect = std::min<uint64_t>(4096, 96000 - at);
                ok18 = ok18 && got == expect;
                for (uint64_t f = 0; f < got && ok18; f += 512)
                    ok18 = std::fabs(chunk[f * 2] - b.samples[(at + f) * 2]) < 1e-4f;
            }
        }
        sr.close();
        remove(tmp.c_str());
        if (ok18)
            printf("PASS (random seeks match the full decode)\n");
        else
            printf("FAIL (%s)\n", err.c_str());
    }

    printf("[19/22] queue+timer+pool: ");
    fflush(stdout);
    bool ok19;
    {
        // worker pool does real work; results marshal back via runOnMain;
        // a frame-pumped timer fires on schedule
        std::atomic<int> jobs{0};
        int mainSeen = 0;
        {
            snd::platform::ThreadPool pool(4);
            for (int i = 0; i < 64; ++i)
                pool.submit([&] {
                    jobs.fetch_add(1);
                    snd::platform::runOnMain([&] { ++mainSeen; });
                });
            pool.wait();
        }
        int ticks = 0;
        snd::platform::Timer timer;
        timer.start(10, [&] { ++ticks; });
        auto deadline =
            std::chrono::steady_clock::now() + std::chrono::milliseconds(120);
        while (std::chrono::steady_clock::now() < deadline) {
            snd::platform::processMainQueue();
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        timer.stop();
        ok19 = jobs.load() == 64 && mainSeen == 64 && ticks >= 5 && ticks <= 14;
        if (ok19)
            printf("PASS (64 jobs -> 64 main-thread callbacks, timer %d ticks)\n",
                   ticks);
        else
            printf("FAIL (jobs=%d main=%d ticks=%d)\n", jobs.load(), mainSeen, ticks);
    }

    printf("[20/22] graph + latency: ");
    fflush(stdout);
    bool ok20;
    {
        // a fake 256-sample-latency node in parallel with a direct wire: the
        // graph must delay the direct path so the impulse sums coherently
        enum : uint32_t { kFakeDelay = 256 };
        struct FakeDelay final : snd::plugin::Instance {
            snd::plugin::Description d;
            std::vector<snd::plugin::Parameter*> noParams;
            std::vector<float> ringL, ringR;
            size_t pos = 0;

            const snd::plugin::Description& description() const override { return d; }
            bool prepare(double, uint32_t) override
            {
                ringL.assign(kFakeDelay, 0.0f);
                ringR.assign(kFakeDelay, 0.0f);
                pos = 0;
                return true;
            }
            void unprepare() override {}
            bool process(const float* const* in, uint32_t, float* const* out,
                         uint32_t, uint32_t frames) override
            {
                for (uint32_t f = 0; f < frames; ++f) {
                    float l = ringL[pos], r = ringR[pos];
                    ringL[pos] = in[0][f];
                    ringR[pos] = in[1][f];
                    out[0][f] = l;
                    out[1][f] = r;
                    pos = (pos + 1) % kFakeDelay;
                }
                return true;
            }
            void idle() override {}
            uint32_t latencySamples() const override { return kFakeDelay; }
            const std::vector<snd::plugin::Parameter*>& parameters() const override
            {
                return noParams;
            }
            snd::plugin::Parameter* parameterById(const std::string&) const override
            {
                return nullptr;
            }
            bool saveState(std::vector<uint8_t>&) override { return false; }
            bool loadState(const uint8_t*, size_t) override { return false; }
            bool hasEditor() override { return false; }
            bool openEditor(const std::string&) override { return false; }
            void closeEditor() override {}
            bool editorOpen() const override { return false; }
        };

        snd::plugin::Graph g;
        int delayed = g.addNode(std::make_unique<FakeDelay>());
        ok20 = g.connect(snd::plugin::Graph::kInput, delayed) &&
               g.connect(delayed, snd::plugin::Graph::kOutput) &&
               g.connect(snd::plugin::Graph::kInput, snd::plugin::Graph::kOutput) &&
               g.prepare(48000.0, 512);
        ok20 = ok20 && g.latencySamples() == 256;

        if (ok20) {
            // impulse in; expect ONE spike of 2.0 at offset 256
            std::vector<float> inL(512, 0.0f), inR(512, 0.0f), outL(512), outR(512);
            inL[0] = inR[0] = 1.0f;
            const float* ins[2] = {inL.data(), inR.data()};
            float* outs[2] = {outL.data(), outR.data()};
            ok20 = g.process(ins, outs, 512);
            int spikeAt = -1;
            float spike = 0;
            double residue = 0;
            for (int f = 0; f < 512; ++f) {
                if (std::fabs(outL[f]) > spike) {
                    spike = std::fabs(outL[f]);
                    spikeAt = f;
                }
                residue += std::fabs(outL[f]);
            }
            residue -= spike;
            ok20 = ok20 && spikeAt == 256 && std::fabs(spike - 2.0f) < 1e-5f &&
                   residue < 1e-5;
            if (ok20)
                printf("PASS (parallel paths aligned: single 2.0 spike at 256, "
                       "graph latency 256)\n");
            else
                printf("FAIL (spike %.3f at %d, residue %.6f)\n", spike, spikeAt,
                       residue);
        } else
            printf("FAIL (graph build/prepare)\n");
    }

    printf("[21/22] SDK instrument:  ");
    fflush(stdout);
    bool ok21;
    {
        // M2 proof: an SND-built synth, hosted by SND, sings on noteOn and
        // decays after noteOff
        snd::plugin::HostManager manager;
        manager.addDefaultFormats();
        snd::plugin::Format* vst3 = nullptr;
        for (auto& f : manager.formats())
            if (std::string(f->name()) == "VST3")
                vst3 = f.get();
        auto found = vst3 ? vst3->scan(SND_TEST_DEMOSYNTH_PATH)
                          : std::vector<snd::plugin::Description>{};
        auto synth = found.empty() ? nullptr : manager.create(found[0]);
        ok21 = synth != nullptr && found[0].name == "SND Demo Synth" &&
               synth->prepare(48000.0, 512);

        float peakOn = 0.0f, peakTail = 0.0f;
        if (ok21) {
            const uint32_t block = 512;
            std::vector<float> L(block), R(block);
            float* outs[2] = {L.data(), R.data()};
            snd::midi::Buffer on = {snd::midi::Message::noteOn(0, 60, 127)};
            snd::midi::Buffer off = {snd::midi::Message::noteOff(0, 60)};
            snd::midi::Buffer none;
            for (int i = 0; i < 20 && ok21; ++i) { // ~0.2s held
                ok21 = synth->processMidi(nullptr, 0, outs, 2, block,
                                          i == 0 ? on : none);
                for (uint32_t f = 0; f < block; ++f)
                    peakOn = std::max(peakOn, std::fabs(L[f]));
            }
            for (int i = 0; i < 200 && ok21; ++i) { // ~2s after release
                ok21 = synth->processMidi(nullptr, 0, outs, 2, block,
                                          i == 0 ? off : none);
                if (i > 150)
                    for (uint32_t f = 0; f < block; ++f)
                        peakTail = std::max(peakTail, std::fabs(L[f]));
            }
            ok21 = ok21 && peakOn > 0.05f && peakTail < peakOn * 0.1f;
        }
        if (ok21)
            printf("PASS (noteOn peak %.3f, tail after release %.4f)\n", peakOn,
                   peakTail);
        else
            printf("FAIL (found=%zu on=%.3f tail=%.4f)\n", found.size(), peakOn,
                   peakTail);
    }

    printf("[22/22] AU wrapper:      ");
    fflush(stdout);
#if defined(__APPLE__)
    bool ok22;
    {
        // the VST3-wrapped-as-AU, registered IN-PROCESS (no system registry,
        // no registrar rescan), hosted through our own AU host
        void* lib = dlopen(SND_TEST_DEMOSYNTH_AU_BIN, RTLD_NOW);
        auto factory =
            lib ? (AudioComponentFactoryFunction)dlsym(lib, "AUWrapperFactory")
                : nullptr;
        ok22 = factory != nullptr;
        if (ok22) {
            AudioComponentDescription desc{};
            desc.componentType = 'aumu';
            desc.componentSubType = 'dsyn';
            desc.componentManufacturer = 'SndX';
            ok22 = AudioComponentRegister(&desc, CFSTR("SND: Demo Synth"),
                                          0x00010000, factory) != nullptr;
        }
        float peak = 0.0f;
        if (ok22) {
            snd::plugin::HostManager manager;
            manager.addDefaultFormats();
            snd::plugin::Description d;
            d.format = "AU";
            d.identifier = "aumu,dsyn,SndX";
            auto synth = manager.create(d);
            ok22 = synth != nullptr && synth->prepare(48000.0, 512);
            if (ok22) {
                const uint32_t block = 512;
                std::vector<float> L(block), R(block);
                float* outs[2] = {L.data(), R.data()};
                snd::midi::Buffer on = {snd::midi::Message::noteOn(0, 60, 127)};
                snd::midi::Buffer none;
                for (int i = 0; i < 40 && ok22; ++i) {
                    ok22 = synth->processMidi(nullptr, 0, outs, 2, block,
                                              i == 0 ? on : none);
                    for (uint32_t f = 0; f < block; ++f)
                        peak = std::max(peak, std::fabs(L[f]));
                }
                std::vector<uint8_t> st;
                ok22 = ok22 && peak > 0.05f && synth->saveState(st) && !st.empty();
            }
        }
        if (ok22)
            printf("PASS (VST3-in-AU sang via in-process registration, peak %.3f)\n",
                   peak);
        else
            printf("FAIL (peak %.3f)\n", peak);
    }
#else
    bool ok22 = true;
    printf("skipped (AU is macOS-only)\n");
#endif

    bool all = ok1 && ok2 && ok3 && ok4 && ok5 && ok6 && ok7 && ok8 && ok9 && ok10 &&
               ok11 && ok12 && ok13 && ok14 && ok15 && ok16 && ok17 && ok18 && ok19 &&
               ok20 && ok21 && ok22;
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

        // the audio widget set, all in one place
        static float drive = 0.5f, mix = 0.3f, gainDb = -6.0f, fad = 0.8f;
        static bool live = true, power = true;
        static snd::ui::MeterState msV, msH;
        ImGui::Begin("SND Widgets");
        snd::ui::sectionHeader("knobs");
        snd::ui::knob("drive", &drive);
        ImGui::SameLine();
        snd::ui::knob("mix", &mix);
        ImGui::SameLine();
        snd::ui::knobDb("gain", &gainDb, -60.0f, 12.0f);
        snd::ui::sectionHeader("switches");
        snd::ui::toggle("live", &live);
        ImGui::SameLine();
        if (snd::ui::led("pwr", power, 6.0f, true))
            power = !power;
        ImGui::SameLine();
        snd::ui::badge("VST3");
        ImGui::SameLine();
        snd::ui::badge("48k");
        snd::ui::sectionHeader("murk-parity widgets");
        static bool cells[4 * 16] = {};
        static int step = 0;
        step = (step + 1) % (16 * 8);
        snd::ui::patternGrid("grid", cells, 4, 16, ImVec2(320, 72), step / 8);
        static std::vector<snd::ui::EnvPoint> env;
        snd::ui::envelopeEditor("env", env, ImVec2(200, 80));
        ImGui::SameLine();
        static float px = 0.5f, py = 0.5f;
        snd::ui::xyPad("xy", &px, &py, ImVec2(80, 80));
        static snd::ui::KeyboardState kbst;
        snd::ui::keyboard("kb", kbst, ImVec2(320, 60), 48, 2,
                          [](uint8_t, uint8_t) {}, [](uint8_t) {});
        static float dn = 0.3f;
        snd::ui::dragNumber("drag me", &dn, 0.01f, 0.0f, 1.0f);
        snd::ui::sectionHeader("levels");
        float sig = power ? fad * (0.55f + 0.45f * (float)std::sin(ImGui::GetTime() * 3.0))
                          : 0.0f;
        snd::ui::fader("fad", &fad, ImVec2(26, 120));
        ImGui::SameLine();
        snd::ui::meter("mv", msV, sig, ImVec2(10, 120));
        snd::ui::meter("mh", msH, sig, ImVec2(220, 8));
        ImGui::End();

        window.endFrame();
    }

    device.close();
    return 0;
}
