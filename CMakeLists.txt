cmake_minimum_required(VERSION 3.22)

# Build a universal (Apple Silicon + Intel) binary by default on macOS
if(APPLE AND NOT CMAKE_OSX_ARCHITECTURES)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "macOS architectures")
endif()
set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "Minimum macOS version")

project(PEPPER VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---------------------------------------------------------------------------
# JUCE: use a local checkout if you have one (-DJUCE_PATH=/path/to/JUCE),
# otherwise it is downloaded automatically.
# ---------------------------------------------------------------------------
set(JUCE_PATH "" CACHE PATH "Path to a local JUCE checkout (optional)")

if(JUCE_PATH)
    add_subdirectory(${JUCE_PATH} ${CMAKE_BINARY_DIR}/JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        8.0.10
        GIT_SHALLOW    ON)
    FetchContent_MakeAvailable(JUCE)
endif()

# ---------------------------------------------------------------------------
# Plug-in formats. AAX is what Pro Tools loads; it is built from the AAX SDK
# that ships inside JUCE. Turn it off with -DPEPPER_BUILD_AAX=OFF.
# ---------------------------------------------------------------------------
option(PEPPER_BUILD_AAX "Build the AAX (Pro Tools) format" ON)
option(PEPPER_COPY_AFTER_BUILD "Install the plug-ins to the system folders after building" OFF)

set(PEPPER_FORMATS VST3 Standalone)
if(APPLE)
    list(APPEND PEPPER_FORMATS AU)
endif()
if(PEPPER_BUILD_AAX AND (APPLE OR WIN32))
    list(APPEND PEPPER_FORMATS AAX)
endif()

juce_add_plugin(Pepper
    PRODUCT_NAME             "PEPPER"
    COMPANY_NAME             "Red Hot"
    BUNDLE_ID                "com.redhot.pepper"
    PLUGIN_MANUFACTURER_CODE Rhot
    PLUGIN_CODE              Ppr1
    FORMATS                  ${PEPPER_FORMATS}
    IS_SYNTH                 FALSE
    NEEDS_MIDI_INPUT         FALSE
    NEEDS_MIDI_OUTPUT        FALSE
    IS_MIDI_EFFECT           FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    VST3_CATEGORIES          Fx Distortion
    AU_MAIN_TYPE             kAudioUnitType_Effect
    AAX_CATEGORY             AAX_ePlugInCategory_Harmonic
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "PEPPER needs your audio input so you can play your guitar through it."
    COPY_PLUGIN_AFTER_BUILD  ${PEPPER_COPY_AFTER_BUILD})

target_sources(Pepper PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/Presets.cpp
    Source/dsp/PepperEngine.cpp
    Source/gui/PepperLookAndFeel.cpp
    Source/gui/PepperSelector.cpp)

target_include_directories(Pepper PRIVATE Source)

target_compile_definitions(Pepper PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(Pepper
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# ---------------------------------------------------------------------------
# Offline test harness (renders test riffs through every pepper and checks
# levels, stability and aliasing). Not needed to build the plug-in.
# ---------------------------------------------------------------------------
option(PEPPER_BUILD_TESTS "Build the offline DSP test harness" OFF)
if(PEPPER_BUILD_TESTS)
    add_subdirectory(tests)
endif()
