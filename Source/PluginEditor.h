#pragma once
#include "PluginProcessor.h"
#include <juce_gui_basics/juce_gui_basics.h>

class PhonemeTimelineComponent : public juce::Component
{
public:
    explicit PhonemeTimelineComponent (VocalPhonemeGainAudioProcessor& proc);
    void paint (juce::Graphics& g) override;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseDrag (const juce::MouseEvent& e) override;
    void mouseDoubleClick (const juce::MouseEvent& e) override;
    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;
    void updateFromProcessor();

private:
    VocalPhonemeGainAudioProcessor& processor;
    std::vector<PhonemeClip> cachedClips;
    double viewStartSec = 0.0;
    double viewDurationSec = 8.0;
    int draggedClipIndex = -1;
    float dragStartGainDb = 0.0f;

    int hitTestClip (juce::Point<float> pt) const;
    float timeToX (double t) const;
    double xToTime (float x) const;
    float gainDbToY (float db) const;
};

class VocalPhonemeGainAudioProcessorEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit VocalPhonemeGainAudioProcessorEditor (VocalPhonemeGainAudioProcessor&);
    ~VocalPhonemeGainAudioProcessorEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void setupKnob (juce::Slider& slider, juce::Label& label, const juce::String& text);

    VocalPhonemeGainAudioProcessor& audioProcessor;
    PhonemeTimelineComponent timeline;

    juce::ToggleButton captureButton { "Auto-Capture Phonemes" };
    juce::TextButton resetGainsButton { "Reset Clip Gains" };
    juce::TextButton clearMapButton { "Clear Map" };

    juce::Slider vowelGainSlider, consonantGainSlider, thresholdSlider, vcBiasSlider, smoothSlider;
    juce::Label  vowelGainLabel,  consonantGainLabel,  thresholdLabel,  vcBiasLabel,  smoothLabel;

    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    std::unique_ptr<SliderAttachment> vowelAttach, consonantAttach, thresholdAttach, vcBiasAttach, smoothAttach;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VocalPhonemeGainAudioProcessorEditor)
};
