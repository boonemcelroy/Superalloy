# ==============================================================================
#  Superalloy — CMakeLists.txt
#  Windows (VS2026), macOS (Xcode) and Linux. VST3 always; AAX when the AAX SDK
#  path is supplied:  -DWAVRELY_AAX_SDK=C:/SDKs/aax-sdk-2-x-x
#  JUCE: uses -DWAVRELY_JUCE_DIR=<path to JUCE checkout> if given, otherwise
#  fetches JUCE 9.0.6 from GitHub.
# ==============================================================================
cmake_minimum_required (VERSION 3.22)
project (Superalloy VERSION 6.2.0 LANGUAGES C CXX)

set (CMAKE_CXX_STANDARD 20)
set (CMAKE_CXX_STANDARD_REQUIRED ON)
set (CMAKE_CXX_EXTENSIONS OFF)
if (APPLE)
    set (CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "" FORCE)
    set (CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "" FORCE)
endif ()

set (WAVRELY_JUCE_DIR "" CACHE PATH "Path to a JUCE checkout (optional)")
set (WAVRELY_AAX_SDK  "" CACHE PATH "Path to the Avid AAX SDK (optional)")
option (WAVRELY_BUILD_VERIFY "Build the verifySuperalloy harness" ON)

if (WAVRELY_JUCE_DIR)
    add_subdirectory ("${WAVRELY_JUCE_DIR}" JUCE EXCLUDE_FROM_ALL)
else ()
    include (FetchContent)
    FetchContent_Declare (JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        9.0.6
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable (JUCE)
endif ()

set (WAVRELY_FORMATS VST3)
if (WAVRELY_AAX_SDK)
    juce_set_aax_sdk_path ("${WAVRELY_AAX_SDK}")
    list (APPEND WAVRELY_FORMATS AAX)
endif ()

juce_add_plugin (Superalloy
    COMPANY_NAME               "wavRely Mastering Systems"
    COMPANY_WEBSITE            ""
    BUNDLE_ID                  com.wavrely.superalloy
    PLUGIN_MANUFACTURER_CODE   Wvry
    PLUGIN_CODE                Salo
    PRODUCT_NAME               "Superalloy"
    VERSION                    6.2.0
    FORMATS                    ${WAVRELY_FORMATS}
    IS_SYNTH                   FALSE
    NEEDS_MIDI_INPUT           FALSE
    NEEDS_MIDI_OUTPUT          FALSE
    IS_MIDI_EFFECT             FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD    FALSE
    VST3_CATEGORIES            Fx Mastering
    AAX_CATEGORY               AAX_ePlugInCategory_Harmonic)

target_sources (Superalloy PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/TransformerEngine.cpp)

target_include_directories (Superalloy PRIVATE Source)

target_compile_definitions (Superalloy PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries (Superalloy
    PRIVATE
        juce::juce_audio_utils
        juce::juce_audio_processors
        juce::juce_gui_basics
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# Bit-exact, IEEE-strict maths: never fast-math in the DSP.
if (MSVC)
    target_compile_options (Superalloy PRIVATE /fp:precise /utf-8)
else ()
    target_compile_options (Superalloy PRIVATE -fno-fast-math -ffp-contract=off)
endif ()

# ------------------------------------------------------------------ harness
if (WAVRELY_BUILD_VERIFY)
    add_executable (verifySuperalloy tools/verifySuperalloy.cpp Source/TransformerEngine.cpp)
    target_include_directories (verifySuperalloy PRIVATE Source)
    if (MSVC)
        target_compile_options (verifySuperalloy PRIVATE /O2 /fp:precise /utf-8)
    else ()
        target_compile_options (verifySuperalloy PRIVATE -O3 -fno-fast-math -ffp-contract=off)
    endif ()
endif ()

# ------------------------------------------------------------- GUI snapshot
option (WAVRELY_BUILD_SNAPSHOT "Build the off-screen GUI snapshot tool" OFF)
if (WAVRELY_BUILD_SNAPSHOT)
    juce_add_console_app (snapshotSuperalloy PRODUCT_NAME "snapshotSuperalloy")
    target_sources (snapshotSuperalloy PRIVATE tools/snapshotSuperalloy.cpp Source/PluginProcessor.cpp Source/PluginEditor.cpp Source/TransformerEngine.cpp)
    target_include_directories (snapshotSuperalloy PRIVATE Source)
    target_compile_definitions (snapshotSuperalloy PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JucePlugin_Build_Standalone=0)
    target_link_libraries (snapshotSuperalloy PRIVATE juce::juce_audio_utils juce::juce_audio_processors juce::juce_gui_basics)
endif ()

# ------------------------------------------------------- frequency response
option (WAVRELY_BUILD_MEASURE "Build the frequency-response measurement tool" OFF)
if (WAVRELY_BUILD_MEASURE)
    juce_add_console_app (analyzeSuperalloy PRODUCT_NAME "analyzeSuperalloy")
    target_sources (analyzeSuperalloy PRIVATE tools/analyzeSuperalloy.cpp Source/PluginProcessor.cpp Source/PluginEditor.cpp Source/TransformerEngine.cpp)
    target_include_directories (analyzeSuperalloy PRIVATE Source)
    target_compile_definitions (analyzeSuperalloy PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JucePlugin_Build_Standalone=0)
    target_link_libraries (analyzeSuperalloy PRIVATE juce::juce_audio_utils juce::juce_audio_processors juce::juce_gui_basics juce::juce_dsp)
    if (MSVC)
        target_compile_options (analyzeSuperalloy PRIVATE /fp:precise /utf-8)
    else ()
        target_compile_options (analyzeSuperalloy PRIVATE -fno-fast-math -ffp-contract=off)
    endif ()

    juce_add_console_app (measureSuperalloy PRODUCT_NAME "measureSuperalloy")
    target_sources (measureSuperalloy PRIVATE tools/measureSuperalloy.cpp Source/PluginProcessor.cpp Source/PluginEditor.cpp Source/TransformerEngine.cpp)
    target_include_directories (measureSuperalloy PRIVATE Source)
    target_compile_definitions (measureSuperalloy PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JucePlugin_Build_Standalone=0)
    target_link_libraries (measureSuperalloy PRIVATE juce::juce_audio_utils juce::juce_audio_processors juce::juce_gui_basics)
    if (MSVC)
        target_compile_options (measureSuperalloy PRIVATE /fp:precise /utf-8)
    else ()
        target_compile_options (measureSuperalloy PRIVATE -fno-fast-math -ffp-contract=off)
    endif ()
endif ()
