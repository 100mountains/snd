// Placeholder proving the audio+UI stack builds and runs. No editor logic here yet.
//
// Run with --selftest for a headless pass/fail check of the actual audio path
// (decode+play a file, record from input) instead of opening the GUI window.

#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

#include "imgui.h"
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_opengl3.h"
#include <GLFW/glfw3.h>

#if defined(__APPLE__)
#include <OpenGL/gl3.h>
#else
#include <GL/gl.h>
#endif

#include <cstdio>
#include <string>

#ifndef SND_TEST_ASSETS_DIR
#define SND_TEST_ASSETS_DIR "."
#endif

// ---------------------------------------------------------------------------
// --selftest: headless checks of the real audio path. No GUI, no CI -- meant
// to be run by hand (see tools/build.sh) whenever someone wants to confirm
// the foundation still actually works, not on every commit.
// ---------------------------------------------------------------------------

struct PlaybackTestState {
    ma_decoder* decoder;
    ma_uint64   framesRead;
    bool        decodeError;
};

static void playback_test_callback(ma_device* device, void* output, const void* /*input*/, ma_uint32 frameCount)
{
    PlaybackTestState* state = (PlaybackTestState*)device->pUserData;
    ma_uint64 framesReadThisCall = 0;
    ma_result result = ma_decoder_read_pcm_frames(state->decoder, output, frameCount, &framesReadThisCall);
    state->framesRead += framesReadThisCall;

    if (framesReadThisCall < frameCount) {
        ma_uint32 channels  = device->playback.channels;
        ma_uint32 remaining = frameCount - (ma_uint32)framesReadThisCall;
        ma_silence_pcm_frames((float*)output + framesReadThisCall * channels, remaining, ma_format_f32, channels);
    }
    if (result != MA_SUCCESS && result != MA_AT_END) {
        state->decodeError = true;
    }
}

struct CaptureTestState {
    ma_uint64 framesCaptured;
};

static void capture_test_callback(ma_device* device, void* /*output*/, const void* /*input*/, ma_uint32 frameCount)
{
    CaptureTestState* state = (CaptureTestState*)device->pUserData;
    state->framesCaptured += frameCount;
}

static bool selftest_decode_and_play()
{
    std::string tonePath = std::string(SND_TEST_ASSETS_DIR) + "/tone.wav";

    ma_decoder decoder;
    if (ma_decoder_init_file(tonePath.c_str(), NULL, &decoder) != MA_SUCCESS) {
        printf("FAIL (could not decode %s)\n", tonePath.c_str());
        return false;
    }

    PlaybackTestState state = {};
    state.decoder = &decoder;

    ma_device_config cfg   = ma_device_config_init(ma_device_type_playback);
    cfg.playback.format    = ma_format_f32;
    cfg.playback.channels  = decoder.outputChannels;
    cfg.sampleRate         = decoder.outputSampleRate;
    cfg.dataCallback       = playback_test_callback;
    cfg.pUserData          = &state;

    bool ok = false;
    ma_device device;
    if (ma_device_init(NULL, &cfg, &device) != MA_SUCCESS) {
        printf("FAIL (could not open playback device)\n");
    } else if (ma_device_start(&device) != MA_SUCCESS) {
        printf("FAIL (could not start playback device)\n");
        ma_device_uninit(&device);
    } else {
        ma_sleep(1200); // the tone is ~1s; give it a little extra to finish
        ma_device_uninit(&device);
        if (state.framesRead > 0 && !state.decodeError) {
            printf("PASS (%llu frames decoded and played)\n", (unsigned long long)state.framesRead);
            ok = true;
        } else {
            printf("FAIL (no frames played, or a decode error occurred)\n");
        }
    }

    ma_decoder_uninit(&decoder);
    return ok;
}

static bool selftest_record()
{
    CaptureTestState state = {};

    ma_device_config cfg = ma_device_config_init(ma_device_type_capture);
    cfg.capture.format    = ma_format_f32;
    cfg.capture.channels  = 1;
    cfg.sampleRate        = 48000;
    cfg.dataCallback      = capture_test_callback;
    cfg.pUserData         = &state;

    ma_device device;
    if (ma_device_init(NULL, &cfg, &device) != MA_SUCCESS) {
        printf("FAIL (could not open capture device)\n");
        return false;
    }
    if (ma_device_start(&device) != MA_SUCCESS) {
        printf("FAIL (could not start capture device)\n");
        ma_device_uninit(&device);
        return false;
    }

    ma_sleep(1000);
    ma_device_uninit(&device);

    if (state.framesCaptured > 0) {
        printf("PASS (%llu frames captured)\n", (unsigned long long)state.framesCaptured);
        return true;
    }
    printf("FAIL (no frames captured)\n");
    return false;
}

static int run_selftest()
{
    printf("=== SND self-test ===\n");

    printf("[1/2] decode + playback: ");
    fflush(stdout);
    bool playbackOk = selftest_decode_and_play();

    printf("[2/2] capture/record:    ");
    fflush(stdout);
    bool captureOk = selftest_record();

    printf("[--] VST/plugin load:    skipped (plugin hosting not built yet)\n");

    bool allPassed = playbackOk && captureOk;
    printf("=== %s ===\n", allPassed ? "ALL PASS" : "FAILED");
    return allPassed ? 0 : 1;
}

// ---------------------------------------------------------------------------
// Normal path: open a silent playback device and a blank window.
// ---------------------------------------------------------------------------

static void silence_callback(ma_device* /*device*/, void* output, const void* /*input*/, ma_uint32 frameCount)
{
    ma_silence_pcm_frames(output, frameCount, ma_format_f32, 2);
}

int main(int argc, char** argv)
{
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "--selftest") {
            return run_selftest();
        }
    }

    // --- audio device ---
    ma_device_config deviceConfig = ma_device_config_init(ma_device_type_playback);
    deviceConfig.playback.format   = ma_format_f32;
    deviceConfig.playback.channels = 2;
    deviceConfig.sampleRate        = 48000;
    deviceConfig.dataCallback      = silence_callback;

    ma_device device;
    if (ma_device_init(NULL, &deviceConfig, &device) != MA_SUCCESS) {
        fprintf(stderr, "Failed to open audio device\n");
        return 1;
    }
    if (ma_device_start(&device) != MA_SUCCESS) {
        fprintf(stderr, "Failed to start audio device\n");
        ma_device_uninit(&device);
        return 1;
    }
    printf("Audio device open: %s\n", device.playback.name);

    // --- window ---
    if (!glfwInit()) {
        fprintf(stderr, "Failed to init GLFW\n");
        ma_device_uninit(&device);
        return 1;
    }

#if defined(__APPLE__)
    const char* glsl_version = "#version 150";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
#else
    const char* glsl_version = "#version 130";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
#endif

    GLFWwindow* window = glfwCreateWindow(800, 600, "SND placeholder", NULL, NULL);
    if (!window) {
        fprintf(stderr, "Failed to create window\n");
        glfwTerminate();
        ma_device_uninit(&device);
        return 1;
    }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init(glsl_version);

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        ImGui::Begin("SND");
        ImGui::Text("miniaudio + Dear ImGui build proof.");
        ImGui::Text("Audio device: %s", device.playback.name);
        ImGui::Text("No editor logic here yet -- this just proves the stack builds.");
        ImGui::End();

        ImGui::Render();
        int display_w, display_h;
        glfwGetFramebufferSize(window, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);
        glClearColor(0.1f, 0.1f, 0.12f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(window);
    }

    ma_device_uninit(&device);

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    glfwDestroyWindow(window);
    glfwTerminate();

    return 0;
}
