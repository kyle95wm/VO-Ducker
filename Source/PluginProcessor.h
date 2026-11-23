#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "DuckerEngine.h"
#include "UpdateChecker.h"

namespace ParamIDs
{
    inline constexpr const char* threshold = "threshold";
    inline constexpr const char* attack    = "attack";
    inline constexpr const char* release   = "release";
    inline constexpr const char* hold      = "hold";
    inline constexpr const char* lookahead = "lookahead";
    inline constexpr const char* bypass    = "bypass";
}

class VODuckerProcessor : public juce::AudioProcessor,
                          private juce::Timer
{
public:
    VODuckerProcessor();
    ~VODuckerProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;

    // Float processing only; JUCE converts double-precision audio for us. The using-declaration
    // keeps the base class's double overloads visible (silences GCC's -Woverloaded-virtual).
    using AudioProcessor::processBlock;
    using AudioProcessor::processBlockBypassed;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlockBypassed (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    float getCurrentReductionDb() const noexcept { return engine.getCurrentReductionDb(); }
    bool isSidechainConnected() const noexcept { return sidechainActive.load(); }

    juce::AudioProcessorValueTreeState apvts;
    UpdateChecker updateChecker;

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

private:
    voducker::DuckerParams readParams() const;
    void timerCallback() override; // keeps reported latency in sync with Lookahead (message thread)
    int lookaheadToSamples (float ms) const;

    std::atomic<double> currentSampleRate { 44100.0 };

    voducker::DuckerEngine engine;
    std::atomic<bool> sidechainActive { false };

    std::atomic<float>* thresholdParam = nullptr;
    std::atomic<float>* attackParam    = nullptr;
    std::atomic<float>* releaseParam   = nullptr;
    std::atomic<float>* holdParam      = nullptr;
    std::atomic<float>* lookaheadParam = nullptr;
    // Internal bypass: an ordinary automatable parameter, deliberately NOT returned from
    // getBypassParameter(), so it never touches or follows the DAW's own bypass switch.
    juce::AudioParameterBool* bypassParam = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VODuckerProcessor)
};
