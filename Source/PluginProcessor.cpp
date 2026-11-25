#include "PluginProcessor.h"
#include "PluginEditor.h"

using voducker::DuckerEngine;

namespace
{
    const juce::Identifier presetProperty { "preset" };
}

namespace
{
    juce::NormalisableRange<float> skewedRange (float min, float max, float centre, float step = 0.01f)
    {
        juce::NormalisableRange<float> r (min, max, step);
        r.setSkewForCentre (centre);
        return r;
    }

    juce::String msToText (float v, int)
    {
        return v >= 1000.0f ? juce::String (v / 1000.0f, 2) + " s"
                            : juce::String (v, 1) + " ms";
    }

    float textToMs (const juce::String& t)
    {
        const auto v = t.getFloatValue();
        return t.containsIgnoreCase ("ms") ? v : (t.trim().endsWithIgnoreCase ("s") ? v * 1000.0f : v);
    }
}

juce::AudioProcessorValueTreeState::ParameterLayout VODuckerProcessor::createLayout()
{
    using P = juce::AudioParameterFloat;
    using Attr = juce::AudioParameterFloatAttributes;

    // No separate unit labels: the value text already includes the unit, and some hosts'
    // generic UIs append the label to the raw 0..1 value (showing "1ms" for 1000 ms).
    const auto dbAttr = Attr().withStringFromValueFunction ([] (float v, int) { return juce::String (v, 2) + " dB"; })
                              .withValueFromStringFunction ([] (const juce::String& t) { return t.getFloatValue(); });
    const auto msAttr = Attr().withStringFromValueFunction (msToText)
                              .withValueFromStringFunction (textToMs);

    // Apart from Threshold, the defaults are the "Default" factory preset (entry 0).
    const auto& d = voducker::kFactoryPresets[0];

    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    layout.add (std::make_unique<P> (juce::ParameterID { ParamIDs::threshold, 1 }, "Threshold",
                                     juce::NormalisableRange<float> (-60.0f, 0.0f, 0.01f), -21.24f, dbAttr));
    layout.add (std::make_unique<P> (juce::ParameterID { ParamIDs::attack, 1 }, "Attack",
                                     skewedRange (1.0f, 250.0f, 30.0f), d.attackMs, msAttr));
    layout.add (std::make_unique<P> (juce::ParameterID { ParamIDs::release, 1 }, "Release",
                                     skewedRange (1.0f, 5000.0f, 300.0f), d.releaseMs, msAttr));
    layout.add (std::make_unique<P> (juce::ParameterID { ParamIDs::hold, 1 }, "Hold",
                                     skewedRange (0.0f, DuckerEngine::kMaxHoldMs, 100.0f), d.holdMs, msAttr));
    layout.add (std::make_unique<P> (juce::ParameterID { ParamIDs::lookahead, 1 }, "Lookahead",
                                     skewedRange (0.0f, DuckerEngine::kMaxLookaheadMs, 150.0f), d.lookaheadMs, msAttr));
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { ParamIDs::bypass, 1 }, "Bypass", false));
    return layout;
}

VODuckerProcessor::VODuckerProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",     juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output",    juce::AudioChannelSet::stereo(), true)
                          .withInput  ("Sidechain", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "VODucker", createLayout())
{
    thresholdParam = apvts.getRawParameterValue (ParamIDs::threshold);
    attackParam    = apvts.getRawParameterValue (ParamIDs::attack);
    releaseParam   = apvts.getRawParameterValue (ParamIDs::release);
    holdParam      = apvts.getRawParameterValue (ParamIDs::hold);
    lookaheadParam = apvts.getRawParameterValue (ParamIDs::lookahead);
    bypassParam    = dynamic_cast<juce::AudioParameterBool*> (apvts.getParameter (ParamIDs::bypass));
    jassert (bypassParam != nullptr);

    // A new instance starts on the "Default" preset. Sessions saved before presets existed
    // have no preset property, so they load as "Custom".
    apvts.state.setProperty (presetProperty, 0, nullptr);

    startTimerHz (10);
}

bool VODuckerProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto mainIn  = layouts.getMainInputChannelSet();
    const auto mainOut = layouts.getMainOutputChannelSet();

    if (mainIn != mainOut)
        return false;
    if (mainOut != juce::AudioChannelSet::mono() && mainOut != juce::AudioChannelSet::stereo())
        return false;

    if (layouts.inputBuses.size() > 1)
    {
        const auto sc = layouts.getChannelSet (true, 1);
        if (! sc.isDisabled() && sc != juce::AudioChannelSet::mono() && sc != juce::AudioChannelSet::stereo())
            return false;
    }
    return true;
}

int VODuckerProcessor::lookaheadToSamples (float ms) const
{
    return (int) std::lround (ms * 0.001 * currentSampleRate.load());
}

voducker::DuckerParams VODuckerProcessor::readParams() const
{
    voducker::DuckerParams p;
    p.thresholdDb = thresholdParam->load();
    p.attackMs    = attackParam->load();
    p.releaseMs   = releaseParam->load();
    p.holdMs      = holdParam->load();
    p.lookaheadMs = lookaheadParam->load();
    return p;
}

void VODuckerProcessor::prepareToPlay (double sampleRate, int)
{
    currentSampleRate = sampleRate;
    engine.prepare (sampleRate, getMainBusNumOutputChannels());
    engine.setParams (readParams(), true);
    engine.setBypassed (bypassParam->get());
    engine.reset(); // start fully in the current bypass state, no fade
    setLatencySamples (engine.getLatencySamples());
}

void VODuckerProcessor::timerCallback()
{
    // Hosts expect latency changes to be reported from the message thread.
    const int wanted = lookaheadToSamples (lookaheadParam->load());
    if (wanted != getLatencySamples())
        setLatencySamples (wanted);
}

void VODuckerProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    engine.setParams (readParams());
    engine.setBypassed (bypassParam->get());

    auto mainBus = getBusBuffer (buffer, true, 0);
    const int numSamples = buffer.getNumSamples();

    const float* keyPtrs[2] = { nullptr, nullptr };
    int numKey = 0;

    if (getBusCount (true) > 1)
    {
        if (auto* scBus = getBus (true, 1); scBus != nullptr && scBus->isEnabled())
        {
            auto sc = getBusBuffer (buffer, true, 1);
            numKey = juce::jmin (2, sc.getNumChannels());
            for (int c = 0; c < numKey; ++c)
                keyPtrs[c] = sc.getReadPointer (c);
        }
    }
    sidechainActive = numKey > 0;

    // Clear any output channels beyond the main input (shouldn't happen with supported layouts).
    for (int c = getMainBusNumInputChannels(); c < getTotalNumOutputChannels(); ++c)
        buffer.clear (c, 0, numSamples);

    engine.process (mainBus.getArrayOfWritePointers(), mainBus.getNumChannels(),
                    keyPtrs, numKey, numSamples);
}

void VODuckerProcessor::processBlockBypassed (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    // The DAW's own bypass (separate from the internal Bypass parameter): same latency, no ducking.
    auto mainBus = getBusBuffer (buffer, true, 0);
    engine.processDelayOnly (mainBus.getArrayOfWritePointers(), mainBus.getNumChannels(), buffer.getNumSamples());
}

// ---------------------------------------------------------------------------
// Factory presets
// ---------------------------------------------------------------------------
namespace
{
    struct PresetValue { const char* id; float value; };

    // Threshold is left out on purpose: it's set separately and presets never touch it.
    std::array<PresetValue, 4> presetValues (const voducker::FactoryPreset& p)
    {
        return { { { ParamIDs::attack,    p.attackMs },
                   { ParamIDs::release,   p.releaseMs },
                   { ParamIDs::hold,      p.holdMs },
                   { ParamIDs::lookahead, p.lookaheadMs } } };
    }
}

int VODuckerProcessor::getPresetIndex() const
{
    const int index = apvts.state.getProperty (presetProperty, -1);
    return juce::isPositiveAndBelow (index, voducker::kNumFactoryPresets) ? index : -1;
}

void VODuckerProcessor::setCurrentProgram (int index)
{
    if (! juce::isPositiveAndBelow (index, voducker::kNumFactoryPresets))
        return;

    // Threshold and Bypass are left alone: a preset only sets the ducking timing.
    for (const auto& pv : presetValues (voducker::kFactoryPresets[index]))
        if (auto* param = apvts.getParameter (pv.id))
        {
            param->beginChangeGesture();
            param->setValueNotifyingHost (param->convertTo0to1 (pv.value));
            param->endChangeGesture();
        }

    apvts.state.setProperty (presetProperty, index, nullptr);
}

const juce::String VODuckerProcessor::getProgramName (int index)
{
    return juce::isPositiveAndBelow (index, voducker::kNumFactoryPresets)
               ? juce::String (voducker::kFactoryPresets[index].name) : juce::String();
}

bool VODuckerProcessor::isPresetModified() const
{
    const int index = getPresetIndex();
    if (index < 0)
        return true;

    for (const auto& pv : presetValues (voducker::kFactoryPresets[index]))
    {
        auto* param = apvts.getParameter (pv.id);
        // Compare on the 0..1 scale so the tolerance means the same on every (skewed) range.
        if (param != nullptr && std::abs (param->getValue() - param->convertTo0to1 (pv.value)) > 1.0e-4f)
            return true;
    }
    return false;
}

juce::AudioProcessorEditor* VODuckerProcessor::createEditor()
{
    return new VODuckerEditor (*this);
}

void VODuckerProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void VODuckerProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new VODuckerProcessor();
}
