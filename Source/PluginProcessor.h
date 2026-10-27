#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include <vector>
#include <atomic>

enum class PhonemeType : int { Silence = 0, Vowel = 1, Consonant = 2 };

struct PhonemeClip
{
    double startTimeSec = 0.0;
    double endTimeSec   = 0.05;
    PhonemeType type    = PhonemeType::Vowel;
    float peakLevel     = 0.0f;
    float clipGainDb    = 0.0f;
};

class VocalPhonemeGainAudioProcessor : public juce::AudioProcessor
{
public:
    VocalPhonemeGainAudioProcessor();
    ~VocalPhonemeGainAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

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

    juce::AudioProcessorValueTreeState apvts;
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    std::vector<PhonemeClip> getClipsCopy() const;
    void setClipGain (size_t index, float newGainDb);
    void toggleClipType (size_t index);
    void clearClips();
    void resetAllClipGains();

    std::atomic<double> currentPlayheadSec { 0.0 };
    std::atomic<int> activePhonemeType { 0 };
    std::atomic<float> currentAppliedGainDb { 0.0f };
    std::atomic<bool> captureEnabled { true };

private:
    PhonemeType classifyFrame (const float* monoData, int numSamples, float thresholdLinear, float vcBias, float& outPeak);
    int findOrCreateClipForTime (double timeSec, double blockDurationSec, PhonemeType detectedType, float peak);

    mutable juce::SpinLock clipLock;
    std::vector<PhonemeClip> clips;

    double currentSampleRate = 44100.0;
    double fallbackPlayheadSec = 0.0;
    float lpState = 0.0f;
    float lpCoeff = 0.2f;
    float smoothedGainLinear = 1.0f;

    PhonemeType pendingType = PhonemeType::Silence;
    double pendingDurationSec = 0.0;
    PhonemeType stableType = PhonemeType::Silence;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VocalPhonemeGainAudioProcessor)
};
