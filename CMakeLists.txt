cmake_minimum_required(VERSION 3.22)

# The release workflow passes -DVO_DUCKER_VERSION=<tag without the leading v>,
# so the version baked into the plugin (and used by the update checker) always
# matches the GitHub Release it ships in.
set(VO_DUCKER_VERSION "0.1.1" CACHE STRING "Plugin version (major.minor.patch)")

project(VODucker VERSION ${VO_DUCKER_VERSION} LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "Minimum macOS version")
endif()

option(VO_DUCKER_BUILD_AU "Build the Audio Unit as well as the VST3 (macOS only)" ON)
option(VO_DUCKER_BUILD_STANDALONE "Build a standalone app (handy for quick UI testing)" OFF)
option(VO_DUCKER_BUILD_TESTS "Build the DSP test executable" ON)
option(VO_DUCKER_BUILD_SCREENSHOT_TOOL "Build a tool that renders the UI to docs/screenshot.png" OFF)

# ---------------------------------------------------------------------------
# JUCE: use a local checkout if one is given, otherwise fetch a pinned release.
# ---------------------------------------------------------------------------
set(VO_DUCKER_JUCE_PATH "" CACHE PATH "Path to an existing JUCE checkout (optional)")

if(VO_DUCKER_JUCE_PATH)
    add_subdirectory(${VO_DUCKER_JUCE_PATH} ${CMAKE_BINARY_DIR}/JUCE EXCLUDE_FROM_ALL)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        8.0.9
        GIT_SHALLOW    TRUE
        GIT_PROGRESS   TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

set(VO_DUCKER_FORMATS VST3)
if(APPLE AND VO_DUCKER_BUILD_AU)
    list(APPEND VO_DUCKER_FORMATS AU)
endif()
if(VO_DUCKER_BUILD_STANDALONE)
    list(APPEND VO_DUCKER_FORMATS Standalone)
endif()

juce_add_plugin(VODucker
    PRODUCT_NAME "VODucker"
    COMPANY_NAME "kyle95wm"
    COMPANY_WEBSITE "https://github.com/kyle95wm/VO-Ducker"
    BUNDLE_ID "com.kyle95wm.voducker"
    PLUGIN_MANUFACTURER_CODE Ky95
    PLUGIN_CODE VoDk
    FORMATS ${VO_DUCKER_FORMATS}
    IS_SYNTH FALSE
    NEEDS_CURL TRUE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_CATEGORIES Fx Dynamics
    AU_MAIN_TYPE kAudioUnitType_Effect
    DESCRIPTION "Audio description ducker: sidechain compressor with up to 1 s lookahead")

target_sources(VODucker PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/UpdateChecker.cpp)

target_include_directories(VODucker PRIVATE Source)

target_compile_definitions(VODucker PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    VO_DUCKER_GITHUB_REPO="kyle95wm/VO-Ducker")

target_link_libraries(VODucker
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# ---------------------------------------------------------------------------
# Headless DSP tests (no plugin host needed): ctest --test-dir build
# ---------------------------------------------------------------------------
if(VO_DUCKER_BUILD_TESTS)
    enable_testing()
    add_executable(DuckerTests Tests/DuckerTests.cpp)
    target_include_directories(DuckerTests PRIVATE Source)
    add_test(NAME DuckerTests COMMAND DuckerTests)
endif()

# ---------------------------------------------------------------------------
# Optional: render the editor to a PNG for the README (see Tools/RenderScreenshot.cpp)
# ---------------------------------------------------------------------------
if(VO_DUCKER_BUILD_SCREENSHOT_TOOL)
    juce_add_console_app(VODuckerScreenshot PRODUCT_NAME "VODuckerScreenshot")
    target_sources(VODuckerScreenshot PRIVATE
        Tools/RenderScreenshot.cpp
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
        Source/UpdateChecker.cpp)
    target_include_directories(VODuckerScreenshot PRIVATE Source)
    target_compile_definitions(VODuckerScreenshot PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_DISPLAY_SPLASH_SCREEN=0
        JucePlugin_Name="VODucker"
        JucePlugin_VersionString="${PROJECT_VERSION}"
        JucePlugin_Build_Standalone=0
        VO_DUCKER_GITHUB_REPO="kyle95wm/VO-Ducker")
    target_link_libraries(VODuckerScreenshot PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
        juce::juce_recommended_config_flags)
endif()
