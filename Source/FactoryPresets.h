#pragma once

// Factory presets for VODucker. Framework-free so Tests/DuckerTests.cpp can check
// every value against the parameter ranges.
//
// The list is exposed to hosts as the plugin's programs (Logic shows them as factory
// presets) and in the editor's Preset menu. Entry 0 holds the parameter defaults.
// Threshold is deliberately not part of a preset: it depends on the narration's level,
// so it is set separately and loading a preset leaves it where it is.
// Only ever APPEND to this list: hosts may save the program index in sessions.

#include <cstddef>

namespace voducker
{

struct FactoryPreset
{
    const char* name;
    float attackMs;
    float releaseMs;
    float holdMs;
    float lookaheadMs;
};

inline constexpr FactoryPreset kFactoryPresets[] =
{
    //  name                     attack  release   hold  lookahead
    { "Default",                 15.0f,  570.4f, 500.0f,    0.0f },
    { "AD Natural",              30.0f,  800.0f, 500.0f,  150.0f },
    { "AD Early Duck",           60.0f,  700.0f, 500.0f,  400.0f },
    { "AD Tight",                 5.0f,  250.0f, 200.0f,   50.0f },
    { "Slow Recovery",           20.0f, 1200.0f, 500.0f,  250.0f },
    { "Zero Latency",             3.0f,  500.0f, 500.0f,    0.0f },
};

inline constexpr int kNumFactoryPresets = (int) (sizeof (kFactoryPresets) / sizeof (kFactoryPresets[0]));

} // namespace voducker
