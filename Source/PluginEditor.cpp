#include "PluginEditor.h"

namespace Colours_
{
    // Family palette from BrandLookAndFeel.h (shared across kyle95wm plugins),
    // plus VODucker's own gain-reduction colour (meter, bypass pill, sidechain status).
    const juce::Colour background = brand::bg;
    const juce::Colour panel      = brand::panel;
    const juce::Colour outline    = brand::track;
    const juce::Colour text       = brand::textMain;
    const juce::Colour dimText    = brand::textDim;
    const juce::Colour good       = brand::good;
    const juce::Colour reduction  { 0xffff9a3c };
}

// ---------------------------------------------------------------------------
void ReductionMeter::setReductionDb (float db)
{
    // Fast attack, smooth visual fall-off.
    currentDb = db > currentDb ? db : currentDb + (db - currentDb) * 0.35f;

    if (db >= peakDb) { peakDb = db; peakHoldFrames = 45; }
    else if (--peakHoldFrames <= 0) peakDb = juce::jmax (db, peakDb - 0.5f);

    repaint();
}

void ReductionMeter::paint (juce::Graphics& g)
{
    auto area = getLocalBounds().toFloat();
    auto label = area.removeFromBottom (18.0f);
    auto bar = area.reduced (area.getWidth() * 0.28f, 2.0f);

    g.setColour (Colours_::panel);
    g.fillRoundedRectangle (bar, 3.0f);

    // Scale: 0 dB at the top, 60 dB at the bottom (log-ish for readability).
    auto dbToY = [&bar] (float db)
    {
        const float norm = std::sqrt (juce::jlimit (0.0f, 60.0f, db) / 60.0f);
        return bar.getY() + norm * bar.getHeight();
    };

    g.setFont (juce::FontOptions (10.0f));
    for (float tick : { 3.0f, 6.0f, 12.0f, 24.0f, 40.0f, 60.0f })
    {
        const float ty = dbToY (tick);
        g.setColour (Colours_::outline);
        g.drawHorizontalLine ((int) ty, bar.getX(), bar.getRight());
        g.setColour (Colours_::dimText);
        g.drawText (juce::String ((int) tick), juce::Rectangle<float> (0.0f, ty - 6.0f, bar.getX() - 3.0f, 12.0f),
                    juce::Justification::centredRight);
    }

    if (currentDb > 0.05f)
    {
        g.setColour (Colours_::reduction);
        g.fillRoundedRectangle (bar.withBottom (dbToY (currentDb)), 3.0f);
    }
    if (peakDb > 0.05f)
    {
        g.setColour (Colours_::text);
        g.drawHorizontalLine ((int) dbToY (peakDb), bar.getX(), bar.getRight());
    }

    g.setColour (Colours_::text);
    g.setFont (juce::FontOptions (11.0f));
    g.drawText (currentDb < 0.05f ? juce::String ("0.0") : "-" + juce::String (currentDb, 1), label, juce::Justification::centred);
}

// ---------------------------------------------------------------------------
VODuckerEditor::VODuckerEditor (VODuckerProcessor& p)
    : AudioProcessorEditor (&p), proc (p), footer (p.updateChecker)
{
    setLookAndFeel (&lnf);
    // Preset menu colours. Set on this editor's look-and-feel instance, not in the shared
    // BrandLookAndFeel.h, which has to stay identical across plugins.
    lnf.setColour (juce::ComboBox::backgroundColourId, Colours_::panel);
    lnf.setColour (juce::ComboBox::outlineColourId, Colours_::outline);
    lnf.setColour (juce::ComboBox::textColourId, Colours_::text);
    lnf.setColour (juce::ComboBox::arrowColourId, Colours_::dimText);
    lnf.setColour (juce::ComboBox::focusedOutlineColourId, brand::accent);
    lnf.setColour (juce::PopupMenu::backgroundColourId, Colours_::panel);
    lnf.setColour (juce::PopupMenu::textColourId, Colours_::text);
    lnf.setColour (juce::PopupMenu::highlightedBackgroundColourId, brand::accent);
    lnf.setColour (juce::PopupMenu::highlightedTextColourId, Colours_::background);

    setupKnob (threshold, ParamIDs::threshold, "Threshold");
    setupKnob (attack,    ParamIDs::attack,    "Attack");
    setupKnob (release,   ParamIDs::release,   "Release");
    setupKnob (hold,      ParamIDs::hold,      "Hold");
    setupKnob (lookahead, ParamIDs::lookahead, "Lookahead");

    addAndMakeVisible (meter);

    // Internal bypass parameter: automatable, independent of the DAW's own bypass switch.
    brand::makePill (bypassButton, "ACTIVE", "BYPASSED", Colours_::reduction);
    bypassButton.setTooltip ("Bypass the ducking (keeps the lookahead delay so timing doesn't shift)");
    addAndMakeVisible (bypassButton);
    bypassAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        proc.apvts, ParamIDs::bypass, bypassButton);

    // Factory presets. Item IDs are preset index + 1 (0 means "nothing selected" to ComboBox).
    presetLabel.setText ("Preset", juce::dontSendNotification);
    presetLabel.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    presetLabel.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (presetLabel);

    for (int i = 0; i < proc.getNumPrograms(); ++i)
        presetBox.addItem (proc.getProgramName (i), i + 1);
    presetBox.setTextWhenNothingSelected ("Custom");
    presetBox.setTooltip ("Load a factory preset (sets Attack, Release, Hold and Lookahead; Threshold and Bypass stay as they are)");
    presetBox.onChange = [this]
    {
        if (const int id = presetBox.getSelectedId(); id > 0)
            proc.setCurrentProgram (id - 1);
        refreshPresetUi();
    };
    addAndMakeVisible (presetBox);

    auto stepPreset = [this] (int delta)
    {
        const int n = proc.getNumPrograms();
        const int current = proc.getPresetIndex();
        const int next = current < 0 ? (delta > 0 ? 0 : n - 1) : (current + delta + n) % n;
        proc.setCurrentProgram (next);
        refreshPresetUi();
    };
    prevPresetButton.onClick = [stepPreset] { stepPreset (-1); };
    nextPresetButton.onClick = [stepPreset] { stepPreset (+1); };
    prevPresetButton.setTooltip ("Previous preset");
    nextPresetButton.setTooltip ("Next preset");
    addAndMakeVisible (prevPresetButton);
    addAndMakeVisible (nextPresetButton);

    for (auto* l : { &infoLabel, &sidechainLabel })
    {
        l->setFont (juce::FontOptions (brand::captionSize));
        l->setColour (juce::Label::textColourId, Colours_::dimText);
        addAndMakeVisible (*l);
    }
    infoLabel.setJustificationType (juce::Justification::centredLeft);
    sidechainLabel.setJustificationType (juce::Justification::centredRight);

    addAndMakeVisible (footer);

    setSize (660, 466);
    startTimerHz (30);
    timerCallback();
}

VODuckerEditor::~VODuckerEditor()
{
    setLookAndFeel (nullptr);
}

void VODuckerEditor::setupKnob (Knob& k, const char* paramId, const juce::String& name)
{
    // Add before styling, so the text box is built with the brand look-and-feel.
    addAndMakeVisible (k.slider);
    k.slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    k.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 84, 20);
    k.slider.setDoubleClickReturnValue (true, proc.apvts.getParameter (paramId)->convertFrom0to1 (
                                                  proc.apvts.getParameter (paramId)->getDefaultValue()));
    k.slider.setColour (juce::Slider::textBoxBackgroundColourId, Colours_::panel);  // VODucker's boxed values

    k.label.setText (name, juce::dontSendNotification);
    k.label.setJustificationType (juce::Justification::centred);
    k.label.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    addAndMakeVisible (k.label);

    k.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, paramId, k.slider);
}

void VODuckerEditor::paint (juce::Graphics& g)
{
    g.fillAll (Colours_::background);
    // Non-ASCII subtitle text must go through CharPointer_UTF8 (see BrandLookAndFeel.h).
    brand::drawHeader (g, getLocalBounds(), JucePlugin_Name, UpdateChecker::getCurrentVersion(),
                       juce::String (juce::CharPointer_UTF8 (
                           "Audio description ducker  \xc2\xb7  sidechain compressor with up to 1 s lookahead")));
}

void VODuckerEditor::resized()
{
    auto area = getLocalBounds().reduced (brand::margin);
    area.removeFromTop (brand::headerHeight);

    // Preset bar, centred under the header.
    auto presetRow = area.removeFromTop (36).withTrimmedTop (6);
    const int presetWidth = 60 + 6 + 28 + 4 + 240 + 4 + 28;
    presetRow = presetRow.withSizeKeepingCentre (presetWidth, 28);
    presetLabel.setBounds (presetRow.removeFromLeft (60));
    presetRow.removeFromLeft (6);
    prevPresetButton.setBounds (presetRow.removeFromLeft (28));
    presetRow.removeFromLeft (4);
    nextPresetButton.setBounds (presetRow.removeFromRight (28));
    presetRow.removeFromRight (4);
    presetBox.setBounds (presetRow);

    footer.setBounds (area.removeFromBottom (brand::footerHeight));
    area.removeFromBottom (10);

    // Pill row: ratio/latency info on the left, Bypass centred, sidechain status on the right.
    auto pills = area.removeFromBottom (36);
    bypassButton.setBounds (pills.withSizeKeepingCentre (130, 30));
    const int sideW = (pills.getWidth() - 130) / 2 - 12;
    infoLabel.setBounds (pills.removeFromLeft (sideW));
    sidechainLabel.setBounds (pills.removeFromRight (sideW));
    area.removeFromBottom (14);
    area.removeFromTop (12);

    meter.setBounds (area.removeFromRight (70));
    area.removeFromRight (8);

    const int knobW = area.getWidth() / 5;
    for (auto* k : { &threshold, &attack, &release, &hold, &lookahead })
    {
        auto col = area.removeFromLeft (knobW).reduced (4, 0);
        k->label.setBounds (col.removeFromTop (22));
        k->slider.setBounds (col);
    }
}

void VODuckerEditor::timerCallback()
{
    meter.setReductionDb (proc.getCurrentReductionDb());

    const double sr = proc.getSampleRate() > 0 ? proc.getSampleRate() : 44100.0;
    const double latencyMs = 1000.0 * proc.getLatencySamples() / sr;
    infoLabel.setText (juce::String (juce::CharPointer_UTF8 ("4:1  \xc2\xb7  18 dB knee  \xc2\xb7  latency "))
                           + juce::String (latencyMs, 1) + " ms",
                       juce::dontSendNotification);

    const bool sc = proc.isSidechainConnected();
    const bool bypassed = bypassButton.getToggleState();
    sidechainLabel.setText (bypassed ? "Bypassed: not ducking"
                                     : sc ? "AD narration key: connected" : "No sidechain: not ducking",
                            juce::dontSendNotification);
    sidechainLabel.setColour (juce::Label::textColourId, sc && ! bypassed ? Colours_::good : Colours_::reduction);

    const float knobAlpha = bypassed ? 0.45f : 1.0f;
    for (auto* k : { &threshold, &attack, &release, &hold, &lookahead })
        if (std::abs (k->slider.getAlpha() - knobAlpha) > 0.001f)
        {
            k->slider.setAlpha (knobAlpha);
            k->label.setAlpha (knobAlpha);
        }

    refreshPresetUi();
}

void VODuckerEditor::refreshPresetUi()
{
    // Follows preset changes from the host and marks the preset as edited once a knob moves.
    if (presetBox.isPopupActive())
        return;

    const int index = proc.getPresetIndex();
    if (index < 0)
    {
        if (presetBox.getSelectedId() != 0 || presetBox.getText().isNotEmpty())
            presetBox.setSelectedId (0, juce::dontSendNotification);
    }
    else if (! proc.isPresetModified())
    {
        if (presetBox.getSelectedId() != index + 1)
            presetBox.setSelectedId (index + 1, juce::dontSendNotification);
    }
    else
    {
        const auto text = proc.getProgramName (index) + " (edited)";
        if (presetBox.getText() != text)
            presetBox.setText (text, juce::dontSendNotification);
    }
}
