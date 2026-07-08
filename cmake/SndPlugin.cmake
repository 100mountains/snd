# snd_add_plugin(<target>
#     SOURCES <files...>          # define sndPluginSpec()/sndCreateProcessor()
#     [OUTPUT_NAME <name>]        # bundle/product name (default: target)
#     [STANDALONE]                # also build a <target>-app executable
# )
#
# Builds a self-contained VST3 bundle at ${CMAKE_BINARY_DIR}/<name>.vst3 from
# an snd::plugin::client Processor. Everything static inside the bundle
# (imgui, snd widgets), so the plugin never clashes with its host.

# Optional AU output (macOS): pass AU_TYPE (aufx|aumu), AU_SUBTYPE and
# AU_MANUFACTURER (4-char codes; manufacturer must contain an uppercase
# letter). Produces <name>.component wrapping the VST3 via the SDK's
# auwrapper built against Apple's AudioUnitSDK -- no Xcode generator, no
# CoreAudio SDK download.
function(snd_add_plugin target)
    cmake_parse_arguments(ARG "STANDALONE"
        "OUTPUT_NAME;AU_TYPE;AU_SUBTYPE;AU_MANUFACTURER" "SOURCES" ${ARGN})

    # Callers outside the SND tree don't see the FetchContent dirs SND
    # populated in its own scope; recover them from the global registry.
    if(NOT vst3sdk_SOURCE_DIR)
        FetchContent_GetProperties(vst3sdk)
    endif()
    if(APPLE AND NOT audiounitsdk_SOURCE_DIR)
        FetchContent_GetProperties(audiounitsdk)
    endif()
    if(NOT ARG_OUTPUT_NAME)
        set(ARG_OUTPUT_NAME ${target})
    endif()

    add_library(${target} MODULE
        ${ARG_SOURCES}
        ${SND_ROOT_DIR}/src/plugin_client/vst3_wrapper.cpp
        ${vst3sdk_SOURCE_DIR}/public.sdk/source/vst/vstsinglecomponenteffect.cpp
    )
    if(APPLE)
        target_sources(${target} PRIVATE
            ${SND_ROOT_DIR}/src/plugin_client/editor_view_mac.mm
            ${vst3sdk_SOURCE_DIR}/public.sdk/source/main/macmain.cpp)
        set_source_files_properties(
            ${SND_ROOT_DIR}/src/plugin_client/editor_view_mac.mm
            PROPERTIES COMPILE_FLAGS "-fobjc-arc")
        # ObjC classes are process-global: two SND plugins in one host would
        # collide (the Waves disease). Rename the view class per plugin.
        # (target_compile_definitions, NOT source-file props -- those are
        # file-scoped and the last plugin would win for everyone.)
        target_compile_definitions(${target} PRIVATE
            SndPluginGLView=SndPluginGLView_${target})
    elseif(WIN32)
        target_sources(${target} PRIVATE
            ${SND_ROOT_DIR}/src/plugin_client/editor_view_win.cpp
            ${vst3sdk_SOURCE_DIR}/public.sdk/source/main/dllmain.cpp)
    else()
        target_sources(${target} PRIVATE
            ${SND_ROOT_DIR}/src/plugin_client/editor_view_x11.cpp
            ${vst3sdk_SOURCE_DIR}/public.sdk/source/main/linuxmain.cpp)
    endif()

    target_link_libraries(${target} PRIVATE snd sdk)
    if(APPLE)
        target_link_libraries(${target} PRIVATE "-framework QuartzCore")
    endif()

    set(bundle ${CMAKE_BINARY_DIR}/${ARG_OUTPUT_NAME}.vst3)
    if(APPLE)
        set_target_properties(${target} PROPERTIES
            OUTPUT_NAME ${ARG_OUTPUT_NAME}
            PREFIX ""
            SUFFIX ""
            LIBRARY_OUTPUT_DIRECTORY ${bundle}/Contents/MacOS)
        file(WRITE ${bundle}/Contents/Info.plist
"<?xml version=\"1.0\" encoding=\"UTF-8\"?>
<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">
<plist version=\"1.0\">
<dict>
    <key>CFBundleExecutable</key><string>${ARG_OUTPUT_NAME}</string>
    <key>CFBundleIdentifier</key><string>com.snd.plugin.${target}</string>
    <key>CFBundleName</key><string>${ARG_OUTPUT_NAME}</string>
    <key>CFBundlePackageType</key><string>BNDL</string>
    <key>CFBundleVersion</key><string>1.0.0</string>
</dict>
</plist>
")
    elseif(WIN32)
        set_target_properties(${target} PROPERTIES
            OUTPUT_NAME ${ARG_OUTPUT_NAME}
            PREFIX ""
            SUFFIX ".vst3"
            LIBRARY_OUTPUT_DIRECTORY ${bundle}/Contents/x86_64-win
            RUNTIME_OUTPUT_DIRECTORY ${bundle}/Contents/x86_64-win)
        # Multi-config generators (VS) append a per-config subdir by default,
        # which would break the .vst3 bundle layout. Pin every config to the
        # bundle so the DLL lands at Contents/x86_64-win/<name>.vst3.
        foreach(_cfg DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
            set_target_properties(${target} PROPERTIES
                LIBRARY_OUTPUT_DIRECTORY_${_cfg} ${bundle}/Contents/x86_64-win
                RUNTIME_OUTPUT_DIRECTORY_${_cfg} ${bundle}/Contents/x86_64-win)
        endforeach()
    else()
        set_target_properties(${target} PROPERTIES
            OUTPUT_NAME ${ARG_OUTPUT_NAME}
            PREFIX ""
            SUFFIX ".so"
            LIBRARY_OUTPUT_DIRECTORY ${bundle}/Contents/${CMAKE_SYSTEM_PROCESSOR}-linux)
    endif()

    set(${target}_VST3_BUNDLE ${bundle} PARENT_SCOPE)

    if(ARG_STANDALONE)
        add_executable(${target}-app
            ${ARG_SOURCES}
            ${SND_ROOT_DIR}/src/plugin_client/standalone_main.cpp)
        target_link_libraries(${target}-app PRIVATE snd)
    endif()

    if(APPLE AND ARG_AU_SUBTYPE)
        set(auTarget ${target}-au)
        file(GLOB AUSDK_SOURCES ${audiounitsdk_SOURCE_DIR}/src/AudioUnitSDK/*.cpp)
        add_library(${auTarget} MODULE
            ${vst3sdk_SOURCE_DIR}/public.sdk/source/vst/auwrapper/auwrapper.mm
            ${vst3sdk_SOURCE_DIR}/public.sdk/source/vst/auwrapper/aucocoaview.mm
            ${vst3sdk_SOURCE_DIR}/public.sdk/source/vst/auwrapper/NSDataIBStream.mm
            ${vst3sdk_SOURCE_DIR}/public.sdk/source/vst/auwrapper/usediids.cpp
            ${AUSDK_SOURCES})
        target_compile_features(${auTarget} PRIVATE cxx_std_20) # AudioUnitSDK needs it
        target_compile_definitions(${auTarget} PRIVATE
            SMTG_AUWRAPPER_USES_AUSDK=1
            SMTG_AUCocoaUIBase_CLASS_NAME=SNDAUCocoaUI_${target}
            CA_USE_AUDIO_PLUGIN_ONLY=0)
        target_include_directories(${auTarget} PRIVATE
            ${audiounitsdk_SOURCE_DIR}/include
            ${vst3sdk_SOURCE_DIR})
        target_link_libraries(${auTarget} PRIVATE sdk_hosting
            "-framework AudioUnit" "-framework AudioToolbox"
            "-framework CoreAudio" "-framework CoreMIDI"
            "-framework CoreFoundation" "-framework Cocoa"
            "-framework QuartzCore")
        add_dependencies(${auTarget} ${target})

        set(component ${CMAKE_BINARY_DIR}/${ARG_OUTPUT_NAME}.component)
        set_target_properties(${auTarget} PROPERTIES
            OUTPUT_NAME ${ARG_OUTPUT_NAME}
            PREFIX ""
            SUFFIX ""
            LIBRARY_OUTPUT_DIRECTORY ${component}/Contents/MacOS)
        if(NOT ARG_AU_TYPE)
            set(ARG_AU_TYPE "aufx")
        endif()
        file(WRITE ${component}/Contents/Info.plist
"<?xml version=\"1.0\" encoding=\"UTF-8\"?>
<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">
<plist version=\"1.0\">
<dict>
    <key>CFBundleExecutable</key><string>${ARG_OUTPUT_NAME}</string>
    <key>CFBundleIdentifier</key><string>com.snd.au.${target}</string>
    <key>CFBundleName</key><string>${ARG_OUTPUT_NAME}</string>
    <key>CFBundlePackageType</key><string>BNDL</string>
    <key>CFBundleVersion</key><string>1.0.0</string>
    <key>AudioComponents</key>
    <array>
        <dict>
            <key>factoryFunction</key><string>AUWrapperFactory</string>
            <key>description</key><string>${ARG_OUTPUT_NAME}</string>
            <key>manufacturer</key><string>${ARG_AU_MANUFACTURER}</string>
            <key>name</key><string>SND: ${ARG_OUTPUT_NAME}</string>
            <key>subtype</key><string>${ARG_AU_SUBTYPE}</string>
            <key>type</key><string>${ARG_AU_TYPE}</string>
            <key>version</key><integer>65536</integer>
            <key>sandboxSafe</key><true/>
        </dict>
    </array>
</dict>
</plist>
")
        # the wrapper loads Resources/plugin.vst3 at runtime
        add_custom_command(TARGET ${auTarget} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                ${CMAKE_BINARY_DIR}/${ARG_OUTPUT_NAME}.vst3
                ${component}/Contents/Resources/plugin.vst3)
    endif()
endfunction()
