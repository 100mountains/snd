cmake_minimum_required(VERSION 3.22)
project(app-template LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

# miniaudio -- single-header audio I/O + decoding (public domain / MIT-0)
# Pinned to a release tag, not a floating branch, so every build uses the same
# code until someone deliberately bumps this.
FetchContent_Declare(
    miniaudio
    GIT_REPOSITORY https://github.com/mackron/miniaudio.git
    GIT_TAG 0.11.25
)
FetchContent_MakeAvailable(miniaudio)

# Dear ImGui -- immediate-mode UI (MIT). Pinned (plain release, not -docking).
FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG v1.92.8
)
FetchContent_MakeAvailable(imgui)

# GLFW -- cross-platform windowing (zlib license). Pinned.
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

add_executable(app-template
    main.cpp
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
)

target_include_directories(app-template PRIVATE
    ${miniaudio_SOURCE_DIR}
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
)

# Lets --selftest find test/assets/tone.wav regardless of the build directory
# or working directory it's run from.
target_compile_definitions(app-template PRIVATE
    SND_TEST_ASSETS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/test/assets"
)

target_link_libraries(app-template PRIVATE glfw)

if(APPLE)
    target_link_libraries(app-template PRIVATE
        "-framework OpenGL"
        "-framework CoreAudio"
        "-framework CoreFoundation"
        "-framework AudioToolbox"
        "-framework AudioUnit")
elseif(WIN32)
    target_link_libraries(app-template PRIVATE opengl32)
else()
    find_package(OpenGL REQUIRED)
    find_package(Threads REQUIRED)
    target_link_libraries(app-template PRIVATE OpenGL::GL Threads::Threads m dl)
endif()
