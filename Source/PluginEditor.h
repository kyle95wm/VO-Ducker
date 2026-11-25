#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "BrandLookAndFeel.h"
#include "UpdateFooter.h"

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
    void refreshPresetUi();

    struct Knob
    {
        juce::Slider slider;
        juce::Label label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };

    void setupKnob (Knob&, const char* paramId, const juce::String& name);

    VODuckerProcessor& proc;
    brand::LookAndFeel lnf;

    Knob threshold, attack, release, hold, lookahead;
    ReductionMeter meter;

    juce::Label presetLabel;
    juce::ComboBox presetBox;
    juce::TextButton prevPresetButton { "<" }, nextPresetButton { ">" };

    juce::Label infoLabel, sidechainLabel;
    juce::ToggleButton bypassButton { "Bypass" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> bypassAttachment;

    UpdateFooter footer;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VODuckerEditor)
};
