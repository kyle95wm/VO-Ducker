#include "PluginEditor.h"

namespace Colours_
{
    const juce::Colour background { 0xff16181d };
    const juce::Colour panel      { 0xff1f2229 };
    const juce::Colour outline    { 0xff2e323b };
    const juce::Colour text       { 0xffe6e8ec };
    const juce::Colour dimText    { 0xff8b919c };
    const juce::Colour accent     { 0xff4fb3ff };
    const juce::Colour reduction  { 0xffff9a3c };
    const juce::Colour good       { 0xff59d18c };
}

// ---------------------------------------------------------------------------
DuckerLookAndFeel::DuckerLookAndFeel()
{
    setColour (juce::Slider::textBoxTextColourId, Colours_::text);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxBackgroundColourId, Colours_::panel);
    setColour (juce::Label::textColourId, Colours_::text);
    setColour (juce::TextButton::buttonColourId, Colours_::panel);
    setColour (juce::TextButton::textColourOffId, Colours_::text);
    setColour (juce::ToggleButton::textColourId, Colours_::dimText);
    setColour (juce::ToggleButton::tickColourId, Colours_::accent);
    setColour (juce::ToggleButton::tickDisabledColourId, Colours_::dimText);
}

void DuckerLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                                          float startAngle, float endAngle, juce::Slider&)
{
    const auto bounds = juce::Rectangle<int> (x, y, w, h).toFloat().reduced (6.0f);
    const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto centre = bounds.getCentre();
    const float angle = startAngle + pos * (endAngle - startAngle);
    const float track = 5.0f;

    juce::Path bg;
    bg.addCentredArc (centre.x, centre.y, radius - track, radius - track, 0.0f, startAngle, endAngle, true);
    g.setColour (Colours_::outline);
    g.strokePath (bg, juce::PathStrokeType (track, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    juce::Path value;
    value.addCentredArc (centre.x, centre.y, radius - track, radius - track, 0.0f, startAngle, angle, true);
    g.setColour (Colours_::accent);
    g.strokePath (value, juce::PathStrokeType (track, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    const float knobR = radius - track * 2.6f;
    g.setColour (Colours_::panel);
    g.fillEllipse (juce::Rectangle<float> (knobR * 2.0f, knobR * 2.0f).withCentre (centre));

    const juce::Point<float> tip (centre.x + std::sin (angle) * knobR * 0.8f,
                                  centre.y - std::cos (angle) * knobR * 0.8f);
    g.setColour (Colours_::text);
    g.drawLine ({ centre.getPointOnCircumference (knobR * 0.25f, angle), tip }, 2.5f);
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
    g.drawText ("-" + juce::String (currentDb, 1), label, juce::Justification::centred);
}

// ---------------------------------------------------------------------------
VODuckerEditor::VODuckerEditor (VODuckerProcessor& p)
    : AudioProcessorEditor (&p), proc (p)
{
    setLookAndFeel (&lnf);

    setupKnob (threshold, ParamIDs::threshold, "Threshold");
    setupKnob (attack,    ParamIDs::attack,    "Attack");
    setupKnob (release,   ParamIDs::release,   "Release");
    setupKnob (hold,      ParamIDs::hold,      "Hold");
    setupKnob (lookahead, ParamIDs::lookahead, "Lookahead");

    addAndMakeVisible (meter);

    for (auto* l : { &infoLabel, &sidechainLabel, &updateLabel })
    {
        l->setFont (juce::FontOptions (12.0f));
        l->setColour (juce::Label::textColourId, Colours_::dimText);
        addAndMakeVisible (*l);
    }
    updateLabel.setJustificationType (juce::Justification::centredRight);
    sidechainLabel.setJustificationType (juce::Justification::centred);

    updateButton.onClick = [this]
    {
        if (pendingReleaseUrl.isNotEmpty())
            juce::URL (pendingReleaseUrl).launchInDefaultBrowser();
        else
            proc.updateChecker.checkNow (true);
        refreshUpdateUi();
    };
    addAndMakeVisible (updateButton);

    autoCheckToggle.setToggleState (proc.updateChecker.isAutoCheckEnabled(), juce::dontSendNotification);
    autoCheckToggle.onClick = [this] { proc.updateChecker.setAutoCheckEnabled (autoCheckToggle.getToggleState()); };
    addAndMakeVisible (autoCheckToggle);

    proc.updateChecker.checkNow (false); // throttled to once a day, respects the toggle

    setSize (660, 330);
    startTimerHz (30);
    timerCallback();
}

VODuckerEditor::~VODuckerEditor()
{
    setLookAndFeel (nullptr);
}

void VODuckerEditor::setupKnob (Knob& k, const char* paramId, const juce::String& name)
{
    k.slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    k.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 84, 20);
    k.slider.setDoubleClickReturnValue (true, proc.apvts.getParameter (paramId)->convertFrom0to1 (
                                                  proc.apvts.getParameter (paramId)->getDefaultValue()));
    addAndMakeVisible (k.slider);

    k.label.setText (name, juce::dontSendNotification);
    k.label.setJustificationType (juce::Justification::centred);
    k.label.setFont (juce::FontOptions (13.0f, juce::Font::bold));
    addAndMakeVisible (k.label);

    k.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, paramId, k.slider);
}

void VODuckerEditor::paint (juce::Graphics& g)
{
    g.fillAll (Colours_::background);

    auto header = getLocalBounds().removeFromTop (44);
    g.setColour (Colours_::panel);
    g.fillRect (header);

    g.setColour (Colours_::text);
    g.setFont (juce::FontOptions (20.0f, juce::Font::bold));
    g.drawText ("VODucker", header.withTrimmedLeft (16).withWidth (120), juce::Justification::centredLeft);

    g.setColour (Colours_::dimText);
    g.setFont (juce::FontOptions (12.0f));
    g.drawText ("v" + UpdateChecker::getCurrentVersion(), header.withTrimmedLeft (128).withWidth (80),
                juce::Justification::centredLeft);
}

void VODuckerEditor::resized()
{
    auto area = getLocalBounds();

    auto header = area.removeFromTop (44).reduced (12, 8);
    updateButton.setBounds (header.removeFromRight (150));
    header.removeFromRight (8);
    updateLabel.setBounds (header.removeFromRight (220));

    auto footer = area.removeFromBottom (34).reduced (12, 6);
    autoCheckToggle.setBounds (footer.removeFromRight (170));
    infoLabel.setBounds (footer.removeFromLeft (260));
    sidechainLabel.setBounds (footer);

    area.reduce (12, 10);
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
    infoLabel.setText ("4:1  |  18 dB knee  |  Latency " + juce::String (latencyMs, 1) + " ms",
                       juce::dontSendNotification);

    const bool sc = proc.isSidechainConnected();
    sidechainLabel.setText (sc ? "AD narration key: connected" : "No sidechain: not ducking", juce::dontSendNotification);
    sidechainLabel.setColour (juce::Label::textColourId, sc ? Colours_::good : Colours_::reduction);

    refreshUpdateUi();
}

void VODuckerEditor::refreshUpdateUi()
{
    using S = UpdateChecker::Status;
    const auto r = proc.updateChecker.getResult();

    pendingReleaseUrl.clear();
    juce::String text, button = "Check for updates";
    auto colour = Colours_::dimText;

    switch (r.status)
    {
        case S::idle:      break;
        case S::checking:  text = "Checking GitHub..."; break;
        case S::upToDate:  text = "Up to date"; colour = Colours_::good; break;
        case S::failed:    text = r.error; colour = Colours_::reduction; break;
        case S::updateAvailable:
            text = "Update available: v" + r.latestVersion;
            colour = Colours_::accent;
            button = "Download";
            pendingReleaseUrl = r.releaseUrl;
            break;
    }

    if (updateLabel.getText() != text)
        updateLabel.setText (text, juce::dontSendNotification);
    updateLabel.setColour (juce::Label::textColourId, colour);
    if (updateButton.getButtonText() != button)
        updateButton.setButtonText (button);
}
