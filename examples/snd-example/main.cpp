// snd-example -- consumes only snd:: APIs. Two jobs:
//   default:     open a window + audio device, prove the stack runs
//   --selftest:  headless pass/fail checks of the real behaviour of every
//                snd capability (decode+play, record, VST3 hosting, AU hosting)
//   --selftest-retained: run only the headless retained UI contract

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
#include "snd/icons.h"
#include "snd/ui.h"
#include "snd/ui_draw_recording.h"
#include "snd/ui_paint.h"
#include "snd/ui_retained.h"
#include "snd/ui_retained_widgets.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <thread>
#include <utility>
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

static bool selftestRetainedUi()
{
    namespace r = snd::ui::retained;
    namespace w = snd::ui::retained::widgets;

    auto named = [](r::Role role, const char* name) {
        r::Semantics sem;
        sem.role = role;
        sem.name = name;
        return sem;
    };
    auto hasAction = [](const r::SemanticNode* node, r::Action action) {
        return node && std::find(node->actions.begin(), node->actions.end(), action) !=
                           node->actions.end();
    };
    auto closeEnough = [](float a, float b) {
        return std::abs(a - b) < 0.0001f;
    };
    auto hasFullFrameFill =
        [&](const snd::ui::draw::RecordingSurface& surface,
            snd::ui::draw::Color color) {
            for (const auto& op : surface.ops()) {
                if (op.name != "fillRect" || op.points.size() < 2 ||
                    op.colors.empty() || op.colors.front() != color)
                    continue;
                if (closeEnough(op.points[0].x, 0.0f) &&
                    closeEnough(op.points[0].y, 0.0f) &&
                    closeEnough(op.points[1].x, 420.0f) &&
                    closeEnough(op.points[1].y, 260.0f))
                    return true;
            }
            return false;
        };

    snd::ui::draw::RecordingSurface recording;
    recording.fillRect({1.0f, 2.0f}, {3.0f, 4.0f}, 0x01020304u, 2.0f,
                       snd::ui::draw::kRoundCornersBottom);
    recording.line({0.0f, 0.0f}, {4.0f, 5.0f}, 0xAABBCCDDu, 1.5f);
    const char* textSample = "abc";
    recording.text({}, 12.0f, {1.0f, 1.0f}, 0x10111213u,
                   textSample, textSample + 2);
    const snd::ui::draw::Vec2 pl[] = {{1.0f, 1.0f}, {2.0f, 3.0f}, {4.0f, 5.0f}};
    recording.polyline(pl, 3, 0x0BADBEEFu, true, 2.0f);
    snd::ui::Palette recPal;
    recPal.text = 0x00112233u;
    recPal.accent = 0x00445566u;
    snd::ui::paint::drawFocusRing(recording, {10.0f, 20.0f}, {30.0f, 40.0f},
                                  recPal, 3.0f, 2.0f);
    snd::ui::paint::drawGradientPanel(recording, {2.0f, 3.0f}, {4.0f, 5.0f},
                                      0x01000001u, 0x02000002u,
                                      0x03000003u, 0x04000004u);
    recording.image((snd::ui::draw::TextureRef)0x1234u, {7.0f, 8.0f},
                    {17.0f, 28.0f}, 0xA1B2C3D4u, {0.25f, 0.5f},
                    {0.75f, 1.0f});
    const auto measured = recording.measureText({}, 12.0f, "abc", nullptr);

    snd::ui::draw::RecordingSurface helperRecording;
    snd::ui::paint::ControlState helperState;
    helperState.hovered = true;
    snd::ui::paint::drawGradientRect(helperRecording, {0.0f, 0.0f},
                                     {4.0f, 4.0f}, 0x11111111u,
                                     0x22222222u, 0.0f);
    snd::ui::paint::drawGradientArc(helperRecording, {5.0f, 6.0f}, 3.0f,
                                    0.0f, 1.0f, 0x33333333u, 0x44444444u,
                                    2.0f, 2);
    snd::ui::paint::drawXYPad(helperRecording, {0.0f, 0.0f}, {10.0f, 10.0f},
                              0.5f, 0.25f, recPal, helperState);
    const bool helperCells[] = {true, false};
    snd::ui::draw::RecordingSurface gridRecording;
    snd::ui::paint::drawPatternGrid(gridRecording, {1.0f, 2.0f}, {20.0f, 10.0f},
                                    helperCells, 1, 2, 1, recPal, {});
    snd::ui::draw::RecordingSurface bodyRecording;
    snd::ui::KnobMod bodyMod;
    bodyMod.depth = 0.10f;
    bodyMod.value = 0.70f;
    snd::ui::paint::drawKnob(bodyRecording, {0.0f, 0.0f}, 20.0f, 0.50f,
                             snd::ui::KnobStyle::Ring, recPal, helperState);
    snd::ui::paint::drawKnobModRing(bodyRecording, {0.0f, 0.0f}, 20.0f,
                                    0.50f, bodyMod, recPal);
    snd::ui::paint::drawButton(bodyRecording, {}, 12.0f, {0.0f, 24.0f},
                               {28.0f, 16.0f}, "OK", recPal, helperState);
    snd::ui::draw::RecordingSurface controlRecording;
    snd::ui::paint::drawToggle(controlRecording, {0.0f, 0.0f}, 24.0f,
                               12.0f, 0.75f, recPal, helperState);
    snd::ui::paint::drawLed(controlRecording, {8.0f, 20.0f}, 3.0f, true,
                            recPal, helperState);
    snd::ui::paint::drawMeter(controlRecording, {0.0f, 28.0f},
                              {30.0f, 5.0f}, 1.0f, 0.5f, -48.0f, recPal);
    snd::ui::paint::drawFader(controlRecording, {0.0f, 40.0f},
                              {18.0f, 50.0f}, 0.65f, recPal, helperState);
    snd::ui::draw::RecordingSurface checkboxRecording;
    snd::ui::paint::drawCheckbox(checkboxRecording, {}, 12.0f,
                                 {0.0f, 0.0f}, {72.0f, 20.0f},
                                 "Snap", true, recPal, helperState);
    snd::ui::draw::RecordingSurface tabRecording;
    const char* tabLabels[] = {"Osc", "Filter", "Env"};
    snd::ui::paint::drawTabBar(tabRecording, {}, 12.0f, {0.0f, 0.0f},
                               {120.0f, 24.0f}, tabLabels, 3, 1, 2,
                               recPal, helperState);

    snd::ui::draw::RecordingSurface keyboardRecording;
    snd::ui::paint::drawKeyboard(keyboardRecording, {0.0f, 0.0f},
                                 {84.0f, 30.0f}, 60, 1, -1, nullptr,
                                 recPal, helperState);
    snd::ui::draw::RecordingSurface moduleRecording;
    snd::ui::paint::drawModuleBox(moduleRecording, {}, 12.0f,
                                  {0.0f, 36.0f}, {72.0f, 44.0f},
                                  "OSC", recPal, helperState, false,
                                  false, {});

    snd::ui::draw::RecordingSurface bridgeRecording;
    snd::ui::paint::ControlState bridgeState;
    snd::ui::paint::ButtonPaintArgs bridgeArgs;
    bridgeArgs.drawList = nullptr;
    bridgeArgs.topLeft = ImVec2(1.0f, 2.0f);
    bridgeArgs.size = ImVec2(11.0f, 13.0f);
    bridgeArgs.palette = &recPal;
    bridgeArgs.state = &bridgeState;
    bridgeArgs.surface = &bridgeRecording;
    bridgeArgs.fontSizePx = 12.0f;
    bool bridgePainterSawSurface = false;
    snd::ui::paint::drawButtonWithPainter(
        bridgeArgs, [&](const snd::ui::paint::ButtonPaintArgs& args) {
            bridgePainterSawSurface = args.drawList == nullptr && args.surface != nullptr;
            if (args.surface)
                args.surface->fillRect({args.topLeft.x, args.topLeft.y},
                                       {args.topLeft.x + args.size.x,
                                        args.topLeft.y + args.size.y},
                                       0x55667788u, 2.5f);
        });

    snd::ui::draw::RecordingSurface defaultBridgeRecording;
    snd::ui::paint::ButtonPaintArgs defaultBridgeArgs;
    defaultBridgeArgs.drawList = nullptr;
    defaultBridgeArgs.topLeft = ImVec2(2.0f, 3.0f);
    defaultBridgeArgs.size = ImVec2(44.0f, 18.0f);
    defaultBridgeArgs.text = "Plain";
    defaultBridgeArgs.palette = &recPal;
    defaultBridgeArgs.state = &bridgeState;
    defaultBridgeArgs.surface = &defaultBridgeRecording;
    defaultBridgeArgs.fontSizePx = 12.0f;
    bool defaultBridgeSawFont = false;
    snd::ui::paint::drawButtonWithPainter(
        defaultBridgeArgs, [&](const snd::ui::paint::ButtonPaintArgs& args) {
            defaultBridgeSawFont = args.drawList == nullptr &&
                                   args.surface != nullptr &&
                                   closeEnough(args.fontSizePx, 12.0f);
            snd::ui::paint::drawDefaultButton(args);
        });
    const auto& helperOps = helperRecording.ops();
    const auto& gridOps = gridRecording.ops();
    const auto& bodyOps = bodyRecording.ops();
    const auto& controlOps = controlRecording.ops();
    const auto& checkboxOps = checkboxRecording.ops();
    const auto& tabOps = tabRecording.ops();
    const auto& keyboardOps = keyboardRecording.ops();
    const auto& moduleOps = moduleRecording.ops();
    const auto& bridgeOps = bridgeRecording.ops();
    const auto& defaultBridgeOps = defaultBridgeRecording.ops();

    r::PaintRenderer surfaceRenderer;
    auto surfaceRoot = w::column("surface.root", 3.0f, r::Insets::all(2.0f));
    surfaceRoot->setSemantics(named(r::Role::Group, "Surface render proof"));
    double surfaceTab = 1.0;
    r::ValueBinding tabBinding;
    tabBinding.get = [&] { return surfaceTab; };
    tabBinding.set = [&](double value) { surfaceTab = value; };
    surfaceRoot->addChild(w::tabBar("surface.tabs", "View",
                                    {"Patch", "Mix", "Mod"},
                                    std::move(tabBinding), &surfaceRenderer,
                                    {112.0f, 24.0f}));
    surfaceRoot->addChild(w::button("surface.button", "Go", {}, &surfaceRenderer));
    bool surfaceCells[] = {true, false, false, true};
    surfaceRoot->addChild(w::patternGrid("surface.pattern", "Pattern",
                                         surfaceCells, 2, 2, &surfaceRenderer,
                                         {64.0f, 30.0f}));
    double surfaceX = 0.25;
    double surfaceY = 0.75;
    r::ValueBinding xBinding;
    xBinding.get = [&] { return surfaceX; };
    xBinding.set = [&](double value) { surfaceX = value; };
    xBinding.min = 0.0;
    xBinding.max = 1.0;
    r::ValueBinding yBinding;
    yBinding.get = [&] { return surfaceY; };
    yBinding.set = [&](double value) { surfaceY = value; };
    yBinding.min = 0.0;
    yBinding.max = 1.0;
    surfaceRoot->addChild(w::xyPad("surface.xy", "XY", std::move(xBinding),
                                   std::move(yBinding), &surfaceRenderer,
                                   {40.0f, 40.0f}));
    surfaceRoot->addChild(w::animatedButton("surface.anim", "Render", {},
                                            &surfaceRenderer, {72.0f, 24.0f},
                                            0xAA332211u, 0xAA554433u, false));
    double surfaceCheck = 1.0;
    r::ValueBinding checkBinding;
    checkBinding.get = [&] { return surfaceCheck; };
    checkBinding.set = [&](double value) { surfaceCheck = value; };
    checkBinding.min = 0.0;
    checkBinding.max = 1.0;
    checkBinding.step = 1.0;
    surfaceRoot->addChild(w::checkbox("surface.checkbox", "Snap",
                                      std::move(checkBinding), &surfaceRenderer,
                                      {80.0f, 22.0f}));
    r::Tree surfaceTree(std::move(surfaceRoot));
    surfaceTree.layout({120.0f, 190.0f});
    r::SemanticNode tabSem;
    bool tabOk = surfaceTree.semanticNode("surface.tabs.tab.1", tabSem) &&
                 tabSem.role == r::Role::Button &&
                 hasAction(&tabSem, r::Action::Activate) &&
                 r::hasState(tabSem.states, r::SemanticState::Selected) &&
                 surfaceTree.performSemanticAction("surface.tabs.tab.0",
                                                   r::Action::Activate) &&
                 std::abs(surfaceTab - 0.0) < 0.0001;
    r::SemanticNode checkboxSem;
    bool checkboxOk =
        surfaceTree.semanticNode("surface.checkbox", checkboxSem) &&
        checkboxSem.role == r::Role::Toggle &&
        checkboxSem.value.text == "Checked" &&
        r::hasState(checkboxSem.states, r::SemanticState::Checked) &&
        hasAction(&checkboxSem, r::Action::Activate) &&
        surfaceTree.performSemanticAction("surface.checkbox",
                                          r::Action::Activate) &&
        std::abs(surfaceCheck - 0.0) < 0.0001;
    snd::ui::draw::RecordingSurface retainedRecording;
    snd::ui::draw::FrameContext frameContext;
    frameContext.fontSizePx = 12.0f;
    frameContext.timeSeconds = 1.25;
    frameContext.pointer = {8.0f, 8.0f};
    frameContext.pointerValid = true;
    surfaceRenderer.render(surfaceTree, retainedRecording, frameContext);
    snd::ui::draw::RecordingSurface defaultContextRecording;
    surfaceRenderer.render(surfaceTree, defaultContextRecording);
    const auto& retainedOps = retainedRecording.ops();
    const auto& defaultContextOps = defaultContextRecording.ops();
    auto hasRetainedOp = [&](const char* name) {
        return std::find_if(retainedOps.begin(), retainedOps.end(),
                            [&](const snd::ui::draw::RecordedOp& op) {
                                return op.name == name;
                            }) != retainedOps.end();
    };
    auto hasRetainedText = [&](const char* text) {
        return std::find_if(retainedOps.begin(), retainedOps.end(),
                            [&](const snd::ui::draw::RecordedOp& op) {
                                return op.name == "text" && op.text == text;
                            }) != retainedOps.end();
    };
    auto hasDefaultContextText = [&](const char* text) {
        return std::find_if(defaultContextOps.begin(), defaultContextOps.end(),
                            [&](const snd::ui::draw::RecordedOp& op) {
                                return op.name == "text" && op.text == text &&
                                       closeEnough(op.a, 13.0f);
                            }) != defaultContextOps.end();
    };

    r::MatrixGridConfig matrixConfig;
    matrixConfig.columns = 8;
    matrixConfig.activeColumns = 6;
    matrixConfig.columnWidth = 10.0f;
    matrixConfig.columnWidths = {10.0f, 12.0f};
    matrixConfig.columnGap = 2.0f;
    matrixConfig.rowGap = 3.0f;
    matrixConfig.gutterWidth = 30.0f;
    matrixConfig.topPad = 1.0f;
    matrixConfig.bottomPad = 2.0f;
    matrixConfig.playheadHeight = 4.0f;
    std::vector<r::MatrixRowBand> matrixRows = {
        {"note", "note", r::MatrixRowType::ContinuousBar, 20.0f},
        {"gate", "gate", r::MatrixRowType::GateCell, 12.0f},
        {"hidden", "hidden", r::MatrixRowType::RouteCell, 10.0f, -1.0f, false},
        {"route", "route", r::MatrixRowType::RouteCell, 12.0f},
        {"band", "band", r::MatrixRowType::Band, 14.0f},
    };
    r::MatrixGridGeometry matrixGeom(matrixConfig, matrixRows);
    matrixGeom.setBounds({5.0f, 7.0f, 200.0f, 100.0f});
    const r::Vec2 matrixPreferred =
        r::MatrixGridGeometry::preferredSize(matrixConfig, matrixRows);
    const r::Rect matrixCell00 = matrixGeom.cellRect(0, 0);
    const r::Rect matrixCell01 = matrixGeom.cellRect(0, 1);
    const r::Rect matrixGutter1 = matrixGeom.gutterRect(1);
    const r::Rect matrixPlay1 = matrixGeom.playheadRect(1);
    const r::MatrixHit matrixHit = matrixGeom.hitTest({36.0f, 36.0f});
    bool matrixOk =
        closeEnough(matrixPreferred.x, 126.0f) &&
        closeEnough(matrixPreferred.y, 74.0f) &&
        closeEnough(matrixCell00.x, 35.0f) &&
        closeEnough(matrixCell00.y, 12.0f) &&
        closeEnough(matrixCell00.w, 10.0f) &&
        closeEnough(matrixCell00.h, 20.0f) &&
        closeEnough(matrixCell01.x, 47.0f) &&
        closeEnough(matrixCell01.w, 12.0f) &&
        closeEnough(matrixGutter1.y, 35.0f) &&
        closeEnough(matrixGutter1.w, 30.0f) &&
        closeEnough(matrixPlay1.x, 47.0f) &&
        closeEnough(matrixPlay1.y, 8.0f) &&
        closeEnough(matrixPlay1.h, 4.0f) &&
        matrixGeom.rowIndex("route") == 2 &&
        matrixHit.kind == r::MatrixHitKind::Cell &&
        matrixHit.row == 1 && matrixHit.column == 0 &&
        matrixHit.rowId == "gate";

    r::SequencerMatrixState seq;
    seq.config.columns = 8;
    seq.config.activeColumns = 6;
    seq.config.columnWidth = 10.0f;
    seq.config.columnGap = 0.0f;
    seq.config.rowGap = 0.0f;
    seq.config.gutterWidth = 20.0f;
    seq.config.playheadHeight = 0.0f;
    seq.config.topPad = 0.0f;
    seq.config.bottomPad = 0.0f;
    seq.rows = {
        {"note", "note", r::MatrixRowType::ContinuousBar, 20.0f},
        {"gate", "gate", r::MatrixRowType::GateCell, 20.0f},
        {"route", "route", r::MatrixRowType::RouteCell, 20.0f},
        {"band", "band", r::MatrixRowType::Band, 20.0f},
    };
    std::vector<double> matrixValues(8, 0.0);
    std::vector<int> matrixGates(8, 0);
    std::vector<int> matrixRoutes(8, 0);
    seq.continuousRows.push_back(
        {"note",
         [&](int c) { return matrixValues[(std::size_t)c]; },
         [&](int c, double v) { matrixValues[(std::size_t)c] = v; },
         0.0,
         1.0,
         0.1,
         0xFFE8D25Cu});
    seq.gateRows.push_back(
        {"gate",
         [&](int c) { return matrixGates[(std::size_t)c] != 0; },
         [&](int c, bool v) { matrixGates[(std::size_t)c] = v ? 1 : 0; },
         0xFF58F07Au});
    seq.routeRows.push_back(
        {"route",
         [&](int c) { return matrixRoutes[(std::size_t)c]; },
         [&](int c, int v) { matrixRoutes[(std::size_t)c] = v; },
         [](int route) {
             return route == 1 ? 0xFFE58A4Fu : 0xFF6FA7FFu;
         },
         3});
    seq.bandRows.push_back({"band", [] {
                                return std::vector<r::MatrixBandCell>{
                                    {1, 3, "A", 0xFF6FA7FFu}};
                            }});
    seq.ghostFences.push_back({"fence.safe", "safe", 2, 5, 0, 2});
    seq.helpOverlays.push_back({"paint", 1, 1});
    seq.playhead.column = 3;

    r::PaintRenderer matrixRenderer;
    r::Tree matrixTree(w::sequencerMatrix("matrix.test", "Matrix", seq,
                                          &matrixRenderer));
    matrixTree.layout({120.0f, 80.0f});
    r::Event matrixEvent;
    matrixEvent.type = r::EventType::MouseDown;
    matrixEvent.position = {25.0f, 2.0f};
    matrixOk = matrixOk && matrixTree.dispatch(matrixEvent);
    matrixEvent.type = r::EventType::MouseMove;
    matrixEvent.position = {55.0f, 18.0f};
    matrixEvent.delta = {30.0f, 16.0f};
    matrixOk = matrixOk && matrixTree.dispatch(matrixEvent);
    matrixEvent.type = r::EventType::MouseUp;
    matrixOk = matrixOk && matrixTree.dispatch(matrixEvent);
    matrixOk = matrixOk && std::abs(matrixValues[0] - 0.9) < 0.0001 &&
               std::abs(matrixValues[1] - 0.6333333333) < 0.0001 &&
               std::abs(matrixValues[2] - 0.3666666667) < 0.0001 &&
               std::abs(matrixValues[3] - 0.1) < 0.0001;

    matrixEvent.type = r::EventType::MouseDown;
    matrixEvent.position = {35.0f, 22.0f};
    matrixOk = matrixOk && matrixTree.dispatch(matrixEvent);
    matrixEvent.type = r::EventType::MouseMove;
    matrixEvent.position = {65.0f, 22.0f};
    matrixEvent.delta = {30.0f, 0.0f};
    matrixOk = matrixOk && matrixTree.dispatch(matrixEvent);
    matrixEvent.type = r::EventType::MouseUp;
    matrixOk = matrixOk && matrixTree.dispatch(matrixEvent);
    matrixOk = matrixOk && matrixGates[1] && matrixGates[2] &&
               matrixGates[3] && matrixGates[4];
    matrixOk = matrixOk &&
               matrixTree.performSemanticAction("matrix.test.cell.1.2",
                                                r::Action::Activate) &&
               !matrixGates[2];

    matrixEvent.type = r::EventType::MouseDown;
    matrixEvent.position = {25.0f, 42.0f};
    matrixOk = matrixOk && matrixTree.dispatch(matrixEvent);
    matrixEvent.type = r::EventType::MouseMove;
    matrixEvent.position = {45.0f, 42.0f};
    matrixEvent.delta = {20.0f, 0.0f};
    matrixOk = matrixOk && matrixTree.dispatch(matrixEvent);
    matrixEvent.type = r::EventType::MouseUp;
    matrixOk = matrixOk && matrixTree.dispatch(matrixEvent);
    matrixOk = matrixOk && matrixRoutes[0] == 1 && matrixRoutes[1] == 1 &&
               matrixRoutes[2] == 1;

    r::SemanticNode matrixSem;
    matrixOk = matrixOk &&
               matrixTree.semanticNode("matrix.test.cell.0.0", matrixSem) &&
               matrixSem.role == r::Role::Slider &&
               closeEnough(matrixSem.bounds.x, 20.0f) &&
               closeEnough(matrixSem.bounds.y, 0.0f) &&
               hasAction(&matrixSem, r::Action::Increment);
    snd::ui::draw::RecordingSurface matrixRecording;
    snd::ui::draw::FrameContext matrixFrame;
    matrixFrame.fontSizePx = 12.0f;
    matrixRenderer.render(matrixTree, matrixRecording, matrixFrame);
    matrixOk = matrixOk && matrixRecording.ops().size() > 40;

    const auto& ops = recording.ops();
    const bool recordingSurfaceOk = ops.size() == 7 &&
        ops[0].name == "fillRect" && ops[0].colors[0] == 0x01020304u &&
        closeEnough(ops[0].points[0].x, 1.0f) &&
        closeEnough(ops[0].points[1].y, 4.0f) &&
        closeEnough(ops[0].a, 2.0f) &&
        closeEnough(ops[0].b, (float)snd::ui::draw::kRoundCornersBottom) &&
        ops[1].name == "line" && ops[1].colors[0] == 0xAABBCCDDu &&
        closeEnough(ops[1].a, 1.5f) &&
        ops[2].name == "text" && ops[2].text == "ab" &&
        ops[3].name == "polyline" && ops[3].points.size() == 3 &&
        ops[3].flag && closeEnough(ops[3].a, 2.0f) &&
        ops[4].name == "strokeRect" && ops[4].colors[0] == 0xE0445566u &&
        closeEnough(ops[4].points[0].x, 8.0f) &&
        closeEnough(ops[4].points[1].y, 42.0f) &&
        closeEnough(ops[4].a, 5.0f) && closeEnough(ops[4].b, 1.5f) &&
        ops[5].name == "fillRectMultiColor" &&
        ops[5].colors.size() == 4 && ops[5].colors[2] == 0x03000003u &&
        closeEnough(ops[5].points[0].x, 2.0f) &&
        closeEnough(ops[5].points[1].x, 6.0f) &&
        ops[6].name == "image" && ops[6].text == "4660" &&
        ops[6].colors.size() == 1 && ops[6].colors[0] == 0xA1B2C3D4u &&
        ops[6].points.size() == 4 &&
        closeEnough(ops[6].points[0].x, 7.0f) &&
        closeEnough(ops[6].points[0].y, 8.0f) &&
        closeEnough(ops[6].points[1].x, 17.0f) &&
        closeEnough(ops[6].points[1].y, 28.0f) &&
        closeEnough(ops[6].points[2].x, 0.25f) &&
        closeEnough(ops[6].points[2].y, 0.5f) &&
        closeEnough(ops[6].points[3].x, 0.75f) &&
        closeEnough(ops[6].points[3].y, 1.0f) &&
        closeEnough(measured.x, 18.0f) && closeEnough(measured.y, 12.0f);
    const bool helperRecordingOk = helperOps.size() == 10 &&
        helperOps[0].name == "fillRectMultiColor" &&
        helperOps[1].name == "line" && helperOps[2].name == "line" &&
        helperOps[3].name == "fillRect" &&
        helperOps[4].name == "line" && helperOps[5].name == "line" &&
        helperOps[6].name == "fillCircle" &&
        helperOps[7].name == "fillCircle" &&
        helperOps[8].name == "strokeCircle" &&
        helperOps[9].name == "strokeRect" &&
        helperOps[9].colors[0] == recPal.frameBright &&
        closeEnough(helperOps[6].points[0].x, 5.0f) &&
        closeEnough(helperOps[6].points[0].y, 7.5f);
    const bool gridRecordingOk = gridOps.size() == 5 &&
        gridOps[0].name == "fillRect" &&
        gridOps[1].name == "fillRect" &&
        gridOps[2].name == "fillRect" &&
        gridOps[3].name == "fillRect" &&
        gridOps[4].name == "strokeRect" &&
        closeEnough(gridOps[4].points[1].x, 21.0f);
    const bool bodyRecordingOk = bodyOps.size() == 11 &&
        bodyOps[0].name == "pathArcTo" &&
        bodyOps[1].name == "pathStroke" &&
        bodyOps[4].name == "fillCircle" &&
        bodyOps[5].name == "pathArcTo" &&
        bodyOps[7].name == "fillCircle" &&
        bodyOps[8].name == "fillRect" &&
        bodyOps[9].name == "strokeRect" &&
        bodyOps[10].name == "text" &&
        bodyOps[10].text == "OK";
    const bool controlRecordingOk = controlOps.size() == 20 &&
        controlOps[0].name == "fillRect" &&
        controlOps[1].name == "strokeRect" &&
        controlOps[2].name == "fillCircle" &&
        controlOps[3].name == "fillCircle" &&
        controlOps[7].name == "strokeCircle" &&
        controlOps[8].name == "fillRect" &&
        controlOps[12].name == "line" &&
        controlOps[13].name == "strokeRect" &&
        controlOps[14].name == "fillRect" &&
        controlOps[19].name == "line";
    const bool checkboxRecordingOk = checkboxOps.size() == 5 &&
        checkboxOps[0].name == "fillRect" &&
        checkboxOps[1].name == "strokeRect" &&
        checkboxOps[2].name == "line" &&
        checkboxOps[3].name == "line" &&
        checkboxOps[4].name == "text" &&
        checkboxOps[4].text == "Snap";
    const bool tabRecordingOk = tabOps.size() >= 7 &&
        tabOps[0].name == "line" &&
        std::find_if(tabOps.begin(), tabOps.end(),
                     [](const snd::ui::draw::RecordedOp& op) {
                         return op.name == "text" && op.text == "Filter";
                     }) != tabOps.end();
    const bool keyboardRecordingOk = keyboardOps.size() >= 2 &&
        keyboardOps[0].name == "fillRect" &&
        closeEnough(keyboardOps[0].b,
                    (float)snd::ui::draw::kRoundCornersBottom) &&
        keyboardOps[1].name == "strokeRect" &&
        closeEnough(keyboardOps[1].c,
                    (float)snd::ui::draw::kRoundCornersBottom);
    const bool moduleRecordingOk = moduleOps.size() >= 2 &&
        moduleOps[1].name == "fillRect" &&
        closeEnough(moduleOps[1].b, (float)snd::ui::draw::kRoundCornersAll);
    const bool bridgeRecordingOk = bridgePainterSawSurface &&
        bridgeOps.size() == 1 &&
        bridgeOps[0].name == "fillRect" &&
        bridgeOps[0].colors[0] == 0x55667788u &&
        closeEnough(bridgeOps[0].points[0].x, 1.0f) &&
        closeEnough(bridgeOps[0].points[1].x, 12.0f) &&
        closeEnough(bridgeOps[0].points[1].y, 15.0f) &&
        closeEnough(bridgeOps[0].a, 2.5f) &&
        defaultBridgeSawFont &&
        defaultBridgeOps.size() >= 3 &&
        defaultBridgeOps[0].name == "fillRect" &&
        defaultBridgeOps[2].name == "text" &&
        defaultBridgeOps[2].text == "Plain";
    const bool retainedRecordingOk = retainedOps.size() >= 20 &&
        hasRetainedOp("fillRect") &&
        hasRetainedOp("strokeRect") &&
        hasRetainedOp("fillCircle") &&
        hasRetainedText("Patch") &&
        hasRetainedText("Go") &&
        hasRetainedText("Render") &&
        hasRetainedText("Snap") &&
        hasDefaultContextText("Go") && tabOk && checkboxOk && matrixOk;
    const bool recordingOk = recordingSurfaceOk && helperRecordingOk &&
        gridRecordingOk && bodyRecordingOk && controlRecordingOk &&
        checkboxRecordingOk && tabRecordingOk && keyboardRecordingOk &&
        moduleRecordingOk && bridgeRecordingOk && retainedRecordingOk;

    auto root = r::Node::make("root", r::Role::Group);
    r::Layout rootLayout;
    rootLayout.kind = r::LayoutKind::Column;
    rootLayout.padding = r::Insets::all(8.0f);
    rootLayout.gap = 4.0f;
    rootLayout.crossAlign = r::Align::Stretch;
    root->setLayout(rootLayout);
    root->setSemantics(named(r::Role::Group, "Test panel"));

    auto row = r::Node::make("controls", r::Role::Group);
    row->setSize(r::Length::fill(), r::Length::fixed(32.0f));
    r::Layout rowLayout;
    rowLayout.kind = r::LayoutKind::Row;
    rowLayout.gap = 4.0f;
    rowLayout.crossAlign = r::Align::Stretch;
    row->setLayout(rowLayout);

    int activated = 0;
    auto play = r::Node::make("transport.play", r::Role::Button);
    play->setSize(r::Length::fixed(60.0f), r::Length::fill());
    play->setFocusable(true);
    play->setSemantics(named(r::Role::Button, "Play"));
    w::attachTooltip(*play, "Start transport");
    play->setOnActivate([&](r::Node&) { ++activated; });

    double gain = 0.5;
    auto slider = r::Node::make("mixer.gain", r::Role::Slider);
    slider->setSize(r::Length::fill(), r::Length::fill());
    slider->setFocusable(true);
    slider->setSemantics(named(r::Role::Slider, "Gain"));
    r::ValueBinding binding;
    binding.get = [&] { return gain; };
    binding.set = [&](double value) { gain = value; };
    binding.format = [](double value) {
        char text[32];
        std::snprintf(text, sizeof text, "%.0f%%", value * 100.0);
        return std::string(text);
    };
    binding.min = 0.0;
    binding.max = 1.0;
    binding.step = 0.1;
    slider->setValueBinding(std::move(binding));

    row->addChild(std::move(play));
    row->addChild(std::move(slider));

    auto meter = r::Node::make("output.meter", r::Role::Meter);
    meter->setSize(r::Length::fill(), r::Length::fixed(12.0f));
    r::Semantics meterSem = named(r::Role::Meter, "Output level");
    meterSem.value.hasNumeric = true;
    meterSem.value.value = 0.25;
    meterSem.value.min = 0.0;
    meterSem.value.max = 1.0;
    meterSem.value.text = "-12 dB";
    meter->setSemantics(meterSem);

    int canvasActions = 0;
    auto canvas = r::Node::make("scope.canvas", r::Role::Canvas);
    canvas->setSize(r::Length::fill(), r::Length::fixed(24.0f));
    canvas->setFocusable(true);
    r::Semantics canvasSemantics = named(r::Role::Canvas, "Oscilloscope");
    canvasSemantics.actions.push_back(r::Action::Activate);
    canvas->setSemantics(canvasSemantics);
    canvas->setOnAction([&](r::Node&, r::Action action, double) {
        if (action != r::Action::Activate)
            return false;
        ++canvasActions;
        return true;
    });

    auto stop = r::Node::make("transport.stop", r::Role::Button);
    stop->setSize(r::Length::fill(), r::Length::fixed(22.0f));
    stop->setFocusable(true);
    stop->setEnabled(false);
    stop->setSemantics(named(r::Role::Button, "Stop"));
    stop->setOnActivate([&](r::Node&) { activated += 100; });

    auto overlay = r::Node::make("overlay", r::Role::Group);
    overlay->setSize(r::Length::fill(), r::Length::fixed(16.0f));
    r::Layout stackLayout;
    stackLayout.kind = r::LayoutKind::Stack;
    stackLayout.mainAlign = r::Align::Stretch;
    stackLayout.crossAlign = r::Align::Stretch;
    overlay->setLayout(stackLayout);

    auto decorative = r::Node::make("decor.icon", r::Role::Text);
    decorative->setSize(r::Length::fill(), r::Length::fill());
    r::Semantics decorSem = named(r::Role::Text, "decorative icon");
    decorSem.hidden = true;
    decorative->setSemantics(decorSem);

    auto front = r::Node::make("overlay.front", r::Role::Text);
    front->setSize(r::Length::fill(), r::Length::fill());
    front->setSemantics(named(r::Role::Text, "Overlay"));
    int customEvents = 0;
    int rightClickEvents = 0;
    int doubleClickEvents = 0;
    int modifiedEvents = 0;
    int wheelEvents = 0;
    int contextEvents = 0;
    bool sawMoveDelta = false;
    float wheelY = 0.0f;
    float moveDeltaY = 0.0f;
    bool releaseOutside = false;
    front->setOnEvent([&](r::Node& node, const r::Event& event) {
        if (event.type == r::EventType::MouseMove) {
            ++customEvents;
            if (event.delta.y != 0.0f) {
                sawMoveDelta = true;
                moveDeltaY = event.delta.y;
            }
            return true;
        }
        if (event.type == r::EventType::MouseDown &&
            event.button == r::MouseButton::Right) {
            ++rightClickEvents;
            if (event.shift && event.ctrl && event.alt && event.super)
                ++modifiedEvents;
            return true;
        }
        if (event.type == r::EventType::MouseDown &&
            event.button == r::MouseButton::Left && event.clickCount == 2) {
            ++doubleClickEvents;
            return true;
        }
        if (event.type == r::EventType::MouseUp &&
            event.button == r::MouseButton::Left && event.clickCount == 2) {
            return true;
        }
        if (event.type == r::EventType::MouseUp &&
            event.button == r::MouseButton::Left &&
            !node.bounds().contains(event.position)) {
            releaseOutside = true;
            return true;
        }
        if (event.type == r::EventType::MouseWheel) {
            ++wheelEvents;
            wheelY = event.wheelDelta.y;
            return true;
        }
        if (event.type == r::EventType::ContextMenu) {
            ++contextEvents;
            return true;
        }
        return false;
    });

    overlay->addChild(std::move(decorative));
    overlay->addChild(std::move(front));

    root->addChild(std::move(row));
    root->addChild(std::move(meter));
    root->addChild(std::move(canvas));
    root->addChild(std::move(stop));
    root->addChild(std::move(overlay));

    r::Tree tree(std::move(root));
    tree.layout({200.0f, 120.0f});
    if (!recordingOk)
    {
        std::fprintf(stderr,
                     "retained recording groups: surface=%d helpers=%d grid=%d "
                     "body=%d controls=%d checkbox=%d tabs=%d keyboard=%d "
                     "module=%d bridge=%d retained=%d\n",
                     recordingSurfaceOk, helperRecordingOk, gridRecordingOk,
                     bodyRecordingOk, controlRecordingOk, checkboxRecordingOk,
                     tabRecordingOk, keyboardRecordingOk, moduleRecordingOk,
                     bridgeRecordingOk, retainedRecordingOk);
    }
    bool ok = tree.validate().empty();

    const auto* playNode = tree.find("transport.play");
    const auto* sliderNode = tree.find("mixer.gain");
    const auto* frontNode = tree.find("overlay.front");
    ok = ok && playNode && sliderNode && frontNode;

    if (ok) {
        r::Rect playBounds = playNode->bounds();
        r::Rect sliderBounds = sliderNode->bounds();
        ok = playBounds.x == 8.0f && playBounds.y == 8.0f &&
             playBounds.w == 60.0f && playBounds.h == 32.0f &&
             sliderBounds.x == 72.0f && sliderBounds.y == 8.0f &&
             sliderBounds.w == 120.0f && sliderBounds.h == 32.0f;
    }

    tree.clearDirty();
    ok = ok && !tree.dirty();

    r::Event move;
    move.type = r::EventType::MouseMove;
    move.position = {10.0f, 10.0f};
    ok = ok && tree.dispatch(move) && tree.hovered() &&
         tree.hovered()->id() == "transport.play" && tree.dirty();

    auto nodeSnapshot = tree.nodeSnapshot();
    auto findNode = [&](const char* id) -> const r::NodeSnapshot* {
        auto it = std::find_if(
            nodeSnapshot.begin(), nodeSnapshot.end(),
            [&](const r::NodeSnapshot& node) { return node.id == id; });
        return it == nodeSnapshot.end() ? nullptr : &*it;
    };
    const r::NodeSnapshot* playSnap = findNode("transport.play");
    const r::NodeSnapshot* gainSnap = findNode("mixer.gain");
    const r::NodeSnapshot* canvasSnap = findNode("scope.canvas");
    const r::NodeSnapshot* stopSnap = findNode("transport.stop");
    ok = ok && playSnap && playSnap->interaction.hovered &&
         !playSnap->interaction.disabled;
    ok = ok && gainSnap && gainSnap->value.hasNumeric &&
         std::abs(gainSnap->value.value - 0.5) < 0.0001;
    ok = ok && canvasSnap && canvasSnap->role == r::Role::Canvas;
    ok = ok && stopSnap && stopSnap->interaction.disabled;
    ok = ok && findNode("decor.icon") != nullptr; // renderable, but semantic-hidden

    move.position = {10.0f, frontNode ? frontNode->bounds().y + 1.0f : 65.0f};
    const r::Vec2 frontPoint = move.position;
    ok = ok && tree.dispatch(move) && customEvents == 1 && tree.hovered() &&
         tree.hovered()->id() == "overlay.front";
    r::Event drag;
    drag.type = r::EventType::MouseDown;
    drag.position = move.position;
    ok = ok && tree.dispatch(drag);
    drag.type = r::EventType::MouseMove;
    drag.position = {10.0f, 10.0f};
    drag.delta = {0.0f, -12.0f};
    ok = ok && tree.dispatch(drag) && customEvents == 2;
    ok = ok && sawMoveDelta && std::abs(moveDeltaY + 12.0f) < 0.0001f;
    drag.type = r::EventType::MouseUp;
    ok = ok && tree.dispatch(drag);
    ok = ok && releaseOutside;

    r::Event pointer;
    pointer.type = r::EventType::MouseDown;
    pointer.position = frontPoint;
    pointer.button = r::MouseButton::Right;
    pointer.shift = true;
    pointer.ctrl = true;
    pointer.alt = true;
    pointer.super = true;
    ok = ok && tree.dispatch(pointer) && rightClickEvents == 1 &&
         modifiedEvents == 1;

    pointer = r::Event{};
    pointer.type = r::EventType::MouseDown;
    pointer.position = frontPoint;
    pointer.button = r::MouseButton::Left;
    pointer.clickCount = 2;
    ok = ok && tree.dispatch(pointer) && doubleClickEvents == 1;
    pointer.type = r::EventType::MouseUp;
    ok = ok && tree.dispatch(pointer);

    pointer = r::Event{};
    pointer.type = r::EventType::MouseWheel;
    pointer.position = frontPoint;
    pointer.button = r::MouseButton::None;
    pointer.wheelDelta = {0.0f, -2.0f};
    ok = ok && tree.dispatch(pointer) && wheelEvents == 1 &&
         std::abs(wheelY + 2.0f) < 0.0001f;

    pointer = r::Event{};
    pointer.type = r::EventType::ContextMenu;
    pointer.position = frontPoint;
    pointer.button = r::MouseButton::Right;
    ok = ok && tree.dispatch(pointer) && contextEvents == 1;
    tree.clearDirty();

    ok = ok && tree.focusNext() && tree.focused() &&
         tree.focused()->id() == "transport.play";
    r::Event key;
    key.type = r::EventType::KeyDown;
    key.key = r::Key::Space;
    ok = ok && tree.dispatch(key) && activated == 1;

    r::Event mouse;
    mouse.type = r::EventType::MouseDown;
    mouse.position = {10.0f, 10.0f};
    mouse.button = r::MouseButton::Right;
    ok = ok && !tree.dispatch(mouse) && activated == 1;
    mouse.type = r::EventType::MouseUp;
    ok = ok && !tree.dispatch(mouse) && activated == 1;

    mouse.type = r::EventType::MouseDown;
    mouse.button = r::MouseButton::Left;
    ok = ok && tree.dispatch(mouse);
    mouse.type = r::EventType::MouseUp;
    ok = ok && tree.dispatch(mouse) && activated == 2;
    r::SemanticNode playFocusSem;
    ok = ok && tree.semanticNode("transport.play", playFocusSem) &&
         r::hasState(playFocusSem.states, r::SemanticState::Focused) &&
         !r::hasState(playFocusSem.states, r::SemanticState::FocusVisible);

    key.key = r::Key::Tab;
    ok = ok && tree.dispatch(key) && tree.focused() &&
         tree.focused()->id() == "mixer.gain";
    r::SemanticNode gainFocusSem;
    ok = ok && tree.semanticNode("mixer.gain", gainFocusSem) &&
         r::hasState(gainFocusSem.states, r::SemanticState::FocusVisible);
    key.key = r::Key::Right;
    ok = ok && tree.dispatch(key) && std::abs(gain - 0.6) < 0.0001;
    ok = ok && tree.performAction("mixer.gain", r::Action::SetValue, 0.25) &&
         std::abs(gain - 0.25) < 0.0001;
    ok = ok && tree.performAction("mixer.gain", r::Action::Decrement) &&
         std::abs(gain - 0.15) < 0.0001;

    key.key = r::Key::Tab;
    ok = ok && tree.dispatch(key) && tree.focused() &&
         tree.focused()->id() == "scope.canvas";
    ok = ok && tree.dispatch(key) && tree.focused() &&
         tree.focused()->id() == "transport.play"; // disabled Stop is skipped
    ok = ok && !tree.performAction("transport.stop", r::Action::Activate) &&
         activated == 2;

    const auto* hit = tree.hitTest({10.0f, frontNode ? frontNode->bounds().y + 1.0f : 65.0f});
    ok = ok && hit && hit->id() == "overlay.front";

    auto overlayPriorityRoot = r::Node::make("overlay-priority.root");
    r::Layout overlayPriorityLayout;
    overlayPriorityLayout.kind = r::LayoutKind::Stack;
    overlayPriorityRoot->setLayout(overlayPriorityLayout);
    auto earlyBranch = r::Node::make("overlay-priority.early");
    earlyBranch->setLayout(overlayPriorityLayout);
    earlyBranch->setSize(r::Length::fill(), r::Length::fill());
    auto popup = r::Node::make("overlay-priority.popup", r::Role::Menu);
    popup->setSize(r::Length::fill(), r::Length::fill());
    popup->setOverlay(true);
    earlyBranch->addChild(std::move(popup));
    auto laterContent = r::Node::make("overlay-priority.content", r::Role::Button);
    laterContent->setSize(r::Length::fill(), r::Length::fill());
    overlayPriorityRoot->addChild(std::move(earlyBranch));
    overlayPriorityRoot->addChild(std::move(laterContent));
    r::Tree overlayPriorityTree(std::move(overlayPriorityRoot));
    overlayPriorityTree.layout({100.0f, 40.0f});
    const auto* overlayHit = overlayPriorityTree.hitTest({50.0f, 20.0f});
    const bool overlayPriorityOk =
        overlayHit && overlayHit->id() == "overlay-priority.popup";
    ok = ok && overlayPriorityOk;

    auto semantics = tree.semanticSnapshot();
    auto findSem = [&](const char* id) -> const r::SemanticNode* {
        auto it = std::find_if(semantics.begin(), semantics.end(),
                               [&](const r::SemanticNode& node) { return node.id == id; });
        return it == semantics.end() ? nullptr : &*it;
    };
    const r::SemanticNode* playSem = findSem("transport.play");
    const r::SemanticNode* gainSem = findSem("mixer.gain");
    const r::SemanticNode* meterSemNode = findSem("output.meter");
    const r::SemanticNode* canvasSem = findSem("scope.canvas");
    const r::SemanticNode* stopSem = findSem("transport.stop");

    r::SemanticNode oneSem;
    r::NodeSnapshot oneNode;
    r::ValueRange oneValue;
    ok = ok && tree.semanticNode("mixer.gain", oneSem) &&
         oneSem.id == "mixer.gain";
    ok = ok && !tree.semanticNode("decor.icon", oneSem);
    ok = ok && tree.nodeSnapshot("decor.icon", oneNode) &&
         oneNode.id == "decor.icon";
    ok = ok && tree.value("mixer.gain", oneValue) &&
         std::abs(oneValue.value - 0.15) < 0.0001;

    ok = ok && playSem && playSem->role == r::Role::Button &&
         playSem->name == "Play" && playSem->tooltip == "Start transport" &&
         hasAction(playSem, r::Action::Activate);
    ok = ok && gainSem && gainSem->role == r::Role::Slider &&
         gainSem->value.hasNumeric && std::abs(gainSem->value.value - 0.15) < 0.0001 &&
         gainSem->value.text == "15%" && hasAction(gainSem, r::Action::Increment) &&
         hasAction(gainSem, r::Action::SetValue) &&
         r::hasState(gainSem->states, r::SemanticState::Focusable);
    ok = ok && meterSemNode && meterSemNode->value.hasNumeric &&
         !hasAction(meterSemNode, r::Action::Focus);
    ok = ok && canvasSem && canvasSem->role == r::Role::Canvas &&
         canvasSem->name == "Oscilloscope" && hasAction(canvasSem, r::Action::Focus) &&
         hasAction(canvasSem, r::Action::Activate);
    ok = ok && tree.performSemanticAction("scope.canvas", r::Action::Activate) &&
         canvasActions == 1;
    ok = ok && stopSem && r::hasState(stopSem->states, r::SemanticState::Disabled);
    ok = ok && !tree.performSemanticAction("transport.stop", r::Action::Activate);
    ok = ok && tree.setValue("mixer.gain", 0.4) && std::abs(gain - 0.4) < 0.0001;
    ok = ok && tree.incrementValue("mixer.gain") && std::abs(gain - 0.5) < 0.0001;
    ok = ok && tree.decrementValue("mixer.gain") && std::abs(gain - 0.4) < 0.0001;
    ok = ok && findSem("decor.icon") == nullptr;
    ok = ok && !tree.performSemanticAction("decor.icon", r::Action::Activate);
    ok = ok && tree.dirty();

    auto duplicateRoot = r::Node::make("dup.root");
    duplicateRoot->addChild(r::Node::make("dup.child"));
    duplicateRoot->addChild(r::Node::make("dup.child"));
    r::Tree duplicateTree(std::move(duplicateRoot));
    auto validation = duplicateTree.validate();
    ok = ok && validation.size() == 1 &&
         validation[0].kind == r::ValidationIssueKind::DuplicateId &&
         validation[0].id == "dup.child";

    auto unnamedRoot = r::Node::make("unnamed.root");
    unnamedRoot->addChild(r::Node::make("unnamed.button", r::Role::Button));
    r::Tree unnamedTree(std::move(unnamedRoot));
    validation = unnamedTree.validate();
    ok = ok && validation.size() == 1 &&
         validation[0].kind == r::ValidationIssueKind::MissingAccessibleName &&
         validation[0].id == "unnamed.button";

    auto hiddenRoot = r::Node::make("hidden.root");
    auto hiddenButton = r::Node::make("hidden.button", r::Role::Button);
    r::Semantics hiddenSem;
    hiddenSem.role = r::Role::Button;
    hiddenSem.states |= r::SemanticState::Hidden;
    hiddenButton->setSemantics(hiddenSem);
    hiddenRoot->addChild(std::move(hiddenButton));
    r::Tree hiddenTree(std::move(hiddenRoot));
    ok = ok && hiddenTree.validate().empty() &&
         hiddenTree.semanticSnapshot().size() == 1;

    double toggleValue = 1.0;
    auto toggleRoot = r::Node::make("toggle.root");
    auto boundToggle = r::Node::make("toggle.bound", r::Role::Toggle);
    boundToggle->setSemantics(named(r::Role::Toggle, "Bypass"));
    r::ValueBinding toggleBinding;
    toggleBinding.get = [&] { return toggleValue; };
    toggleBinding.set = [&](double value) { toggleValue = value; };
    toggleBinding.min = 0.0;
    toggleBinding.max = 1.0;
    toggleBinding.step = 1.0;
    boundToggle->setValueBinding(std::move(toggleBinding));
    toggleRoot->addChild(std::move(boundToggle));
    r::Tree toggleTree(std::move(toggleRoot));
    toggleTree.layout({80.0f, 24.0f});
    auto toggleSemantics = toggleTree.semanticSnapshot();
    auto findToggleSem = [&](const std::vector<r::SemanticNode>& nodes) {
        auto it = std::find_if(nodes.begin(), nodes.end(),
                               [](const r::SemanticNode& node) {
                                   return node.id == "toggle.bound";
                               });
        return it == nodes.end() ? nullptr : &*it;
    };
    const r::SemanticNode* toggleSem = findToggleSem(toggleSemantics);
    ok = ok && toggleSem &&
         r::hasState(toggleSem->states, r::SemanticState::Checked);

    toggleTree.clearDirty();
    toggleValue = 0.0;
    ok = ok && toggleTree.refreshBoundValues() && toggleTree.dirty();
    toggleSemantics = toggleTree.semanticSnapshot();
    toggleSem = findToggleSem(toggleSemantics);
    ok = ok && toggleSem &&
         !r::hasState(toggleSem->states, r::SemanticState::Checked) &&
         std::abs(toggleSem->value.value) < 0.0001;
    auto toggleNodes = toggleTree.nodeSnapshot();
    auto toggleNodeIt = std::find_if(toggleNodes.begin(), toggleNodes.end(),
                                     [](const r::NodeSnapshot& node) {
                                         return node.id == "toggle.bound";
                                     });
    ok = ok && toggleNodeIt != toggleNodes.end() &&
         !toggleNodeIt->interaction.checked;

    if (r::Node* toggleNode = toggleTree.find("toggle.bound")) {
        toggleNode->semantics().states |= r::SemanticState::Checked;
        toggleTree.clearDirty();
        toggleSemantics = toggleTree.semanticSnapshot();
        toggleSem = findToggleSem(toggleSemantics);
        ok = ok && toggleSem &&
             !r::hasState(toggleSem->states, r::SemanticState::Checked);
    } else {
        ok = false;
    }
    ok = ok && toggleTree.performSemanticAction("toggle.bound", r::Action::Activate) &&
         std::abs(toggleValue - 1.0) < 0.0001;
    toggleSemantics = toggleTree.semanticSnapshot();
    toggleSem = findToggleSem(toggleSemantics);
    ok = ok && toggleSem &&
         r::hasState(toggleSem->states, r::SemanticState::Checked);

    double readonlyValue = 0.7;
    auto readonlyRoot = r::Node::make("readonly.root");
    auto readonly = r::Node::make("readonly.meter", r::Role::Meter);
    readonly->setSemantics(named(r::Role::Meter, "Read-only level"));
    r::ValueBinding readonlyBinding;
    readonlyBinding.get = [&] { return readonlyValue; };
    readonlyBinding.min = 0.0;
    readonlyBinding.max = 1.0;
    readonly->setValueBinding(std::move(readonlyBinding));
    readonlyRoot->addChild(std::move(readonly));
    r::Tree readonlyTree(std::move(readonlyRoot));
    r::SemanticNode readonlySem;
    ok = ok && readonlyTree.semanticNode("readonly.meter", readonlySem) &&
         readonlySem.value.hasNumeric &&
         std::abs(readonlySem.value.value - 0.7) < 0.0001 &&
         !hasAction(&readonlySem, r::Action::SetValue) &&
         !hasAction(&readonlySem, r::Action::Increment) &&
         !hasAction(&readonlySem, r::Action::Decrement) &&
         !readonlyTree.setValue("readonly.meter", 0.2) &&
         !readonlyTree.incrementValue("readonly.meter");

    int retainedWrapperClicks = 0;
    double retainedDb = -12.0;
    auto wrapperRoot = w::column("wrapper.root", 4.0f);
    auto gradient = w::gradientButton(
        "wrapper.gradient", "Gradient",
        [&](r::Node&) { ++retainedWrapperClicks; });
    w::attachTooltip(*gradient, "Run gradient action");
    wrapperRoot->addChild(std::move(gradient));
    r::ValueBinding dbBinding;
    dbBinding.get = [&] { return retainedDb; };
    dbBinding.set = [&](double value) { retainedDb = value; };
    wrapperRoot->addChild(w::knobDb("wrapper.gain", "Gain", dbBinding,
                                    -60.0, 12.0));
    r::Tree wrapperTree(std::move(wrapperRoot));
    wrapperTree.layout({140.0f, 100.0f});
    r::SemanticNode gradientSem;
    r::SemanticNode knobDbSem;
    ok = ok && wrapperTree.validate().empty() &&
         wrapperTree.semanticNode("wrapper.gradient", gradientSem) &&
         gradientSem.role == r::Role::Button &&
         gradientSem.tooltip == "Run gradient action" &&
         hasAction(&gradientSem, r::Action::Activate) &&
         wrapperTree.performSemanticAction("wrapper.gradient",
                                           r::Action::Activate) &&
         retainedWrapperClicks == 1 &&
         wrapperTree.semanticNode("wrapper.gain", knobDbSem) &&
         knobDbSem.role == r::Role::Slider &&
         knobDbSem.value.hasNumeric &&
         std::abs(knobDbSem.value.min + 60.0) < 0.0001 &&
         std::abs(knobDbSem.value.max - 12.0) < 0.0001 &&
         knobDbSem.value.text.find("dB") != std::string::npos &&
         hasAction(&knobDbSem, r::Action::SetValue) &&
         wrapperTree.setValue("wrapper.gain", -6.0) &&
         std::abs(retainedDb + 6.0) < 0.0001;

    int selectedIndex = 0;
    int selectedCallback = -1;
    auto listRoot = w::selectableList(
        "choices", "Choices", {"A", "B", "C"}, &selectedIndex, nullptr,
        {120.0f, 80.0f},
        [&](r::Node&, int index) { selectedCallback = index; });
    r::Tree listTree(std::move(listRoot));
    listTree.layout({140.0f, 100.0f});
    auto listSemantics = listTree.semanticSnapshot();
    auto findListSem = [&](const char* id) -> const r::SemanticNode* {
        auto it = std::find_if(listSemantics.begin(), listSemantics.end(),
                               [&](const r::SemanticNode& node) {
                                   return node.id == id;
                               });
        return it == listSemantics.end() ? nullptr : &*it;
    };
    const r::SemanticNode* choice0 = findListSem("choices.item.0");
    const r::SemanticNode* choice2 = findListSem("choices.item.2");
    ok = ok && listTree.validate().empty() && choice0 && choice2 &&
         r::hasState(choice0->states, r::SemanticState::Selected) &&
         !r::hasState(choice2->states, r::SemanticState::Selected) &&
         hasAction(choice2, r::Action::Activate) &&
         listTree.performSemanticAction("choices.item.2",
                                        r::Action::Activate) &&
         selectedIndex == 2 && selectedCallback == 2;
    listSemantics = listTree.semanticSnapshot();
    choice0 = findListSem("choices.item.0");
    choice2 = findListSem("choices.item.2");
    ok = ok && choice0 && choice2 &&
         !r::hasState(choice0->states, r::SemanticState::Selected) &&
         r::hasState(choice2->states, r::SemanticState::Selected);
    listTree.clearDirty();
    selectedIndex = 1;
    ok = ok && listTree.refreshBoundValues() && listTree.dirty();
    listSemantics = listTree.semanticSnapshot();
    const r::SemanticNode* choice1 = findListSem("choices.item.1");
    choice2 = findListSem("choices.item.2");
    ok = ok && choice1 && choice2 &&
         r::hasState(choice1->states, r::SemanticState::Selected) &&
         !r::hasState(choice2->states, r::SemanticState::Selected);

    double fieldValue = 120.0;
    r::PaintRenderer fieldRenderer;
    r::ValueBinding fieldBinding;
    fieldBinding.get = [&] { return fieldValue; };
    fieldBinding.set = [&](double value) { fieldValue = value; };
    fieldBinding.min = 40.0;
    fieldBinding.max = 240.0;
    fieldBinding.step = 1.0;
    fieldBinding.format = [](double value) {
        char text[32];
        std::snprintf(text, sizeof text, "%.0f", value);
        return std::string(text);
    };
    auto fieldRoot = r::Node::make("field.root");
    fieldRoot->addChild(w::valueField("field.tempo", "Tempo", fieldBinding,
                                      &fieldRenderer, {92.0f, 28.0f}));
    r::Tree fieldTree(std::move(fieldRoot));
    fieldTree.layout({120.0f, 44.0f});
    r::SemanticNode fieldSem;
    ok = ok && fieldTree.validate().empty() &&
         fieldTree.semanticNode("field.tempo", fieldSem) &&
         fieldSem.role == r::Role::Slider &&
         fieldSem.value.text == "120";
    r::Event fieldDrag;
    fieldDrag.type = r::EventType::MouseDown;
    fieldDrag.button = r::MouseButton::Left;
    fieldDrag.position = {8.0f, 8.0f};
    ok = ok && fieldTree.dispatch(fieldDrag);
    fieldDrag.type = r::EventType::MouseMove;
    fieldDrag.delta = {10.0f, 0.0f};
    fieldDrag.position = {18.0f, 8.0f};
    ok = ok && fieldTree.dispatch(fieldDrag) &&
         std::abs(fieldValue - 122.0) < 0.0001;
    fieldDrag.type = r::EventType::MouseUp;
    fieldDrag.delta = {};
    ok = ok && fieldTree.dispatch(fieldDrag);
    fieldDrag.type = r::EventType::MouseDown;
    fieldDrag.clickCount = 2;
    ok = ok && fieldTree.dispatch(fieldDrag);
    r::Event fieldText;
    fieldText.type = r::EventType::TextInput;
    fieldText.text = "13";
    ok = ok && fieldTree.dispatch(fieldText);
    r::Event fieldKey;
    fieldKey.type = r::EventType::KeyDown;
    fieldKey.key = r::Key::Backspace;
    ok = ok && fieldTree.dispatch(fieldKey);
    fieldText.text = "35";
    ok = ok && fieldTree.dispatch(fieldText);
    fieldKey.key = r::Key::Left;
    ok = ok && fieldTree.dispatch(fieldKey);
    fieldKey.key = r::Key::Delete;
    ok = ok && fieldTree.dispatch(fieldKey);
    fieldKey.key = r::Key::End;
    ok = ok && fieldTree.dispatch(fieldKey);
    fieldText.text = "5";
    ok = ok && fieldTree.dispatch(fieldText);
    ok = ok && fieldTree.semanticNode("field.tempo", fieldSem) &&
         fieldSem.value.text == "135";
    fieldKey.key = r::Key::Enter;
    ok = ok && fieldTree.dispatch(fieldKey) &&
         std::abs(fieldValue - 135.0) < 0.0001;

    int externalCount = 2;
    int observedCount = externalCount;
    auto externalRoot = r::Node::make("external.root");
    auto externalCanvas = r::Node::make("external.canvas", r::Role::Canvas);
    externalCanvas->setSemantics(named(r::Role::Canvas, "External model"));
    auto refreshExternalSemantics = [&](r::Node& node) {
        r::Semantics& sem = node.semantics();
        sem.value.hasNumeric = true;
        sem.value.min = 0.0;
        sem.value.max = std::max(0, externalCount);
        sem.value.step = 1.0;
        sem.value.value = externalCount;
        char text[64];
        std::snprintf(text, sizeof text, "%d external items", externalCount);
        sem.value.text = text;
    };
    refreshExternalSemantics(*externalCanvas);
    externalCanvas->setOnRefresh([&](r::Node& node) {
        if (observedCount == externalCount)
            return false;
        observedCount = externalCount;
        refreshExternalSemantics(node);
        return true;
    });
    externalRoot->addChild(std::move(externalCanvas));
    r::Tree externalTree(std::move(externalRoot));
    externalTree.clearDirty();
    ok = ok && !externalTree.refreshBoundValues() && !externalTree.dirty();
    externalCount = 4;
    ok = ok && externalTree.refreshBoundValues() && externalTree.dirty();
    r::SemanticNode externalSem;
    ok = ok && externalTree.semanticNode("external.canvas", externalSem) &&
         externalSem.value.hasNumeric &&
         std::abs(externalSem.value.value - 4.0) < 0.0001 &&
         externalSem.value.text == "4 external items";

    int outlineActivated = 0;
    r::PaintRenderer outlineRenderer;
    auto outlineRoot = w::column("outline.root", 4.0f);
    outlineRoot->addChild(w::outlineButton(
        "outline.square", "Square outline", [&](r::Node&) { ++outlineActivated; },
        &outlineRenderer, {96.0f, 30.0f}));
    r::Tree outlineTree(std::move(outlineRoot));
    outlineTree.layout({120.0f, 44.0f});
    const auto* outlineNode = outlineTree.find("outline.square");
    const auto* outlineStyle = outlineRenderer.styleFor("outline.square");
    r::SemanticNode outlineSem;
    ok = ok && outlineNode && outlineNode->bounds().w == 120.0f &&
         outlineNode->bounds().h == 30.0f && outlineStyle &&
         outlineStyle->kind == r::VisualKind::OutlineButton &&
         outlineStyle->outlineButtonStyle.rounding == 0.0f &&
         outlineTree.semanticNode("outline.square", outlineSem) &&
         outlineSem.role == r::Role::Button &&
         outlineSem.name == "Square outline" &&
         hasAction(&outlineSem, r::Action::Activate) &&
         outlineTree.performAction("outline.square", r::Action::Activate) &&
         outlineActivated == 1;

    std::vector<snd::ui::MenuItem> menuItems = {
        {"add", "Add module", "*", false, true, false},
        {"sep", {}, {}, true, false, false},
        {"disabled", "Unavailable", {}, false, false, false},
        {"duplicate", "Duplicate", {}, false, true, true},
        {"delete", "Delete", {}, false, true, false, "Del", true},
        {"more", "More", {}, false, true, false, {}, false,
         std::vector<snd::ui::MenuItem>{
             {"freeze", "Freeze", {}, false, true, false},
             {"flatten", "Flatten", {}, false, true, false},
         }},
    };

    int menuActivated = 0;
    int menuIndex = -1;
    snd::ui::PopupMenuState menuState;
    menuState.open = true;
    r::PaintRenderer menuRenderer;
    auto menuRoot = w::popupMenu(
        "menu.popup", &menuState, menuItems,
        [&](r::Node&, const snd::ui::MenuItem&, int index) {
            ++menuActivated;
            menuIndex = index;
        },
        &menuRenderer, 180.0f);
    r::Tree menuTree(std::move(menuRoot));
    menuTree.layout({220.0f, 120.0f});
    r::SemanticNode menuAddSem;
    r::SemanticNode menuDisabledSem;
    r::SemanticNode menuDupSem;
    r::SemanticNode menuPopupSem;
    r::SemanticNode menuMoreSem;
    const auto* menuAddStyle = menuRenderer.styleFor("menu.popup.add");
    const auto* menuDeleteStyle = menuRenderer.styleFor("menu.popup.delete");
    ok = ok && menuTree.validate().empty() &&
         menuTree.semanticNode("menu.popup", menuPopupSem) &&
         menuPopupSem.role == r::Role::Menu &&
         menuTree.semanticNode("menu.popup.add", menuAddSem) &&
         menuAddSem.role == r::Role::MenuItem &&
         hasAction(&menuAddSem, r::Action::Activate) &&
         !menuTree.semanticNode("menu.popup.sep", menuAddSem) &&
         menuTree.semanticNode("menu.popup.disabled", menuDisabledSem) &&
         r::hasState(menuDisabledSem.states, r::SemanticState::Disabled) &&
         !hasAction(&menuDisabledSem, r::Action::Activate) &&
         menuTree.semanticNode("menu.popup.duplicate", menuDupSem) &&
         r::hasState(menuDupSem.states, r::SemanticState::Checked) &&
         menuAddStyle && menuAddStyle->kind == r::VisualKind::MenuItem &&
         menuAddStyle->menuItem.icon == "*" &&
         menuDeleteStyle && menuDeleteStyle->menuItem.danger &&
         menuDeleteStyle->menuItem.rightText == "Del";
    ok = ok && menuTree.semanticNode("menu.popup.more", menuMoreSem) &&
         hasAction(&menuMoreSem, r::Action::OpenMenu) &&
         menuTree.performSemanticAction("menu.popup.more", r::Action::OpenMenu);
    ok = ok && menuState.openSubmenuPath.size() == 1;
    ok = ok && menuTree.refreshBoundValues();
    menuTree.layout({220.0f, 180.0f});
    ok = ok && menuTree.semanticNode("menu.popup.more.sub.freeze", menuMoreSem) &&
         menuMoreSem.role == r::Role::MenuItem;

    ok = ok && menuTree.focus("menu.popup.add");
    key = {};
    key.type = r::EventType::KeyDown;
    key.text = "d";
    ok = ok && menuTree.dispatch(key);
    menuRenderer.prepareOpenPopups(menuTree);
    ok = ok && menuTree.focused() &&
         menuTree.focused()->id() == "menu.popup.duplicate";
    key = {};
    key.type = r::EventType::KeyDown;
    key.key = r::Key::Down;
    ok = ok && menuTree.dispatch(key);
    menuRenderer.prepareOpenPopups(menuTree);
    ok = ok && menuTree.focused() &&
         menuTree.focused()->id() == "menu.popup.delete";
    key.key = r::Key::Escape;
    ok = ok && menuTree.dispatch(key) && !menuState.open &&
         menuTree.find("menu.popup") && !menuTree.find("menu.popup")->visible();

    menuState.open = true;
    if (auto* menuNode = menuTree.find("menu.popup"))
        menuNode->setVisible(true);
    ok = ok && menuRenderer.dismissOpenPopupsOutside(
                     menuTree, ImVec2(0.0f, 0.0f), ImVec2(500.0f, 500.0f)) &&
         !menuState.open && menuTree.find("menu.popup") &&
         !menuTree.find("menu.popup")->visible();

    menuState.open = true;
    if (auto* menuNode = menuTree.find("menu.popup"))
        menuNode->setVisible(true);
    ok = ok && menuTree.performAction("menu.popup.duplicate", r::Action::Activate) &&
         menuActivated == 1 && menuIndex == 3 && !menuState.open;

    int selectedMenu = 3;
    int dropdownActivated = 0;
    snd::ui::PopupMenuState dropdownState;
    r::PaintRenderer dropdownRenderer;
    snd::ui::paint::OutlineButtonStyle dropdownButtonStyle;
    dropdownButtonStyle.hoverBorder = IM_COL32(30, 200, 180, 255);
    dropdownButtonStyle.activeFill = IM_COL32(30, 200, 180, 48);
    dropdownButtonStyle.rounding = 3.0f;
    auto dropdownRoot = w::dropdownMenu(
        "menu.select", "Module type", dropdownState, menuItems, &selectedMenu,
        [&](r::Node&, const snd::ui::MenuItem&, int) { ++dropdownActivated; },
        &dropdownRenderer, {150.0f, 28.0f}, 180.0f, dropdownButtonStyle);
    r::Tree dropdownTree(std::move(dropdownRoot));
    dropdownTree.layout({220.0f, 160.0f});
    r::SemanticNode dropdownButtonSem;
    const auto* dropdownButtonVisual = dropdownRenderer.styleFor("menu.select.button");
    ok = ok && dropdownTree.validate().empty() &&
         dropdownTree.semanticNode("menu.select.button", dropdownButtonSem) &&
         dropdownButtonSem.role == r::Role::ComboBox &&
         dropdownButtonSem.name == "Duplicate" &&
         hasAction(&dropdownButtonSem, r::Action::OpenMenu) &&
         dropdownButtonVisual &&
         dropdownButtonVisual->outlineButtonStyle.hoverBorder ==
             dropdownButtonStyle.hoverBorder &&
         dropdownButtonVisual->outlineButtonStyle.activeFill ==
             dropdownButtonStyle.activeFill &&
         dropdownButtonVisual->outlineButtonStyle.rounding ==
             dropdownButtonStyle.rounding &&
         !r::hasState(dropdownButtonSem.states, r::SemanticState::Expanded) &&
         !dropdownTree.semanticNode("menu.select.menu.add", menuAddSem);
    const bool dropdownOpened =
        dropdownTree.performAction("menu.select.button", r::Action::OpenMenu);
    ok = ok && dropdownOpened && dropdownState.open;
    ok = ok && dropdownTree.refreshBoundValues();
    ok = ok &&
         dropdownTree.semanticNode("menu.select.menu.add", menuAddSem) &&
         dropdownTree.semanticNode("menu.select.button", dropdownButtonSem) &&
         r::hasState(dropdownButtonSem.states, r::SemanticState::Expanded);
    dropdownTree.layout({220.0f, 160.0f});
    dropdownRenderer.prepareOpenPopups(dropdownTree);
    const r::Node* dropdownMenuNode = dropdownTree.find("menu.select.menu");
    const r::Node* dropdownButtonNode = dropdownTree.find("menu.select.button");
    ok = ok && dropdownMenuNode && dropdownMenuNode->overlay() &&
         dropdownButtonNode &&
         dropdownMenuNode->bounds().y >=
             dropdownButtonNode->bounds().y + dropdownButtonNode->bounds().h;
    ok = ok && dropdownTree.focused() &&
         dropdownTree.focused()->id() == "menu.select.menu.duplicate" &&
         dropdownState.highlightedIndex == 3;
    key.type = r::EventType::KeyDown;
    key.key = r::Key::Down;
    ok = ok && dropdownTree.dispatch(key);
    dropdownRenderer.prepareOpenPopups(dropdownTree);
    ok = ok && dropdownTree.focused() &&
         dropdownTree.focused()->id() == "menu.select.menu.delete" &&
         dropdownState.highlightedIndex == 4;
    key.key = r::Key::Enter;
    const bool dropdownSelected = dropdownTree.dispatch(key);
    ok = ok && dropdownSelected &&
         selectedMenu == 4 && dropdownActivated == 1 && !dropdownState.open;

    int contextOpens = 0;
    int contextActivated = 0;
    snd::ui::PopupMenuState contextState;
    r::PaintRenderer contextRenderer;
    auto contextRoot = r::Node::make("context.root");
    r::Layout contextLayout;
    contextLayout.kind = r::LayoutKind::Stack;
    contextRoot->setLayout(contextLayout);
    contextRoot->addChild(w::contextMenuRegion(
        "context.region", "Canvas menu target", {120.0f, 40.0f}, contextState,
        [&](r::Node&, r::Vec2) { ++contextOpens; }, &contextRenderer));
    contextRoot->addChild(w::popupMenu(
        "context.menu", &contextState, menuItems,
        [&](r::Node&, const snd::ui::MenuItem&, int) { ++contextActivated; },
        &contextRenderer, 120.0f));
    r::Tree contextTree(std::move(contextRoot));
    contextTree.layout({260.0f, 180.0f});
    r::Event contextEvent;
    contextEvent.position = {10.0f, 10.0f};
    contextEvent.button = r::MouseButton::Right;
    contextEvent.type = r::EventType::MouseDown;
    ok = ok && !contextTree.dispatch(contextEvent) && !contextState.open &&
         contextOpens == 0;
    contextEvent.type = r::EventType::MouseUp;
    ok = ok && !contextTree.dispatch(contextEvent) && !contextState.open &&
         contextOpens == 0;
    contextEvent.type = r::EventType::ContextMenu;
    ok = ok && contextTree.dispatch(contextEvent) && contextState.open &&
         contextOpens == 1 &&
         std::abs(contextState.position.x - 10.0f) < 0.0001f &&
         contextState.anchorToPosition;
    ok = ok && contextTree.refreshBoundValues();
    contextTree.layout({260.0f, 180.0f});
    contextRenderer.prepareOpenPopups(contextTree);
    const r::Node* contextMenu = contextTree.find("context.menu");
    ok = ok && contextMenu &&
         std::abs(contextMenu->bounds().x - 10.0f) < 0.0001f &&
         std::abs(contextMenu->bounds().y - 10.0f) < 0.0001f &&
         contextTree.focused() &&
         contextTree.focused()->id() == "context.menu.add";
    r::Event contextClick;
    contextClick.position = {20.0f, 20.0f};
    contextClick.button = r::MouseButton::Left;
    contextClick.type = r::EventType::MouseDown;
    ok = ok && contextTree.dispatch(contextClick);
    contextClick.type = r::EventType::MouseUp;
    ok = ok && contextTree.dispatch(contextClick) && contextActivated == 1 &&
         !contextState.open;
    contextState.open = false;
    ok = ok && contextTree.performSemanticAction("context.region",
                                                 r::Action::OpenMenu) &&
         contextState.open && contextOpens == 2;

    int modalBackgroundActivations = 0;
    int modalConfirmations = 0;
    int modalCancels = 0;
    int modalRootShortcuts = 0;
    r::ModalDialogState confirmState;
    confirmState.open = true;
    r::PaintRenderer modalRenderer;
    auto modalRoot = r::Node::make("modal.root");
    r::Layout modalLayout;
    modalLayout.kind = r::LayoutKind::Stack;
    modalRoot->setLayout(modalLayout);
    modalRoot->setOnEvent([&](r::Node&, const r::Event& event) {
        if (event.type == r::EventType::KeyDown) {
            ++modalRootShortcuts;
            return true;
        }
        return false;
    });
    modalRoot->addChild(w::button(
        "modal.background", "Background",
        [&](r::Node&) { ++modalBackgroundActivations; }, &modalRenderer));
    modalRoot->addChild(w::confirmDialog(
        "modal.confirm", "Delete module?",
        "This removes the module and its cables.", confirmState,
        [&]() { ++modalConfirmations; },
        [&]() { ++modalCancels; }, &modalRenderer));
    r::Tree modalTree(std::move(modalRoot));
    modalTree.layout({420.0f, 260.0f});
    r::SemanticNode modalDialogSem;
    r::SemanticNode modalButtonSem;
    r::SemanticNode modalBackgroundSem;
    ok = ok && modalTree.validate().empty() &&
         modalTree.semanticNode("modal.confirm.dialog", modalDialogSem) &&
         modalDialogSem.role == r::Role::Dialog &&
         modalDialogSem.name == "Delete module?" &&
         !modalTree.semanticNode("modal.background", modalBackgroundSem) &&
         !modalTree.focus("modal.background") &&
         !modalTree.performAction("modal.background", r::Action::Activate) &&
         modalBackgroundActivations == 0;
    ok = ok && modalTree.focusNext() && modalTree.focused() &&
         modalTree.focused()->id() == "modal.confirm.dialog";
    ok = ok && modalTree.focusNext() && modalTree.focused() &&
         modalTree.focused()->id() == "modal.confirm.cancel";
    ok = ok && modalTree.focusNext() && modalTree.focused() &&
         modalTree.focused()->id() == "modal.confirm.confirm";
    ok = ok && modalTree.semanticNode("modal.confirm.confirm", modalButtonSem) &&
         modalButtonSem.role == r::Role::Button &&
         hasAction(&modalButtonSem, r::Action::Activate);
    key = {};
    key.type = r::EventType::KeyDown;
    key.key = r::Key::Delete;
    ok = ok && modalTree.dispatch(key) && modalRootShortcuts == 0;
    key = {};
    key.type = r::EventType::KeyDown;
    key.key = r::Key::Escape;
    ok = ok && modalTree.dispatch(key) && !confirmState.open &&
         confirmState.lastResult == r::ModalDialogResult::Cancel &&
         confirmState.lastAction == "cancel" && modalCancels == 1 &&
         modalTree.find("modal.confirm") &&
         !modalTree.find("modal.confirm")->visible();

    confirmState.open = true;
    ok = ok && modalTree.refreshBoundValues();
    modalTree.layout({420.0f, 260.0f});
    ok = ok && modalTree.performAction("modal.confirm.confirm",
                                       r::Action::Activate) &&
         !confirmState.open &&
         confirmState.lastResult == r::ModalDialogResult::Primary &&
         confirmState.lastAction == "confirm" && modalConfirmations == 1;

    int alertOk = 0;
    r::ModalDialogState alertState;
    alertState.open = true;
    r::ModalDialogOptions alertOptions;
    alertOptions.closeOnScrimClick = true;
    auto alertRoot = r::Node::make("alert.root");
    alertRoot->setLayout(modalLayout);
    alertRoot->addChild(w::button(
        "alert.background", "Background",
        [&](r::Node&) { ++modalBackgroundActivations; }, &modalRenderer));
    alertRoot->addChild(w::alertDialog(
        "alert.modal", "Transport stopped", "The render completed.",
        alertState, [&]() { ++alertOk; }, &modalRenderer, alertOptions));
    r::Tree alertTree(std::move(alertRoot));
    alertTree.layout({420.0f, 260.0f});
    r::SemanticNode alertSem;
    ok = ok && alertTree.validate().empty() &&
         alertTree.semanticNode("alert.modal.dialog", alertSem) &&
         alertSem.role == r::Role::Alert &&
         !alertTree.semanticNode("alert.background", modalBackgroundSem);
    r::Event scrim;
    scrim.position = {8.0f, 8.0f};
    scrim.button = r::MouseButton::Left;
    scrim.type = r::EventType::MouseDown;
    ok = ok && alertTree.dispatch(scrim);
    scrim.type = r::EventType::MouseUp;
    ok = ok && alertTree.dispatch(scrim) && !alertState.open &&
         alertState.lastResult == r::ModalDialogResult::Dismissed &&
         alertState.lastAction == "scrim" && alertOk == 0 &&
         modalBackgroundActivations == 0;

    auto modalBackdropPaintOk =
        [&](r::ModalBackdrop backdrop, snd::ui::draw::Color expected,
            bool shouldPaint) {
            r::PaintRenderer backdropRenderer;
            r::ModalDialogState backdropState;
            backdropState.open = true;
            r::ModalDialogOptions backdropOptions;
            backdropOptions.backdrop = backdrop;
            backdropOptions.scrimColor = 0x11223344u;
            backdropOptions.blankColor = 0x55667788u;
            auto backdropRoot = r::Node::make("backdrop.root");
            backdropRoot->setLayout(modalLayout);
            backdropRoot->addChild(w::modalDialog(
                "backdrop.modal", "Settings", "Choose devices.",
                backdropState, {}, &backdropRenderer, backdropOptions));
            r::Tree backdropTree(std::move(backdropRoot));
            backdropTree.layout({420.0f, 260.0f});
            snd::ui::draw::RecordingSurface backdropRecording;
            backdropRenderer.render(backdropTree, backdropRecording);
            return hasFullFrameFill(backdropRecording, expected) == shouldPaint;
        };
    ok = ok &&
         modalBackdropPaintOk(r::ModalBackdrop::Dim, 0x11223344u, true) &&
         modalBackdropPaintOk(r::ModalBackdrop::Blank, 0x55667788u, true) &&
         modalBackdropPaintOk(r::ModalBackdrop::None, 0x11223344u, false) &&
         modalBackdropPaintOk(r::ModalBackdrop::None, 0x55667788u, false);

    r::ModalDialogState panelState;
    panelState.open = true;
    r::ModalDialogOptions panelOptions;
    panelOptions.backdrop = r::ModalBackdrop::None;
    panelOptions.escapePolicy = r::ModalEscapePolicy::Close;
    panelOptions.role = r::Role::Dialog;
    panelOptions.size = {320.0f, 180.0f};
    auto modalPanelBody = w::panel("modal.panel.body");
    modalPanelBody->setSize(r::Length::fixed(320.0f),
                            r::Length::fixed(180.0f));
    modalPanelBody->setSemantics(named(r::Role::Dialog,
                                       "Audio / MIDI Settings"));
    modalPanelBody->addChild(w::label("modal.panel.label", "Output",
                                      &modalRenderer));
    auto panelRoot = r::Node::make("modal.panel.root");
    panelRoot->setLayout(modalLayout);
    panelRoot->addChild(w::button(
        "modal.panel.background", "Background",
        [&](r::Node&) { ++modalBackgroundActivations; }, &modalRenderer));
    panelRoot->addChild(w::modalPanel("modal.panel", panelState,
                                      std::move(modalPanelBody),
                                      &modalRenderer, panelOptions));
    r::Tree panelTree(std::move(panelRoot));
    panelTree.layout({420.0f, 260.0f});
    r::SemanticNode panelSem;
    ok = ok && panelTree.validate().empty() &&
         panelTree.semanticNode("modal.panel.body", panelSem) &&
         panelSem.role == r::Role::Dialog &&
         panelSem.name == "Audio / MIDI Settings" &&
         !panelTree.semanticNode("modal.panel.background", modalBackgroundSem);
    key = {};
    key.type = r::EventType::KeyDown;
    key.key = r::Key::Escape;
    ok = ok && panelTree.dispatch(key) && !panelState.open &&
         panelState.lastResult == r::ModalDialogResult::Dismissed &&
         panelState.lastAction == "escape";

    r::GraphSurfaceState graphState;
    graphState.viewport.pan = {10.0f, 5.0f};
    graphState.viewport.zoom = 1.0f;
    std::vector<r::GraphNode> graphNodes = {
        {
            "osc",
            "Oscillator",
            {20.0f, 20.0f, 120.0f, 82.0f},
            {},
            {{"out", "Audio out", r::GraphPortDirection::Output,
              r::GraphPortKind::Audio, {112.0f, 34.0f, 12.0f, 12.0f},
              true}},
            {
                {"title", "Oscillator", r::GraphNodePartKind::Title,
                 {0.0f, 0.0f, 120.0f, 26.0f}},
                {"cpu", "CPU", r::GraphNodePartKind::Readout,
                 {10.0f, 52.0f, 64.0f, 20.0f}, "2%"},
                {"bypass", "Bypass", r::GraphNodePartKind::Toggle,
                 {82.0f, 30.0f, 28.0f, 16.0f}, {}, 0.0, false, true, true},
            },
            true,
        },
        {
            "filter",
            "Filter",
            {220.0f, 30.0f, 136.0f, 88.0f},
            {{"in", "Audio in", r::GraphPortDirection::Input,
              r::GraphPortKind::Audio, {-4.0f, 38.0f, 12.0f, 12.0f},
              true}},
            {},
            {
                {"title", "Filter", r::GraphNodePartKind::Title,
                 {0.0f, 0.0f, 136.0f, 26.0f}},
                {"level", "Level", r::GraphNodePartKind::Meter,
                 {18.0f, 54.0f, 88.0f, 10.0f}, {}, 0.7, true},
            },
        },
    };
    std::vector<r::GraphCable> graphCables = {
        {"osc-to-filter", "osc", "out", "filter", "in", true},
    };
    r::GraphSurfaceStyle graphStyle;
    graphStyle.corner = 0.0f;
    graphStyle.squarePins = true;
    graphStyle.pinAudio = IM_COL32(127, 209, 174, 255);
    graphStyle.wireThickness = 2.0f;
    graphStyle.wireDroop = true;
    graphStyle.backdrop = r::GraphSurfaceStyle::Backdrop::GreenGrid;
    const r::Vec2 oscOut = r::graphToScreen(graphState.viewport,
                                            r::Vec2{138.0f, 60.0f});
    r::GraphHit graphHit = r::hitTestGraph(graphState.viewport, graphNodes,
                                           graphCables, oscOut, graphStyle);
    ok = ok && graphHit.kind == r::GraphHitKind::Port &&
         graphHit.nodeId == "osc" && graphHit.portId == "out" && graphHit.output;
    const r::Vec2 bypassPoint = r::graphToScreen(graphState.viewport,
                                                 r::Vec2{116.0f, 58.0f});
    graphHit = r::hitTestGraph(graphState.viewport, graphNodes, graphCables,
                               bypassPoint, graphStyle);
    ok = ok && graphHit.kind == r::GraphHitKind::NodePart &&
         graphHit.nodeId == "osc" && graphHit.partId == "bypass";
    const r::Vec2 cablePoint = r::graphToScreen(graphState.viewport,
                                                r::Vec2{180.0f, 95.0f});
    graphHit = r::hitTestGraph(graphState.viewport, graphNodes, graphCables,
                               cablePoint, graphStyle);
    ok = ok && (graphHit.kind == r::GraphHitKind::Cable ||
                graphHit.kind == r::GraphHitKind::CableEndpoint) &&
         graphHit.cableId == "osc-to-filter";

    int graphSelects = 0;
    int graphActivates = 0;
    int graphContexts = 0;
    int graphViewportChanges = 0;
    int graphConnectionChecks = 0;
    int graphPreviews = 0;
    int graphConnects = 0;
    r::GraphHit selectedGraphHit;
    r::GraphHit activatedGraphHit;
    r::GraphHit connectedGraphFrom;
    r::GraphHit connectedGraphTo;
    snd::ui::PopupMenuState graphMenuState;
    r::PaintRenderer graphRenderer;
    r::GraphSurfaceCallbacks graphCallbacks;
    graphCallbacks.onSelect = [&](const r::GraphHit& hit) {
        ++graphSelects;
        selectedGraphHit = hit;
    };
    graphCallbacks.onActivate = [&](const r::GraphHit& hit) {
        ++graphActivates;
        activatedGraphHit = hit;
    };
    graphCallbacks.onContextMenu = [&](const r::GraphHit&, r::Vec2, r::Vec2) {
        ++graphContexts;
    };
    graphCallbacks.canConnect = [&](const r::GraphHit& from,
                                    const r::GraphHit& to) {
        ++graphConnectionChecks;
        return from.output != to.output;
    };
    graphCallbacks.onConnect = [&](const r::GraphHit& from,
                                   const r::GraphHit& to) {
        ++graphConnects;
        connectedGraphFrom = from;
        connectedGraphTo = to;
    };
    graphCallbacks.onCablePreview = [&](const r::GraphHit&, r::Vec2) {
        ++graphPreviews;
    };
    graphCallbacks.onViewportChanged = [&](const r::GraphViewport&) {
        ++graphViewportChanges;
    };
    auto graphRoot = r::Node::make("graph.root");
    graphRoot->addChild(w::graphSurface("graph.surface", "Patch graph",
                                        graphState, graphNodes, graphCables,
                                        graphCallbacks, &graphRenderer,
                                        {420.0f, 220.0f}, &graphMenuState,
                                        graphStyle));
    r::Tree graphTree(std::move(graphRoot));
    graphTree.layout({440.0f, 240.0f});
    r::SemanticNode graphSem;
    r::SemanticNode graphModuleSem;
    r::SemanticNode graphPortSem;
    r::SemanticNode graphBypassSem;
    r::SemanticNode graphCableSem;
    ok = ok && graphTree.validate().empty() &&
         graphTree.semanticNode("graph.surface", graphSem) &&
         graphSem.role == r::Role::Canvas &&
         graphSem.name == "Patch graph" &&
         hasAction(&graphSem, r::Action::OpenMenu);
    ok = ok && graphTree.semanticNode("graph.surface.module.osc", graphModuleSem) &&
         graphModuleSem.role == r::Role::Group &&
         graphModuleSem.name == "Oscillator" &&
         hasAction(&graphModuleSem, r::Action::OpenMenu) &&
         graphTree.semanticNode("graph.surface.module.osc.port.out", graphPortSem) &&
         graphPortSem.role == r::Role::Button &&
         graphPortSem.name == "Output Audio out" &&
         r::hasState(graphPortSem.states, r::SemanticState::Selected) &&
         graphTree.semanticNode("graph.surface.module.osc.part.bypass",
                                graphBypassSem) &&
         graphBypassSem.role == r::Role::Toggle &&
         graphBypassSem.value.text == "On" &&
         hasAction(&graphBypassSem, r::Action::Activate) &&
         graphTree.semanticNode("graph.surface.cable.osc-to-filter",
                                graphCableSem) &&
         graphCableSem.role == r::Role::Custom &&
         graphCableSem.value.text == "Connected";
    const int graphActivatesBeforeSemantic = graphActivates;
    ok = ok && graphTree.performSemanticAction(
                     "graph.surface.module.osc.part.bypass",
                     r::Action::Activate) &&
         graphActivates == graphActivatesBeforeSemantic + 1;
    int graphContextsBeforeSemantic = graphContexts;
    ok = ok && graphTree.performSemanticAction(
                     "graph.surface.module.osc.port.out",
                     r::Action::OpenMenu) &&
         graphMenuState.open && graphContexts == graphContextsBeforeSemantic + 1 &&
         graphState.hovered.kind == r::GraphHitKind::Port;
    graphMenuState.open = false;
    graphContextsBeforeSemantic = graphContexts;
    ok = ok && graphTree.performSemanticAction(
                     "graph.surface.cable.osc-to-filter",
                     r::Action::Focus) &&
         graphTree.focused() && graphTree.focused()->id() == "graph.surface" &&
         graphState.active.kind == r::GraphHitKind::Cable;
    r::Event graphEvent;
    graphEvent.position = bypassPoint;
    graphEvent.button = r::MouseButton::Left;
    graphEvent.type = r::EventType::MouseDown;
    ok = ok && graphTree.dispatch(graphEvent) && graphSelects == 1 &&
         selectedGraphHit.kind == r::GraphHitKind::NodePart &&
         selectedGraphHit.partId == "bypass";
    graphEvent.type = r::EventType::MouseUp;
    ok = ok && graphTree.dispatch(graphEvent) &&
         graphActivates == graphActivatesBeforeSemantic + 2 &&
         activatedGraphHit.partId == "bypass";
    const r::Vec2 filterIn = r::graphToScreen(graphState.viewport,
                                              r::Vec2{222.0f, 74.0f});
    const int graphSelectsBeforeCable = graphSelects;
    graphEvent.position = oscOut;
    graphEvent.delta = {};
    graphEvent.button = r::MouseButton::Left;
    graphEvent.type = r::EventType::MouseDown;
    ok = ok && graphTree.dispatch(graphEvent) &&
         graphSelects == graphSelectsBeforeCable + 1 &&
         graphState.cablePreviewActive &&
         graphState.cablePreviewStart.nodeId == "osc" &&
         graphState.cablePreviewStart.portId == "out";
    graphEvent.position = filterIn;
    graphEvent.delta = {filterIn.x - oscOut.x, filterIn.y - oscOut.y};
    graphEvent.type = r::EventType::MouseMove;
    ok = ok && graphTree.dispatch(graphEvent) &&
         graphState.cablePreviewValid &&
         graphState.cablePreviewTarget.nodeId == "filter" &&
         graphState.cablePreviewTarget.portId == "in" &&
         graphConnectionChecks > 0 &&
         graphPreviews >= 2;
    graphEvent.delta = {};
    graphEvent.type = r::EventType::MouseUp;
    ok = ok && graphTree.dispatch(graphEvent) &&
         !graphState.cablePreviewActive &&
         graphConnects == 1 &&
         connectedGraphFrom.nodeId == "osc" &&
         connectedGraphFrom.portId == "out" &&
         connectedGraphTo.nodeId == "filter" &&
         connectedGraphTo.portId == "in";
    graphState.focused = {};
    graphState.active = {};
    graphState.hovered = {};
    const int graphActivatesBeforeKeyboard = graphActivates;
    r::Event graphKey;
    graphKey.type = r::EventType::KeyDown;
    graphKey.key = r::Key::Right;
    ok = ok && graphTree.focus("graph.surface") &&
         graphTree.dispatch(graphKey) &&
         graphState.focused.kind == r::GraphHitKind::NodeBody &&
         graphState.focused.nodeId == "osc";
    graphKey.key = r::Key::Right;
    ok = ok && graphTree.dispatch(graphKey) &&
         graphState.focused.kind == r::GraphHitKind::Port &&
         graphState.focused.nodeId == "osc" &&
         graphState.focused.portId == "out";
    ok = ok && graphTree.semanticNode("graph.surface.module.osc.port.out",
                                      graphPortSem) &&
         r::hasState(graphPortSem.states, r::SemanticState::Focused);
    graphKey.key = r::Key::Right;
    ok = ok && graphTree.dispatch(graphKey) &&
         graphState.focused.kind == r::GraphHitKind::NodePart &&
         graphState.focused.nodeId == "osc" &&
         graphState.focused.partId == "bypass";
    graphKey.key = r::Key::Enter;
    ok = ok && graphTree.dispatch(graphKey) &&
         graphActivates == graphActivatesBeforeKeyboard + 1 &&
         activatedGraphHit.partId == "bypass";
    graphEvent.position = oscOut;
    graphEvent.type = r::EventType::MouseWheel;
    graphEvent.button = r::MouseButton::None;
    graphEvent.super = true;
    graphEvent.wheelDelta = {0.0f, 1.0f};
    const float previousZoom = graphState.viewport.zoom;
    ok = ok && graphTree.dispatch(graphEvent) &&
         graphState.viewport.zoom > previousZoom &&
         graphViewportChanges == 1;
    graphEvent.position = oscOut;
    graphEvent.button = r::MouseButton::Right;
    graphEvent.type = r::EventType::ContextMenu;
    ok = ok && graphTree.dispatch(graphEvent) && graphMenuState.open &&
         graphMenuState.anchorToPosition &&
         graphContexts == graphContextsBeforeSemantic + 1 &&
         graphState.hovered.kind == r::GraphHitKind::Port;

    if (ok)
        printf("PASS (layout/focus/events/semantics/dirty state/widgets)\n");
    else
        printf("FAIL (activated=%d gain=%.3f custom=%d right=%d dbl=%d "
               "wheel=%d ctx=%d outline=%d menu=%d dropdown=%d context=%d "
               "graphSel=%d graphAct=%d graphCtx=%d graphView=%d "
               "graphPreview=%d graphConnect=%d graphChecks=%d recording=%d "
               "overlay=%d semantics=%zu)\n",
               activated, gain, customEvents, rightClickEvents,
               doubleClickEvents, wheelEvents, contextEvents, outlineActivated,
               menuActivated, dropdownActivated, contextOpens,
               graphSelects, graphActivates, graphContexts, graphViewportChanges,
               graphPreviews, graphConnects, graphConnectionChecks, recordingOk,
               overlayPriorityOk, semantics.size());
    return recordingOk && ok;
}

static int runSelftest()
{
    printf("=== SND self-test ===\n");

    printf("[1/24] decode + playback: ");
    fflush(stdout);
    bool ok1 = selftestDecodeAndPlay();

    printf("[2/24] capture/record:    ");
    fflush(stdout);
    bool ok2 = selftestRecord();

    printf("[3/24] VST3 hosting:      ");
    fflush(stdout);
    bool ok3 = selftestVST3();

    printf("[4/24] AU hosting:        ");
    fflush(stdout);
#if defined(__APPLE__)
    bool ok4 = selftestAU();
#else
    bool ok4 = true;
    printf("skipped (AU is macOS-only)\n");
#endif

    printf("[5/24] resample:          ");
    fflush(stdout);
    bool ok5 = selftestResample();

    printf("[6/24] STFT round-trip:   ");
    fflush(stdout);
    bool ok6 = selftestStft();

    printf("[7/24] player looping:    ");
    fflush(stdout);
    bool ok7 = selftestLooping();

    printf("[8/24] insert hook:       ");
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

    printf("[9/24] OOP plugin scan:   ");
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

    printf("[10/24] widget set:      ");
    fflush(stdout);
    bool ok10;
    {
        // one headless ImGui frame exercising every snd::ui widget: no GPU,
        // no window -- proves the widget layer stands on its own
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        snd::ui::loadFonts(); // exercise the embedded Material/Lucide fonts headlessly
        ImGuiIO& io = ImGui::GetIO();
        io.DisplaySize = ImVec2(800, 600);
        unsigned char* pixels = nullptr;
        int tw = 0, th = 0;
        io.Fonts->GetTexDataAsRGBA32(&pixels, &tw, &th); // build atlas CPU-side

        snd::ui::Palette pal;
        snd::ui::setPalette(pal);

        float k = 0.5f, db = -6.0f, fad = 0.7f;
        float kd = 0.5f, kr = -0.25f; // styled knobs (Davies / Ring)
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
            interacted = snd::ui::knob("dav", &kd, 0.0f, 1.0f,
                                       snd::ui::KnobStyle::Davies) ||
                         interacted;
            interacted = snd::ui::knob("rng", &kr, -1.0f, 1.0f,
                                       snd::ui::KnobStyle::Ring, 0.0f, "%.2f", true) ||
                         interacted;
            snd::ui::iconButton("ico", ICON_MD_SETTINGS, ImVec2(22, 22));
            interacted = snd::ui::toggle("t", &sw) || interacted;
            interacted = snd::ui::led("l", true, 5.0f, true) || interacted;
            snd::ui::meter("m", ms, 0.5f, ImVec2(10, 80));
            interacted = snd::ui::fader("f", &fad, ImVec2(24, 100)) || interacted;
            snd::ui::badge("VST3");
            snd::ui::sectionHeader("audio widgets");
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
               kd == 0.5f && kr == -0.25f && ms.shown > 0.4f && drawLists > 0;
        if (ok10)
            printf("PASS (knob/toggle/led/meter/fader/badge drew headless)\n");
        else
            printf("FAIL (interacted=%d shown=%.2f lists=%d)\n", (int)interacted,
                   ms.shown, drawLists);
    }

    printf("[11/24] MIDI loopback:   ");
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

    printf("[12/24] AU instrument:   ");
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

    printf("[13/24] client SDK:      ");
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

    printf("[14/24] FLAC encode:     ");
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

    printf("[15/24] MP3 encode:      ");
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

    printf("[16/24] media extract:   ");
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

    printf("[17/24] state tree:      ");
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

    printf("[18/24] stream reader:   ");
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
        snd::audio::StreamWriter sw;
        ok18 = sw.openWav(tmp, b.channels, b.sampleRate, &err);
        for (uint64_t frame = 0; frame < b.frames() && ok18; frame += 3072) {
            const auto count = std::min<uint64_t>(3072, b.frames() - frame);
            ok18 = sw.write(b.samples.data() + frame * b.channels, count, &err);
        }
        sw.close();

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
            printf("PASS (chunked write and random reads match)\n");
        else
            printf("FAIL (%s)\n", err.c_str());
    }

    printf("[19/24] queue+timer+pool: ");
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

    printf("[20/24] graph + latency: ");
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

        struct EventNode final : snd::plugin::Instance {
            explicit EventNode(bool source) : source(source) {}
            snd::plugin::Description d;
            std::vector<snd::plugin::Parameter*> noParams;
            bool source = false;
            int note = -1;
            uint32_t target = 0;

            const snd::plugin::Description& description() const override { return d; }
            bool prepare(double, uint32_t) override { return true; }
            void unprepare() override {}
            bool process(const float* const*, uint32_t, float* const* out,
                         uint32_t, uint32_t frames) override
            {
                for (uint32_t channel = 0; channel < 2; ++channel)
                    std::fill(out[channel], out[channel] + frames, 0.0f);
                return true;
            }
            bool processEvents(const float* const* in, uint32_t inChannels,
                               float* const* out, uint32_t outChannels,
                               uint32_t frames, const snd::midi::Buffer& midiIn,
                               snd::midi::Buffer* midiOut,
                               const snd::control::Buffer& controlIn,
                               snd::control::Buffer* controlOut) override
            {
                if (!process(in, inChannels, out, outChannels, frames))
                    return false;
                if (source)
                    return midiOut && controlOut &&
                           midiOut->push_back(
                               snd::midi::Message::noteOn(0, 64, 100, 3)) &&
                           controlOut->push_back(
                               snd::control::Event::paramSet(0x10203040u, 0.75f, 5));
                if (!midiIn.empty()) note = midiIn[0].data1;
                if (!controlIn.empty()) target = controlIn[0].target;
                return true;
            }
            void idle() override {}
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

        auto source = std::make_unique<EventNode>(true);
        auto sink = std::make_unique<EventNode>(false);
        auto* sinkResult = sink.get();
        snd::plugin::Graph events;
        const int sourceNode = events.addNode(std::move(source));
        const int sinkNode = events.addNode(std::move(sink));
        snd::midi::Buffer noMidi;
        ok20 = ok20 &&
               events.connect(sourceNode, sinkNode,
                              snd::plugin::GraphEdgeType::Midi) &&
               events.connect(sourceNode, sinkNode,
                              snd::plugin::GraphEdgeType::Control) &&
               events.prepare(48000.0, 64) &&
               events.processEvents(nullptr, nullptr, 64, noMidi) &&
               sinkResult->note == 64 && sinkResult->target == 0x10203040u;
        printf(ok20 ? "      typed events: PASS (MIDI + control routed)\n"
                    : "      typed events: FAIL\n");
    }

    printf("[21/24] SDK instrument:  ");
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

    printf("[22/24] AU wrapper:      ");
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

    // ── [23/24] SVG rasterize (nanosvg -> RGBA; the GL-free path) ────────────
    printf("[23/24] svg + png:       ");
    bool ok23 = false;
    {
        const char* svg =
            "<svg width='32' height='32' viewBox='0 0 32 32' "
            "xmlns='http://www.w3.org/2000/svg'>"
            "<circle cx='16' cy='16' r='15' fill='#ffffff'/></svg>";
        auto bmp = snd::ui::rasterizeSvg(svg, 16, IM_COL32(255, 0, 0, 255));
        if (!bmp.rgba.empty() && bmp.w == 16 && bmp.h == 16) {
            auto px = [&](int x, int y, int c) {
                return bmp.rgba[((size_t)y * bmp.w + x) * 4 + c];
            };
            // opaque tinted (red) centre, transparent corner: coverage + tint
            ok23 = px(8, 8, 3) > 200 && px(8, 8, 0) > 200 && px(8, 8, 1) < 40 &&
                   px(0, 0, 3) < 40;
        }
        if (ok23) { // PNG decode (stb_image, GL-free): 2x1 red + half-alpha green
            static const unsigned char png[] = {
                137, 80, 78, 71, 13, 10, 26, 10, 0, 0, 0, 13, 73, 72, 68, 82,
                0, 0, 0, 2, 0, 0, 0, 1, 8, 6, 0, 0, 0, 244, 34, 127, 138,
                0, 0, 0, 15, 73, 68, 65, 84, 120, 156, 99, 248, 207, 192, 240,
                31, 8, 27, 0, 16, 121, 3, 126, 125, 99, 206, 215, 0, 0, 0, 0,
                73, 69, 78, 68, 174, 66, 96, 130};
            auto img = snd::ui::decodeImage(png, (int)sizeof png);
            ok23 = img.w == 2 && img.h == 1 && img.rgba.size() == 8 &&
                   img.rgba[0] == 255 && img.rgba[1] == 0 && img.rgba[3] == 255 &&
                   img.rgba[5] == 255 && img.rgba[7] == 128;
        }
        printf(ok23 ? "PASS\n" : "FAIL\n");
    }

    printf("[24/24] retained UI:     ");
    fflush(stdout);
    bool ok24 = selftestRetainedUi();

    bool all = ok1 && ok2 && ok3 && ok4 && ok5 && ok6 && ok7 && ok8 && ok9 && ok10 &&
               ok11 && ok12 && ok13 && ok14 && ok15 && ok16 && ok17 && ok18 && ok19 &&
               ok20 && ok21 && ok22 && ok23 && ok24;
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

    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "--selftest-retained")
            return selftestRetainedUi() ? 0 : 1;
        if (std::string(argv[i]) == "--selftest")
            return runSelftest();
    }

    snd::audio::Device device;
    if (!device.open(48000, 2)) {
        fprintf(stderr, "Failed to open audio device\n");
        return 1;
    }
    device.start();

    snd::ui::Window window;
    if (!window.create(1180, 800, "SND \xc2\xb7 aGooey UI toolkit")) {
        fprintf(stderr, "Failed to create window\n");
        return 1;
    }
    window.setClearColor(IM_COL32(16, 17, 22, 255));

    while (!window.shouldClose()) {
        if (!window.beginFrame())
            break;

        // ---------------------------------------------------------------------
        // Showcase shell: a left grouped nav + a scrolling content pane, mirroring
        // the form of site/index.html. Every section shows the real widget SND
        // draws (immediate widgets where they exist; paint-layer helpers for the
        // graph/render-surface/randomise-window overlays).
        // ---------------------------------------------------------------------
        enum Sec {
            SEC_Knobs, SEC_ModRing, SEC_Ghost, SEC_Fader, SEC_Toggle, SEC_Buttons,
            SEC_Transport, SEC_Segmented, SEC_Cycle, SEC_Custom, SEC_Tactile,
            SEC_LedBtn, SEC_Led, SEC_Meter, SEC_Badge, SEC_List, SEC_ValueRow,
            SEC_Grid, SEC_Matrix, SEC_Xy, SEC_Env, SEC_Keys, SEC_Surface, SEC_Menus,
            SEC_Dialogs, SEC_Graph, SEC_Fields, SEC_Scroll, SEC_Icons,
        };
        int navTarget = -1;

        const snd::ui::Palette& pal = snd::ui::palette();
        ImU32 dim = pal.textDim;
        ImFont* font = ImGui::GetFont();
        const double t = ImGui::GetTime();

        ImGuiViewport* vp = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(vp->WorkPos);
        ImGui::SetNextWindowSize(vp->WorkSize);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        ImGui::Begin("root", nullptr,
                     ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                         ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                         ImGuiWindowFlags_NoBringToFrontOnFocus |
                         ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        ImGui::PopStyleVar();

        // ---- nav sidebar ----
        ImGui::BeginChild("nav", ImVec2(196.0f, 0.0f), true);
        {
            ImGui::PushStyleColor(ImGuiCol_Text, pal.accent);
            ImGui::TextUnformatted("SND");
            ImGui::PopStyleColor();
            ImGui::PushStyleColor(ImGuiCol_Text, dim);
            ImGui::TextUnformatted("UI toolkit");
            ImGui::PopStyleColor();
            ImGui::Separator();

            auto navGroup = [&](const char* g) {
                ImGui::Dummy(ImVec2(0.0f, 4.0f));
                ImGui::PushStyleColor(ImGuiCol_Text, dim);
                ImGui::TextUnformatted(g);
                ImGui::PopStyleColor();
            };
            auto navLink = [&](int id, const char* label) {
                if (ImGui::Selectable(label))
                    navTarget = id;
            };
            navGroup("Controls");
            navLink(SEC_Knobs, "Knobs");
            navLink(SEC_ModRing, "Knob mod ring");
            navLink(SEC_Ghost, "Randomise window");
            navLink(SEC_Fader, "Fader");
            navLink(SEC_Toggle, "Toggle");
            navLink(SEC_Buttons, "Buttons");
            navLink(SEC_Transport, "Transport");
            navLink(SEC_Segmented, "Segmented");
            navLink(SEC_Cycle, "Cycle button");
            navLink(SEC_Custom, "Animated / custom");
            navLink(SEC_Tactile, "Tactile key");
            navLink(SEC_LedBtn, "LED button");
            navGroup("Indicators");
            navLink(SEC_Led, "LED");
            navLink(SEC_Meter, "Meter");
            navLink(SEC_Badge, "Badge / header");
            navLink(SEC_List, "List item");
            navLink(SEC_ValueRow, "Value row");
            navGroup("Instruments");
            navLink(SEC_Grid, "Pattern grid");
            navLink(SEC_Matrix, "Sequencer matrix");
            navLink(SEC_Xy, "XY pad");
            navLink(SEC_Env, "Envelope");
            navLink(SEC_Keys, "Keyboard");
            navGroup("aGooey");
            navLink(SEC_Surface, "Render surface");
            navLink(SEC_Menus, "Menus");
            navLink(SEC_Dialogs, "Modal dialogs");
            navLink(SEC_Graph, "Graph surface");
            navLink(SEC_Fields, "Text field");
            navLink(SEC_Scroll, "Scroll / splitter");
            navGroup("Assets");
            navLink(SEC_Icons, "Icon catalogue");
        }
        ImGui::EndChild();
        ImGui::SameLine();

        // ---- content ----
        ImGui::BeginChild("content", ImVec2(0.0f, 0.0f), false);
        ImGui::Indent(20.0f);
        ImGui::Dummy(ImVec2(0.0f, 10.0f));

        auto section = [&](int id, const char* title, const char* tag) {
            ImGui::Dummy(ImVec2(0.0f, 18.0f));
            if (navTarget == id)
                ImGui::SetScrollHereY(0.0f);
            snd::ui::sectionHeader(title);
            ImGui::PushStyleColor(ImGuiCol_Text, dim);
            ImGui::TextUnformatted(tag);
            ImGui::PopStyleColor();
            ImGui::Dummy(ImVec2(0.0f, 6.0f));
        };

        // hero
        ImGui::PushStyleColor(ImGuiCol_Text, dim);
        ImGui::TextUnformatted("SND \xc2\xb7 COMPONENT LIBRARY");
        ImGui::PopStyleColor();
        ImGui::SetWindowFontScale(1.7f);
        ImGui::TextUnformatted("The aGooey UI toolkit");
        ImGui::SetWindowFontScale(1.0f);
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 680.0f);
        ImGui::PushStyleColor(ImGuiCol_Text, dim);
        ImGui::TextUnformatted(
            "Every graphic SND draws \xe2\x80\x94 knobs, faders, meters, LEDs, tactile keys, "
            "menus, modal dialogs, graph nodes, a keyboard, an XY pad, a step grid \xe2\x80\x94 "
            "from the same shared paint vocabulary behind snd::ui::retained, Dear ImGui, "
            "and the renderer-neutral draw::Surface path. Drag the knobs & faders "
            "(hold Shift for fine adjust).");
        ImGui::PopStyleColor();
        ImGui::PopTextWrapPos();
        ImGui::PushStyleColor(ImGuiCol_Text, dim);
        ImGui::Text("snd::audio device: %s \xc2\xb7 this example consumes only snd:: APIs",
                    device.name().c_str());
        ImGui::PopStyleColor();

        // ---- Controls ----
        section(SEC_Knobs, "Knobs", "paint::drawKnob \xc2\xb7 KnobStyle");
        {
            static float kr = 0.4f, ks = 0.6f, ksy = 0.5f, kd = 0.7f, kn = 0.35f, kb = -0.2f;
            snd::ui::knob("Ring", &kr, 0.0f, 1.0f, snd::ui::KnobStyle::Ring);
            ImGui::SameLine();
            snd::ui::knob("Seq", &ks, 0.0f, 1.0f, snd::ui::KnobStyle::Seq);
            ImGui::SameLine();
            snd::ui::knob("Synth", &ksy, 0.0f, 1.0f, snd::ui::KnobStyle::Synth);
            ImGui::SameLine();
            snd::ui::knob("Davies", &kd, 0.0f, 1.0f, snd::ui::KnobStyle::Davies);
            ImGui::SameLine();
            snd::ui::knob("NxD", &kn, 0.0f, 1.0f, snd::ui::KnobStyle::Nxd);
            ImGui::SameLine();
            snd::ui::knob("Bipolar", &kb, -1.0f, 1.0f, snd::ui::KnobStyle::Ring, 0.0f,
                          "%+.2f", true);
        }

        section(SEC_ModRing, "Knob modulation ring", "paint::drawKnobModRing");
        {
            static float km = 0.5f;
            snd::ui::KnobMod mod;
            mod.depth = 0.28f;
            mod.value = 0.5f + 0.28f * (float)std::sin(t * 1.5);
            snd::ui::knob("DEPTH", &km, 0.0f, 1.0f, snd::ui::KnobStyle::Synth, mod);
        }

        section(SEC_Ghost, "Randomise window", "paint::drawKnobWindow (ghost fence)");
        {
            ImDrawList* dl = ImGui::GetWindowDrawList();
            snd::ui::paint::ControlState st;
            const float sz = 64.0f;
            ImVec2 p = ImGui::GetCursorScreenPos();
            snd::ui::paint::drawKnob(dl, p, sz, 0.55f, snd::ui::KnobStyle::Davies, pal, st);
            snd::ui::paint::KnobWindow win;
            win.lo = 0.30f;
            win.hi = 0.80f;
            snd::ui::paint::drawKnobWindow(dl, p, sz, win, pal);
            ImGui::Dummy(ImVec2(sz + 18.0f, sz + 20.0f));
            ImGui::SameLine();
            ImVec2 p2 = ImGui::GetCursorScreenPos();
            snd::ui::paint::drawKnob(dl, p2, sz, 0.40f, snd::ui::KnobStyle::Davies, pal, st);
            snd::ui::paint::KnobWindow win2;
            win2.lo = 0.15f;
            win2.hi = 0.55f;
            win2.locked = true;
            snd::ui::paint::drawKnobWindow(dl, p2, sz, win2, pal);
            ImGui::Dummy(ImVec2(sz + 18.0f, sz + 20.0f));
        }

        section(SEC_Fader, "Fader", "paint::drawFader");
        {
            static float fv = 0.7f, fv2 = 0.45f;
            snd::ui::fader("f1", &fv, ImVec2(26.0f, 120.0f));
            ImGui::SameLine();
            snd::ui::fader("f2", &fv2, ImVec2(26.0f, 120.0f));
        }

        section(SEC_Toggle, "Toggle", "paint::drawToggle \xc2\xb7 drawCheckbox");
        {
            static bool tg1 = true, tg2 = false, cb1 = true;
            snd::ui::toggle("Live", &tg1);
            ImGui::SameLine();
            snd::ui::toggle("Mono", &tg2);
            ImGui::SameLine();
            snd::ui::checkbox("Loop", &cb1);
        }

        section(SEC_Buttons, "Buttons", "gradient \xc2\xb7 animated \xc2\xb7 outline");
        {
            snd::ui::gradientButton("Gradient", ImVec2(120.0f, 30.0f),
                                    IM_COL32(70, 70, 90, 255), IM_COL32(35, 35, 50, 255));
            ImGui::SameLine();
            snd::ui::animatedButton("Generate", ImVec2(120.0f, 30.0f));
            ImGui::SameLine();
            snd::ui::outlineButton("Outline", ImVec2(120.0f, 30.0f));
        }

        section(SEC_Transport, "Transport buttons", "paint::drawTransportButton");
        {
            static bool playing = false, looping = false, armed = false;
            using I = snd::ui::Icon;
            ImVec2 tb(38.0f, 22.0f);
            snd::ui::transportButton("t_start", I::SkipToStart, tb);
            ImGui::SameLine();
            if (snd::ui::transportButton("t_play", I::Play, tb, playing, true))
                playing = !playing;
            ImGui::SameLine();
            if (snd::ui::transportButton("t_stop", I::Stop, tb, false, true))
                playing = false;
            ImGui::SameLine();
            if (snd::ui::transportButton("t_rec", I::Record, tb, armed, true))
                armed = !armed;
            ImGui::SameLine();
            snd::ui::transportButton("t_end", I::SkipToEnd, tb);
            ImGui::SameLine();
            if (snd::ui::transportButton("t_loop", I::Loop, tb, looping, true))
                looping = !looping;
        }

        section(SEC_Segmented, "Segmented", "paint::drawSegmented \xc2\xb7 drawTabBar");
        {
            static int seg = 1, tab = 0;
            const char* segs[] = {"Mono", "Stereo", "M/S"};
            snd::ui::segmented("seg", segs, 3, &seg);
            ImGui::SameLine();
            const char* tabs[] = {"Edit", "Mix", "FX"};
            snd::ui::tabBar("tabs", tabs, 3, &tab);
        }

        section(SEC_Cycle, "Cycle button", "paint::drawCycleButton");
        {
            static int cyc = 0;
            const char* rates[] = {"1/4", "1/8", "1/16", "1/32"};
            snd::ui::cycleButton("cyc", rates, 4, &cyc);
        }

        section(SEC_Custom, "Animated & custom paint", "paint::drawButtonWithPainter");
        {
            snd::ui::animatedButton("Randomise", ImVec2(130.0f, 30.0f),
                                    IM_COL32(120, 60, 160, 255), IM_COL32(60, 30, 90, 255));
            ImGui::SameLine();
            snd::ui::button("Custom", ImVec2(130.0f, 30.0f),
                            [](const snd::ui::paint::ButtonPaintArgs& a) {
                                ImVec2 mx(a.topLeft.x + a.size.x, a.topLeft.y + a.size.y);
                                snd::ui::paint::drawGradientRect(a.drawList, a.topLeft, mx,
                                                                 IM_COL32(240, 190, 90, 255),
                                                                 IM_COL32(180, 110, 40, 255),
                                                                 6.0f);
                                if (a.font)
                                    a.drawList->AddText(a.font, ImGui::GetFontSize(),
                                                        ImVec2(a.topLeft.x + 16.0f,
                                                               a.topLeft.y + 7.0f),
                                                        IM_COL32(30, 25, 10, 255), a.text);
                            });
        }

        section(SEC_Tactile, "Tactile key", "paint::drawTactileIconButton");
        {
            snd::ui::iconButton("tk_set", ICON_MD_SETTINGS);
            ImGui::SameLine();
            snd::ui::iconButton("tk_play", ICON_MD_PLAY_ARROW);
            ImGui::SameLine();
            snd::ui::iconButton("tk_save", ICON_MD_SAVE);
            ImGui::SameLine();
            static bool held = true;
            if (snd::ui::iconButton("tk_pow", ICON_MD_POWER_SETTINGS_NEW, ImVec2(0, 0),
                                    nullptr, held))
                held = !held;
            ImGui::SameLine();
            snd::ui::iconButton("tk_eq", ICON_MD_GRAPHIC_EQ, ImVec2(0, 0), nullptr, false,
                                IM_COL32(0x2e, 0x6d, 0xb4, 255));
        }

        section(SEC_LedBtn, "LED button", "paint::drawLedButton");
        {
            static bool armRec = false, mon = true, solo = false;
            snd::ui::ledButton("lb_rec", ICON_MD_MIC, &armRec, armRec);
            ImGui::SameLine();
            snd::ui::ledButton("lb_mon", ICON_MD_HEADPHONES, &mon);
            ImGui::SameLine();
            snd::ui::ledButton("lb_solo", ICON_MD_STAR, &solo, false, ImVec2(0, 0), nullptr,
                               IM_COL32(240, 190, 90, 255));
        }

        // ---- Indicators ----
        section(SEC_Led, "LED", "paint::drawLed");
        {
            static bool l1 = true, l2 = true, l3 = false;
            if (snd::ui::led("led1", l1, 6.0f, true))
                l1 = !l1;
            ImGui::SameLine();
            if (snd::ui::led("led2", l2, 6.0f, true, IM_COL32(80, 220, 120, 255)))
                l2 = !l2;
            ImGui::SameLine();
            if (snd::ui::led("led3", l3, 6.0f, true, IM_COL32(255, 80, 70, 255)))
                l3 = !l3;
        }

        section(SEC_Meter, "Meter", "paint::drawMeter");
        {
            static snd::ui::MeterState mv, mh;
            float sig = 0.5f + 0.45f * (float)std::sin(t * 3.0);
            snd::ui::meter("mv", mv, sig, ImVec2(12.0f, 120.0f));
            ImGui::SameLine();
            snd::ui::meter("mh", mh, sig, ImVec2(240.0f, 12.0f));
        }

        section(SEC_Badge, "Badge & section header", "paint::drawBadge \xc2\xb7 drawSectionHeader");
        {
            snd::ui::badge("VST3");
            ImGui::SameLine();
            snd::ui::badge("AU");
            ImGui::SameLine();
            snd::ui::badge("48k");
            ImGui::SameLine();
            snd::ui::badge("MIDI", IM_COL32(80, 220, 120, 120));
        }

        section(SEC_List, "List item", "paint::drawListItem");
        {
            static std::vector<std::string> items = {"Kick", "Snare", "Hat",
                                                     "Clap", "Rim",   "Tom"};
            static int sel = 0;
            snd::ui::selectableList("list", items, &sel, ImVec2(200.0f, 120.0f));
        }

        section(SEC_ValueRow, "Value row & drag number", "paint::drawValueRow");
        {
            static float g = -6.0f, tempo = 120.0f, mixv = 0.3f;
            snd::ui::dragNumber("Gain dB", &g, 0.1f, -60.0f, 12.0f, "%.1f");
            snd::ui::dragNumber("Tempo", &tempo, 0.5f, 20.0f, 300.0f, "%.0f");
            snd::ui::dragNumber("Mix", &mixv, 0.005f, 0.0f, 1.0f);
        }

        // ---- Instruments ----
        section(SEC_Grid, "Pattern grid", "paint::drawPatternGrid");
        {
            static bool cells[4 * 16] = {};
            static bool seeded = false;
            if (!seeded) {
                for (int i = 0; i < 16; i += 4)
                    cells[i] = true;
                seeded = true;
            }
            int step = (int)(t * 4.0) % 16;
            snd::ui::patternGrid("grid", cells, 4, 16, ImVec2(360.0f, 88.0f), step);
        }

        section(SEC_Matrix, "Sequencer matrix", "paint::drawPatternGrid (multi-row)");
        {
            static bool mcells[8 * 16] = {};
            int step = (int)(t * 4.0) % 16;
            snd::ui::patternGrid("matrix", mcells, 8, 16, ImVec2(360.0f, 150.0f), step);
        }

        section(SEC_Xy, "XY pad", "paint::drawXYPad");
        {
            static float px = 0.5f, py = 0.5f;
            snd::ui::xyPad("xy", &px, &py, ImVec2(140.0f, 140.0f));
        }

        section(SEC_Env, "Envelope / Canvas", "paint::drawEnvelope");
        {
            static std::vector<snd::ui::EnvPoint> env = {
                {0.0f, 0.0f}, {0.3f, 1.0f}, {0.7f, 0.4f}, {1.0f, 0.0f}};
            snd::ui::envelopeEditor("env", env, ImVec2(280.0f, 120.0f));
        }

        section(SEC_Keys, "Keyboard", "paint::drawKeyboard");
        {
            static snd::ui::KeyboardState kb;
            snd::ui::keyboard("kb", kb, ImVec2(360.0f, 80.0f), 48, 2,
                              [](uint8_t, uint8_t) {}, [](uint8_t) {});
        }

        // ---- aGooey ----
        section(SEC_Surface, "aGooey render surface", "draw::Surface (paint vocabulary)");
        {
            ImDrawList* dl = ImGui::GetWindowDrawList();
            snd::ui::paint::ControlState st;
            ImVec2 p = ImGui::GetCursorScreenPos();
            ImVec2 sz(360.0f, 120.0f);
            dl->PushClipRect(p, ImVec2(p.x + sz.x, p.y + sz.y), true);
            snd::ui::paint::drawGradientPanel(dl, p, sz, IM_COL32(40, 44, 60, 255),
                                              IM_COL32(30, 34, 48, 255),
                                              IM_COL32(22, 24, 34, 255),
                                              IM_COL32(34, 30, 46, 255));
            snd::ui::paint::drawKnob(dl, ImVec2(p.x + 24.0f, p.y + 30.0f), 56.0f, 0.62f,
                                     snd::ui::KnobStyle::Synth, pal, st);
            snd::ui::paint::drawMeter(dl, ImVec2(p.x + 100.0f, p.y + 24.0f),
                                      ImVec2(12.0f, 72.0f), 0.6f, 0.85f, -48.0f, pal);
            static float wav[128];
            for (int i = 0; i < 128; ++i)
                wav[i] = 0.8f * (float)std::sin(i * 0.2f + t);
            snd::ui::paint::drawWaveform(dl, ImVec2(p.x + 132.0f, p.y + 20.0f),
                                         ImVec2(206.0f, 80.0f), wav, 128, pal);
            dl->PopClipRect();
            ImGui::Dummy(sz);
        }

        section(SEC_Menus, "Menus", "paint::drawMenuPanel \xc2\xb7 drawMenuItem");
        {
            static const char* waves[] = {"Sine", "Saw", "Square", "Noise"};
            static int msel = 0;
            std::vector<snd::ui::MenuItem> items;
            for (int i = 0; i < 4; ++i) {
                snd::ui::MenuItem m;
                m.id = waves[i];
                m.label = waves[i];
                items.push_back(m);
            }
            snd::ui::dropdownMenu("wave", waves[msel], items, &msel, ImVec2(150.0f, 28.0f));
        }

        section(SEC_Dialogs, "Modal dialogs", "ImGui modal + outlineButton");
        {
            if (snd::ui::outlineButton("Delete module\xe2\x80\xa6", ImVec2(150.0f, 28.0f)))
                ImGui::OpenPopup("delmod");
            if (ImGui::BeginPopupModal("delmod", nullptr,
                                       ImGuiWindowFlags_AlwaysAutoResize)) {
                ImGui::TextUnformatted("Delete module?");
                ImGui::PushStyleColor(ImGuiCol_Text, dim);
                ImGui::TextUnformatted("This cannot be undone.");
                ImGui::PopStyleColor();
                ImGui::Dummy(ImVec2(0.0f, 8.0f));
                if (snd::ui::outlineButton("Cancel", ImVec2(90.0f, 26.0f)))
                    ImGui::CloseCurrentPopup();
                ImGui::SameLine();
                if (snd::ui::outlineButton("Delete", ImVec2(90.0f, 26.0f)))
                    ImGui::CloseCurrentPopup();
                ImGui::EndPopup();
            }
        }

        section(SEC_Graph, "Graph surface", "paint::drawGraphGrid \xc2\xb7 drawModuleBox \xc2\xb7 drawCable");
        {
            ImDrawList* dl = ImGui::GetWindowDrawList();
            snd::ui::paint::ControlState st;
            ImVec2 p = ImGui::GetCursorScreenPos();
            ImVec2 sz(400.0f, 170.0f);
            snd::ui::paint::GraphSurfaceStyle style =
                snd::ui::paint::graphSkinStyle(snd::ui::paint::GraphSkin::Neo);
            style.backdrop = snd::ui::paint::GraphSurfaceStyle::Backdrop::Grid;
            style.backdropFill = snd::ui::paint::graphBackdropFill(style.backdrop);
            dl->PushClipRect(p, ImVec2(p.x + sz.x, p.y + sz.y), true);
            snd::ui::paint::drawGraphGrid(dl, p, sz, ImVec2(0.0f, 0.0f), 1.0f, pal, st, style);
            snd::ui::paint::drawCable(dl, ImVec2(p.x + 150.0f, p.y + 62.0f),
                                      ImVec2(p.x + 250.0f, p.y + 112.0f), pal, st, 0, 2.6f,
                                      style);
            snd::ui::paint::drawModuleBox(dl, font, ImVec2(p.x + 30.0f, p.y + 30.0f),
                                          ImVec2(120.0f, 64.0f), "Oscillator", pal, st,
                                          false, false, style);
            snd::ui::paint::drawModuleBox(dl, font, ImVec2(p.x + 250.0f, p.y + 80.0f),
                                          ImVec2(120.0f, 64.0f), "Filter", pal, st, false,
                                          false, style);
            dl->PopClipRect();
            ImGui::Dummy(sz);
        }

        section(SEC_Fields, "Text field & value field", "ImGui::InputText \xc2\xb7 dragNumber");
        {
            static char name[64] = "Preset 01";
            ImGui::SetNextItemWidth(220.0f);
            ImGui::InputText("##name", name, sizeof name);
            static float freq = 440.0f;
            snd::ui::dragNumber("Frequency", &freq, 1.0f, 20.0f, 20000.0f, "%.0f Hz");
        }

        section(SEC_Scroll, "Scroll view & splitter", "ImGui child + draggable divider");
        {
            static float splitW = 150.0f;
            const float h = 130.0f;
            ImGui::BeginChild("scrollL", ImVec2(splitW, h), true);
            for (int i = 0; i < 30; ++i)
                ImGui::Text("Sample %02d.wav", i);
            ImGui::EndChild();
            ImGui::SameLine();
            ImGui::InvisibleButton("split", ImVec2(8.0f, h));
            if (ImGui::IsItemActive())
                splitW += ImGui::GetIO().MouseDelta.x;
            if (splitW < 90.0f)
                splitW = 90.0f;
            if (splitW > 320.0f)
                splitW = 320.0f;
            ImVec2 sp0 = ImGui::GetItemRectMin(), sp1 = ImGui::GetItemRectMax();
            ImU32 hcol = ImGui::IsItemActive() ? pal.accent : pal.frameBright;
            float cx = (sp0.x + sp1.x) * 0.5f;
            ImGui::GetWindowDrawList()->AddLine(ImVec2(cx, sp0.y + 6.0f),
                                                ImVec2(cx, sp1.y - 6.0f), hcol, 2.0f);
            ImGui::SameLine();
            ImGui::BeginChild("scrollR", ImVec2(0.0f, h), true);
            ImGui::TextWrapped("Right pane. Drag the divider to resize; the list on the "
                               "left scrolls with the wheel.");
            ImGui::EndChild();
        }

        // ---- Assets ----
        section(SEC_Icons, "Icon catalogue", "Material Icons + Lucide");
        {
            const char* glyphs[] = {
                ICON_MD_SETTINGS,   ICON_MD_PLAY_ARROW, ICON_MD_PAUSE,
                ICON_MD_STOP,       ICON_MD_FOLDER,     ICON_MD_SAVE,
                ICON_MD_SEARCH,     ICON_MD_ADD,        ICON_MD_REMOVE,
                ICON_MD_DELETE,     ICON_MD_EDIT,       ICON_MD_CLOSE,
                ICON_MD_CHECK,      ICON_MD_HOME,       ICON_MD_MENU,
                ICON_MD_MIC,        ICON_MD_VOLUME_UP,  ICON_MD_TUNE,
                ICON_MD_PIANO,      ICON_MD_GRAPHIC_EQ, ICON_MD_HEADPHONES,
                ICON_MD_SPEED,      ICON_MD_LAYERS,     ICON_MD_GRID_VIEW,
                ICON_MD_FAVORITE,   ICON_MD_STAR,       ICON_MD_LOCK,
                ICON_MD_VISIBILITY, ICON_MD_REFRESH,    ICON_MD_UNDO,
                ICON_MD_REDO,       ICON_MD_INFO,
            };
            int n = (int)(sizeof(glyphs) / sizeof(glyphs[0]));
            for (int i = 0; i < n; ++i) {
                ImGui::TextUnformatted(glyphs[i]);
                if ((i % 12) != 11 && i + 1 < n)
                    ImGui::SameLine();
            }
            ImGui::Dummy(ImVec2(0.0f, 8.0f));
            ImGui::PushStyleColor(ImGuiCol_Text, dim);
            ImGui::TextUnformatted("Lucide (second font):");
            ImGui::PopStyleColor();
            ImGui::SameLine();
            ImGui::PushFont(snd::ui::iconFontLucide());
            ImGui::TextUnformatted(ICON_LC_SETTINGS);
            ImGui::PopFont();
        }

        ImGui::Dummy(ImVec2(0.0f, 36.0f));
        ImGui::Unindent(20.0f);
        ImGui::EndChild(); // content
        ImGui::End();      // root

        window.endFrame();
    }

    device.close();
    return 0;
}
