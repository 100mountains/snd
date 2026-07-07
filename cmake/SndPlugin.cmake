# snd_add_plugin(<target>
#     SOURCES <files...>          # define sndPluginSpec()/sndCreateProcessor()
#     [OUTPUT_NAME <name>]        # bundle/product name (default: target)
#     [STANDALONE]                # also build a <target>-app executable
# )
#
# Builds a self-contained VST3 bundle at ${CMAKE_BINARY_DIR}/<name>.vst3 from
# an snd::plugin::client Processor. Everything static inside the bundle
# (imgui, snd widgets), so the plugin never clashes with its host.

function(snd_add_plugin target)
    cmake_parse_arguments(ARG "STANDALONE" "OUTPUT_NAME" "SOURCES" ${ARGN})
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
    elseif(WIN32)
        target_sources(${target} PRIVATE
            ${vst3sdk_SOURCE_DIR}/public.sdk/source/main/dllmain.cpp)
    else()
        target_sources(${target} PRIVATE
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
            LIBRARY_OUTPUT_DIRECTORY ${bundle}/Contents/x86_64-win)
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
endfunction()
