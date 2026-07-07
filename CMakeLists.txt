cmake_minimum_required(VERSION 3.25)
project(snd LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# Everything may end up inside a .vst3 shared module (Linux needs PIC there;
# macOS/Windows are PIC by default anyway).
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
if(APPLE)
    enable_language(OBJC OBJCXX)
endif()

include(FetchContent)

# ---------------------------------------------------------------------------
# Vendored dependencies -- all pinned to release tags, all unmodified.
# ---------------------------------------------------------------------------

# miniaudio -- audio device I/O + decoding (public domain / MIT-0)
FetchContent_Declare(
    miniaudio
    GIT_REPOSITORY https://github.com/mackron/miniaudio.git
    GIT_TAG 0.11.25
)
FetchContent_MakeAvailable(miniaudio)

# Dear ImGui -- immediate-mode UI (MIT). Plain release, not -docking.
FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG v1.92.8
)
FetchContent_MakeAvailable(imgui)

# GLFW -- cross-platform windowing (zlib)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG 3.4
)
FetchContent_MakeAvailable(glfw)

# nativefiledialog-extended -- native OS file/folder dialogs (zlib)
set(NFD_BUILD_TESTS OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    nfd
    GIT_REPOSITORY https://github.com/btzy/nativefiledialog-extended.git
    GIT_TAG v1.3.0
)
FetchContent_MakeAvailable(nfd)

# libFLAC -- FLAC encoding (BSD). Programs/tests/docs/ogg all off.
set(BUILD_PROGRAMS OFF CACHE BOOL "" FORCE)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(BUILD_CXXLIBS OFF CACHE BOOL "" FORCE)
set(WITH_OGG OFF CACHE BOOL "" FORCE)
set(INSTALL_MANPAGES OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    flac
    GIT_REPOSITORY https://github.com/xiph/flac.git
    GIT_TAG 1.5.0
)
FetchContent_MakeAvailable(flac)

# imgui-knobs -- rotary knob widgets for ImGui (MIT). No release tags; pinned.
FetchContent_Declare(
    imgui_knobs
    GIT_REPOSITORY https://github.com/altschuler/imgui-knobs.git
    GIT_TAG 6b4b5923129c1e8fa4cd30f8c59439643d44f22f
    SOURCE_SUBDIR "cmake-subdir-skipped"
)
FetchContent_MakeAvailable(imgui_knobs)

# PFFFT -- small fast FFT for audio (BSD-like FFTPACK license). No release
# tags upstream, so pinned to a commit. Compiled directly into snd below
# rather than via its own CMake (keeps its test/bench targets out).
FetchContent_Declare(
    pffft
    GIT_REPOSITORY https://github.com/marton78/pffft.git
    GIT_TAG a4b03590cc2a4bea56f9721996e3057835799179
    SOURCE_SUBDIR "cmake-subdir-skipped"
)
FetchContent_MakeAvailable(pffft)

# Steinberg VST3 SDK (MIT) -- used for its hosting utilities (Module,
# PlugProvider, HostProcessData, ParameterChanges) and to build the test
# plugin. Samples/utilities/VSTGUI all disabled; only the library targets
# (base, pluginterfaces, sdk_common, sdk, sdk_hosting) get built.
set(SMTG_ENABLE_VST3_PLUGIN_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SMTG_ENABLE_VST3_HOSTING_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SMTG_ENABLE_VSTGUI_SUPPORT OFF CACHE BOOL "" FORCE)
set(SMTG_ADD_VST3_UTILITIES OFF CACHE BOOL "" FORCE)
set(SMTG_ENABLE_IOS_TARGETS OFF CACHE BOOL "" FORCE)
set(SMTG_CREATE_MODULE_INFO OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    vst3sdk
    GIT_REPOSITORY https://github.com/steinbergmedia/vst3sdk.git
    GIT_TAG v3.8.0_build_66
    GIT_SHALLOW TRUE
    GIT_SUBMODULES "base" "cmake" "pluginterfaces" "public.sdk"
)
FetchContent_MakeAvailable(vst3sdk)

# ---------------------------------------------------------------------------
# The snd library
# ---------------------------------------------------------------------------

set(SND_VST3_HOSTING_EXTRA
    ${vst3sdk_SOURCE_DIR}/public.sdk/source/vst/hosting/plugprovider.cpp
    ${vst3sdk_SOURCE_DIR}/public.sdk/source/common/memorystream.cpp
)
if(APPLE)
    list(APPEND SND_VST3_HOSTING_EXTRA
        ${vst3sdk_SOURCE_DIR}/public.sdk/source/vst/hosting/module_mac.mm)
    set_source_files_properties(
        ${vst3sdk_SOURCE_DIR}/public.sdk/source/vst/hosting/module_mac.mm
        PROPERTIES COMPILE_FLAGS "-fobjc-arc")
elseif(WIN32)
    list(APPEND SND_VST3_HOSTING_EXTRA
        ${vst3sdk_SOURCE_DIR}/public.sdk/source/vst/hosting/module_win32.cpp)
else()
    list(APPEND SND_VST3_HOSTING_EXTRA
        ${vst3sdk_SOURCE_DIR}/public.sdk/source/vst/hosting/module_linux.cpp)
endif()

add_library(snd STATIC
    src/audio.cpp
    src/midi.cpp
    src/ui.cpp
    src/ui_widgets.cpp
    src/dsp.cpp
    src/platform.cpp
    src/state.cpp
    src/plugin_host.cpp
    src/plugin_graph.cpp
    src/plugin_host/vst3/vst3_format.cpp
    ${imgui_knobs_SOURCE_DIR}/imgui-knobs.cpp
    ${pffft_SOURCE_DIR}/src/pffft.c
    ${pffft_SOURCE_DIR}/src/pffft_common.c
    ${SND_VST3_HOSTING_EXTRA}
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
)

if(APPLE)
    target_sources(snd PRIVATE
        src/audio_media_mac.mm
        src/plugin_host/au/au_format.mm
        src/plugin_host/editor_window_mac.mm
        src/ui_mac.mm)
    set_source_files_properties(
        src/plugin_host/editor_window_mac.mm src/audio_media_mac.mm
        PROPERTIES COMPILE_FLAGS "-fobjc-arc")
endif()

target_include_directories(snd
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${miniaudio_SOURCE_DIR}
        ${imgui_SOURCE_DIR}
        ${imgui_SOURCE_DIR}/backends
        ${imgui_knobs_SOURCE_DIR}
    PRIVATE
        ${pffft_SOURCE_DIR}/src
        ${pffft_SOURCE_DIR}/include/pffft
)

target_link_libraries(snd PUBLIC glfw sdk_hosting nfd FLAC)

if(APPLE)
    target_link_libraries(snd PUBLIC
        "-framework Cocoa"
        "-framework OpenGL"
        "-framework CoreAudio"
        "-framework CoreFoundation"
        "-framework AudioToolbox"
        "-framework AudioUnit"
        "-framework CoreAudioKit"
        "-framework CoreMIDI"
        "-framework AVFoundation"
        "-framework CoreMedia")
elseif(WIN32)
    target_link_libraries(snd PUBLIC opengl32)
else()
    find_package(OpenGL REQUIRED)
    find_package(Threads REQUIRED)
    target_link_libraries(snd PUBLIC OpenGL::GL Threads::Threads m dl)
endif()

# ---------------------------------------------------------------------------
# TestGain -- minimal known-good VST3 plugin owned by SND, used by --selftest
# so tests never depend on what happens to be installed on a machine.
# ---------------------------------------------------------------------------

add_library(testgain MODULE
    test/plugins/testgain/testgain.cpp
    ${vst3sdk_SOURCE_DIR}/public.sdk/source/vst/vstsinglecomponenteffect.cpp
)
if(APPLE)
    target_sources(testgain PRIVATE ${vst3sdk_SOURCE_DIR}/public.sdk/source/main/macmain.cpp)
elseif(WIN32)
    target_sources(testgain PRIVATE ${vst3sdk_SOURCE_DIR}/public.sdk/source/main/dllmain.cpp)
else()
    target_sources(testgain PRIVATE ${vst3sdk_SOURCE_DIR}/public.sdk/source/main/linuxmain.cpp)
endif()
target_link_libraries(testgain PRIVATE sdk)
if(APPLE)
    target_link_libraries(testgain PRIVATE "-framework CoreFoundation")
endif()

# Assemble the .vst3 bundle layout by hand (no SDK cmake helpers -- keeps the
# validator and module-info tooling out of the build).
set(SND_TESTGAIN_BUNDLE ${CMAKE_BINARY_DIR}/TestGain.vst3)
if(APPLE)
    set_target_properties(testgain PROPERTIES
        OUTPUT_NAME TestGain
        PREFIX ""
        SUFFIX ""
        LIBRARY_OUTPUT_DIRECTORY ${SND_TESTGAIN_BUNDLE}/Contents/MacOS)
    file(WRITE ${SND_TESTGAIN_BUNDLE}/Contents/Info.plist
"<?xml version=\"1.0\" encoding=\"UTF-8\"?>
<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">
<plist version=\"1.0\">
<dict>
    <key>CFBundleExecutable</key><string>TestGain</string>
    <key>CFBundleIdentifier</key><string>com.snd.testgain</string>
    <key>CFBundleName</key><string>TestGain</string>
    <key>CFBundlePackageType</key><string>BNDL</string>
    <key>CFBundleVersion</key><string>0.1.0</string>
</dict>
</plist>
")
elseif(WIN32)
    set_target_properties(testgain PROPERTIES
        OUTPUT_NAME TestGain
        PREFIX ""
        SUFFIX ".vst3"
        LIBRARY_OUTPUT_DIRECTORY ${SND_TESTGAIN_BUNDLE}/Contents/x86_64-win)
else()
    set_target_properties(testgain PROPERTIES
        OUTPUT_NAME TestGain
        PREFIX ""
        SUFFIX ".so"
        LIBRARY_OUTPUT_DIRECTORY ${SND_TESTGAIN_BUNDLE}/Contents/${CMAKE_SYSTEM_PROCESSOR}-linux)
endif()

# ---------------------------------------------------------------------------
# Plugin client SDK: write a Processor once, ship VST3 + standalone
# ---------------------------------------------------------------------------

set(SND_ROOT_DIR ${CMAKE_CURRENT_SOURCE_DIR})
include(cmake/SndPlugin.cmake)

snd_add_plugin(demofilter
    SOURCES test/plugins/demofilter/demofilter.cpp
    OUTPUT_NAME DemoFilter
    STANDALONE)

# ---------------------------------------------------------------------------
# Example app (window + audio device + --selftest)
# ---------------------------------------------------------------------------

add_executable(snd-example examples/snd-example/main.cpp)
target_link_libraries(snd-example PRIVATE snd)
add_dependencies(snd-example testgain demofilter)

target_compile_definitions(snd-example PRIVATE
    SND_TEST_ASSETS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/test/assets"
    SND_TEST_VST3_PATH="${SND_TESTGAIN_BUNDLE}"
    SND_TEST_DEMOFILTER_PATH="${CMAKE_BINARY_DIR}/DemoFilter.vst3"
)
