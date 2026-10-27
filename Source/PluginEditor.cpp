#include "PluginEditor.h"

PhonemeTimelineComponent::PhonemeTimelineComponent (VocalPhonemeGainAudioProcessor& proc) : processor (proc) {}

float PhonemeTimelineComponent::timeToX (double t) const
{
    return static_cast<float>((t - viewStartSec) / viewDurationSec) * static_cast<float>(getWidth());
}

double PhonemeTimelineComponent::xToTime (float x) const
{
    return viewStartSec + (static_cast<double>(x) / static_cast<double>(getWidth())) * viewDurationSec;
}

float PhonemeTimelineComponent::gainDbToY (float db) const
{
    const float norm = (12.0f - juce::jlimit (-18.0f, 12.0f, db)) / 30.0f;
    return 24.0f + norm * (static_cast<float>(getHeight()) - 48.0f);
}

int PhonemeTimelineComponent::hitTestClip (juce::Point<float> pt) const
{
    const double clickTime = xToTime (pt.x);
    for (int i = 0; i < static_cast<int>(cachedClips.size()); ++i)
    {
        const auto& c = cachedClips[static_cast<size_t>(i)];
        if (clickTime >= c.startTimeSec && clickTime <= c.endTimeSec) return i;
    }
    return -1;
}

void PhonemeTimelineComponent::updateFromProcessor()
{
    cachedClips = processor.getClipsCopy();
    const double playhead = processor.currentPlayheadSec.load();
    if (playhead < viewStartSec || playhead > (viewStartSec + viewDurationSec * 0.90))
        viewStartSec = std::max (0.0, playhead - viewDurationSec * 0.15);
    repaint();
}

void PhonemeTimelineComponent::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    g.setColour (juce::Colour (0xff14171c));
    g.fillRoundedRectangle (bounds, 6.0f);

    const float zeroDbY = gainDbToY (0.0f);
    g.setColour (juce::Colour (0xff2e3440));
    g.drawHorizontalLine (static_cast<int>(zeroDbY), 0.0f, bounds.getWidth());

    g.setFont (11.0f);
    const int firstSec = static_cast<int>(std::floor (viewStartSec));
    const int lastSec  = static_cast<int>(std::ceil (viewStartSec + viewDurationSec));
    for (int s = firstSec; s <= lastSec; ++s)
    {
        const float x = timeToX (static_cast<double>(s));
        g.setColour (juce::Colour (0xff20242c));
        g.drawVerticalLine (static_cast<int>(x), 0.0f, bounds.getHeight());
        g.setColour (juce::Colour (0xff6c7686));
        g.drawText (juce::String (s) + "s", static_cast<int>(x) + 4, 4, 40, 14, juce::Justification::left);
    }

    for (size_t i = 0; i < cachedClips.size(); ++i)
    {
        const auto& c = cachedClips[i];
        if (c.endTimeSec < viewStartSec || c.startTimeSec > (viewStartSec + viewDurationSec)) continue;

        const float x1 = juce::jlimit (0.0f, bounds.getWidth(), timeToX (c.startTimeSec));
        const float x2 = juce::jlimit (0.0f, bounds.getWidth(), timeToX (c.endTimeSec));
        const float w  = std::max (6.0f, x2 - x1 - 1.5f);

        const bool isVowel = (c.type == PhonemeType::Vowel);
        const juce::Colour baseCol = isVowel ? juce::Colour (0xff38bdf8) : juce::Colour (0xfff59e0b);
        const float handleY = gainDbToY (c.clipGainDb);

        const float scaledPeak = juce::jlimit (0.1f, 1.0f, c.peakLevel * juce::Decibels::decibelsToGain (c.clipGainDb));
        const float waveHalfH  = scaledPeak * (bounds.getHeight() * 0.34f);

        juce::Rectangle<float> clipBox (x1, zeroDbY - waveHalfH, w, waveHalfH * 2.0f);
        g.setColour (baseCol.withAlpha (0.24f));
        g.fillRoundedRectangle (clipBox, 3.0f);
        g.setColour (baseCol.withAlpha (0.65f));
        g.drawRoundedRectangle (clipBox, 3.0f, 1.0f);

        g.setColour (baseCol);
        g.fillRect (x1, handleY - 1.5f, w, 3.0f);

        if (w >= 14.0f)
        {
            g.setColour (juce::Colours::white.withAlpha (0.9f));
            g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
            juce::String tag = isVowel ? "V" : "C";
            if (std::abs (c.clipGainDb) >= 0.1f && w >= 34.0f) tag += " " + juce::String (c.clipGainDb, 1);
            g.drawText (tag, static_cast<int>(x1) + 2, static_cast<int>(handleY) - 16, static_cast<int>(w) - 4, 14, juce::Justification::centred);
        }
    }

    const float playX = timeToX (processor.currentPlayheadSec.load());
    if (playX >= 0.0f && playX <= bounds.getWidth())
    {
        g.setColour (juce::Colour (0xffef4444));
        g.drawLine (playX, 0.0f, playX, bounds.getHeight(), 2.0f);
    }

    if (cachedClips.empty())
    {
        g.setColour (juce::Colour (0xff8892b0));
        g.setFont (14.0f);
        g.drawText ("Press Play in Studio One / Cubase to detect Vowels (Cyan) & Consonants (Amber). Drag any clip up/down to adjust gain.",
                    getLocalBounds().reduced (20), juce::Justification::centred);
    }
}

void PhonemeTimelineComponent::mouseDown (const juce::MouseEvent& e)
{
    const int idx = hitTestClip (e.position);
    if (idx >= 0)
    {
        if (e.mods.isRightButtonDown())
        {
            processor.toggleClipType (static_cast<size_t>(idx));
            updateFromProcessor();
            return;
        }
        draggedClipIndex = idx;
        dragStartGainDb  = cachedClips[static_cast<size_t>(idx)].clipGainDb;
    }
    else draggedClipIndex = -1;
}

void PhonemeTimelineComponent::mouseDrag (const juce::MouseEvent& e)
{
    if (draggedClipIndex >= 0)
    {
        const float deltaDb = -static_cast<float>(e.getDistanceFromDragStartY()) * 0.15f;
        processor.setClipGain (static_cast<size_t>(draggedClipIndex), dragStartGainDb + deltaDb);
        updateFromProcessor();
    }
}

void PhonemeTimelineComponent::mouseDoubleClick (const juce::MouseEvent& e)
{
    const int idx = hitTestClip (e.position);
    if (idx >= 0)
    {
        processor.setClipGain (static_cast<size_t>(idx), 0.0f);
        updateFromProcessor();
    }
}

void PhonemeTimelineComponent::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& wheel)
{
    if (std::abs (wheel.deltaX) > 0.0f)      viewStartSec = std::max (0.0, viewStartSec - wheel.deltaX * 1.5);
    else if (std::abs (wheel.deltaY) > 0.0f) viewStartSec = std::max (0.0, viewStartSec - wheel.deltaY * 1.5);
    repaint();
}

VocalPhonemeGainAudioProcessorEditor::VocalPhonemeGainAudioProcessorEditor (VocalPhonemeGainAudioProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p), timeline (p)
{
    setSize (780, 440);
    addAndMakeVisible (timeline);

    captureButton.setToggleState (audioProcessor.captureEnabled.load(), juce::dontSendNotification);
    captureButton.onClick = [this] { audioProcessor.captureEnabled.store (captureButton.getToggleState()); };
    addAndMakeVisible (captureButton);

    resetGainsButton.onClick = [this] { audioProcessor.resetAllClipGains(); timeline.updateFromProcessor(); };
    addAndMakeVisible (resetGainsButton);

    clearMapButton.onClick = [this] { audioProcessor.clearClips(); timeline.updateFromProcessor(); };
    addAndMakeVisible (clearMapButton);

    setupKnob (vowelGainSlider,     vowelGainLabel,     "All Vowels (dB)");
    setupKnob (consonantGainSlider, consonantGainLabel, "All Consonants (dB)");
    setupKnob (thresholdSlider,     thresholdLabel,     "Gate Threshold");
    setupKnob (vcBiasSlider,        vcBiasLabel,        "V/C Split Bias");
    setupKnob (smoothSlider,        smoothLabel,        "Smooth (ms)");

    vowelAttach     = std::make_unique<SliderAttachment>(audioProcessor.apvts, "vowelGain",     vowelGainSlider);
    consonantAttach = std::make_unique<SliderAttachment>(audioProcessor.apvts, "consonantGain", consonantGainSlider);
    thresholdAttach = std::make_unique<SliderAttachment>(audioProcessor.apvts, "threshold",     thresholdSlider);
    vcBiasAttach    = std::make_unique<SliderAttachment>(audioProcessor.apvts, "vcSensitivity", vcBiasSlider);
    smoothAttach    = std::make_unique<SliderAttachment>(audioProcessor.apvts, "smoothingMs",   smoothSlider);

    startTimerHz (30);
}

VocalPhonemeGainAudioProcessorEditor::~VocalPhonemeGainAudioProcessorEditor() { stopTimer(); }

void VocalPhonemeGainAudioProcessorEditor::setupKnob (juce::Slider& slider, juce::Label& label, const juce::String& text)
{
    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 70, 18);
    slider.setColour (juce::Slider::rotarySliderFillColourId, juce::Colour (0xff38bdf8));
    slider.setColour (juce::Slider::thumbColourId, juce::Colours::white);
    addAndMakeVisible (slider);

    label.setText (text, juce::dontSendNotification);
    label.setJustificationType (juce::Justification::centred);
    label.setFont (juce::FontOptions (12.5f, juce::Font::bold));
    label.setColour (juce::Label::textColourId, juce::Colour (0xffcbd5e1));
    addAndMakeVisible (label);
}

void VocalPhonemeGainAudioProcessorEditor::timerCallback()
{
    timeline.updateFromProcessor();
    repaint (0, 0, getWidth(), 48);
}

void VocalPhonemeGainAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff0f1115));
    g.setColour (juce::Colours::white);
    g.setFont (juce::FontOptions (18.0f, juce::Font::bold));
    g.drawText ("VOCAL PHONEME CLIP GAIN", 18, 12, 260, 24, juce::Justification::left);

    const int pType = audioProcessor.activePhonemeType.load();
    const float gainDb = audioProcessor.currentAppliedGainDb.load();

    juce::String badgeText = "SILENCE";
    juce::Colour badgeCol  = juce::Colour (0xff334155);
    if (pType == 1) { badgeText = "VOWEL";     badgeCol = juce::Colour (0xff0284c7); }
    if (pType == 2) { badgeText = "CONSONANT"; badgeCol = juce::Colour (0xffd97706); }

    juce::Rectangle<float> badgeRect (275.0f, 12.0f, 155.0f, 24.0f);
    g.setColour (badgeCol);
    g.fillRoundedRectangle (badgeRect, 4.0f);
    g.setColour (juce::Colours::white);
    g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    g.drawText (badgeText + " (" + juce::String (gainDb, 1) + " dB)", badgeRect, juce::Justification::centred);
}

void VocalPhonemeGainAudioProcessorEditor::resized()
{
    captureButton.setBounds (450, 12, 165, 24);
    resetGainsButton.setBounds (620, 12, 85, 24);
    clearMapButton.setBounds (710, 12, 55, 24);
    timeline.setBounds (16, 48, getWidth() - 32, 245);

    const int knobY = 322;
    const int colW  = (getWidth() - 32) / 5;
    juce::Slider* sliders[] = { &vowelGainSlider, &consonantGainSlider, &thresholdSlider, &vcBiasSlider, &smoothSlider };
    juce::Label*  labels[]  = { &vowelGainLabel,  &consonantGainLabel,  &thresholdLabel,  &vcBiasLabel,  &smoothLabel };

    for (int i = 0; i < 5; ++i)
    {
        const int x = 16 + i * colW;
        labels[i]->setBounds (x, knobY - 20, colW, 18);
        sliders[i]->setBounds (x + 12, knobY, colW - 24, 105);
    }
}
