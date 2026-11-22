#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"

class DuckerLookAndFeel : public juce::LookAndFeel_V4
{
public:
    DuckerLookAndFeel();
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos,
                           float startAngle, float endAngle, juce::Slider&) override;
};

class ReductionMeter : public juce::Component
{
public:
    void setReductionDb (float db);
    void paint (juce::Graphics&) override;

private:
    float currentDb = 0.0f;
    float peakDb = 0.0f;
    int peakHoldFrames = 0;
};

class VODuckerEditor : public juce::AudioProcessorEditor,
                       private juce::Timer
{
public:
    explicit VODuckerEditor (VODuckerProcessor&);
    ~VODuckerEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void refreshUpdateUi();

    struct Knob
    {
        juce::Slider slider;
        juce::Label label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };

    void setupKnob (Knob&, const char* paramId, const juce::String& name);

    VODuckerProcessor& proc;
    DuckerLookAndFeel lnf;

    Knob threshold, attack, release, hold, lookahead;
    ReductionMeter meter;

    juce::Label infoLabel, sidechainLabel, updateLabel;
    juce::TextButton updateButton { "Check for updates" };
    juce::ToggleButton autoCheckToggle { "Check automatically" };
    juce::String pendingReleaseUrl;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VODuckerEditor)
};
