cmake_minimum_required(VERSION 3.22)

# The release workflow passes -DVO_DUCKER_VERSION=<tag without the leading v>,
# so the version baked into the plugin (and used by the update checker) always
# matches the GitHub Release it ships in.
set(VO_DUCKER_VERSION "0.2.0" CACHE STRING "Plugin version (major.minor.patch)")

set(VO_DUCKER_GITHUB_REPO "kyle95wm/VO-Ducker")   # owner/repo the update checker reads
set(VO_DUCKER_ACCENT      "0xff4fb3ff")           # blue; the plugin's one accent colour (ARGB)

# Must be set before project() to take effect (after it, the setting is
# ignored for the first configure).
if(APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "Minimum macOS version")
endif()

project(VODucker VERSION ${VO_DUCKER_VERSION} LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(VO_DUCKER_BUILD_AU "Build the Audio Unit as well as the VST3 (macOS only)" ON)
option(VO_DUCKER_BUILD_STANDALONE "Build a standalone app (handy for quick UI testing)" OFF)
option(VO_DUCKER_BUILD_TESTS "Build the DSP test executable" ON)
option(VO_DUCKER_BUILD_SCREENSHOT_TOOL "Build a tool that renders the UI to docs/screenshot.png" OFF)
option(VO_DUCKER_COPY_AFTER_BUILD "Install the plugins into your user plug-in folders after building" OFF)

# ---------------------------------------------------------------------------
# JUCE: use a local checkout if one is given, otherwise fetch a pinned release.
# Keep the tag identical across all kyle95wm plugins (see CONVENTIONS.md).
# ---------------------------------------------------------------------------
set(VO_DUCKER_JUCE_PATH "" CACHE PATH "Path to an existing JUCE checkout (optional)")

if(VO_DUCKER_JUCE_PATH)
    add_subdirectory(${VO_DUCKER_JUCE_PATH} ${CMAKE_BINARY_DIR}/JUCE EXCLUDE_FROM_ALL)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        8.0.17-security
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
    COMPANY_NAME "kyle95wm"            # shared: groups every plugin in one Logic menu
    COMPANY_WEBSITE "https://github.com/${VO_DUCKER_GITHUB_REPO}"
    BUNDLE_ID "com.kyle95wm.voducker"
    PLUGIN_MANUFACTURER_CODE Ky95      # shared by every kyle95wm plugin
    PLUGIN_CODE VoDk
    FORMATS ${VO_DUCKER_FORMATS}
    IS_SYNTH FALSE
    NEEDS_CURL TRUE                    # update checker on Linux
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD ${VO_DUCKER_COPY_AFTER_BUILD}
    VST3_CATEGORIES Fx Dynamics
    AU_MAIN_TYPE kAudioUnitType_Effect
    AU_EXPORT_PREFIX VODuckerAU        # same as JUCE's default for this target
    DESCRIPTION "Audio description ducker: sidechain compressor with up to 1 s lookahead")

target_sources(VODucker PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/UpdateChecker.cpp)

target_include_directories(VODucker PRIVATE Source)

# Read by the shared files (UpdateChecker, BrandLookAndFeel), which stay
# byte-identical across plugins.
set(VO_DUCKER_SHARED_DEFINITIONS
    PLUGIN_GITHUB_REPO="${VO_DUCKER_GITHUB_REPO}"
    BRAND_ACCENT_ARGB=${VO_DUCKER_ACCENT})

target_compile_definitions(VODucker PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    ${VO_DUCKER_SHARED_DEFINITIONS})

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
        JucePlugin_Name="VODucker"
        JucePlugin_VersionString="${PROJECT_VERSION}"
        JucePlugin_Build_Standalone=0
        ${VO_DUCKER_SHARED_DEFINITIONS}
        JUCE_MODAL_LOOPS_PERMITTED=1)   # lets the tool pump the message loop while the update check finishes
    target_link_libraries(VODuckerScreenshot PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
        juce::juce_recommended_config_flags)
endif()
