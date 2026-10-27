#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>
#include <algorithm>

VocalPhonemeGainAudioProcessor::VocalPhonemeGainAudioProcessor()
    : AudioProcessor (BusesProperties()
                      .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                      .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMETERS", createParameterLayout())
{
    clips.reserve (2048);
}

juce::AudioProcessorValueTreeState::ParameterLayout VocalPhonemeGainAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;
    params.push_back (std::make_unique<juce::AudioParameterFloat>("vowelGain", "All Vowels Gain", juce::NormalisableRange<float> (-18.0f, 12.0f, 0.1f), 0.0f, "dB"));
    params.push_back (std::make_unique<juce::AudioParameterFloat>("consonantGain", "All Consonants Gain", juce::NormalisableRange<float> (-18.0f, 12.0f, 0.1f), 0.0f, "dB"));
    params.push_back (std::make_unique<juce::AudioParameterFloat>("threshold", "Gate Threshold", juce::NormalisableRange<float> (-60.0f, -12.0f, 0.5f), -42.0f, "dB"));
    params.push_back (std::make_unique<juce::AudioParameterFloat>("vcSensitivity", "V/C Split Bias", juce::NormalisableRange<float> (0.1f, 0.9f, 0.01f), 0.5f, ""));
    params.push_back (std::make_unique<juce::AudioParameterFloat>("smoothingMs", "Transition Smooth", juce::NormalisableRange<float> (1.0f, 50.0f, 0.5f), 8.0f, "ms"));
    return { params.begin(), params.end() };
}

void VocalPhonemeGainAudioProcessor::prepareToPlay (double sampleRate, int)
{
    currentSampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;
    const float cutoffHz = 2400.0f;
    lpCoeff = 1.0f - std::exp (-2.0f * juce::MathConstants<float>::pi * cutoffHz / static_cast<float>(currentSampleRate));
    lpState = 0.0f;
    smoothedGainLinear = 1.0f;
}

void VocalPhonemeGainAudioProcessor::releaseResources() {}

bool VocalPhonemeGainAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& mainIn  = layouts.getMainInputChannelSet();
    const auto& mainOut = layouts.getMainOutputChannelSet();
    if (mainIn != mainOut) return false;
    return mainIn == juce::AudioChannelSet::mono() || mainIn == juce::AudioChannelSet::stereo();
}

PhonemeType VocalPhonemeGainAudioProcessor::classifyFrame (
    const float* monoData, int numSamples, float thresholdLinear, float vcBias, float& outPeak)
{
    if (numSamples <= 0) { outPeak = 0.0f; return PhonemeType::Silence; }

    float sumSq = 0.0f, lowEnergy = 0.0f, highEnergy = 0.0f, peak = 0.0f;
    int zeroCrossings = 0;
    float prevSample = monoData[0];

    for (int i = 0; i < numSamples; ++i)
    {
        const float s = monoData[i];
        const float absS = std::abs (s);
        if (absS > peak) peak = absS;
        sumSq += s * s;

        lpState += lpCoeff * (s - lpState);
        const float hp = s - lpState;
        lowEnergy  += lpState * lpState;
        highEnergy += hp * hp;

        if ((s >= 0.0f && prevSample < 0.0f) || (s < 0.0f && prevSample >= 0.0f))
            ++zeroCrossings;
        prevSample = s;
    }

    outPeak = peak;
    const float rms = std::sqrt (sumSq / static_cast<float>(numSamples));
    if (rms < thresholdLinear) return PhonemeType::Silence;

    const float zcrNorm = static_cast<float>(zeroCrossings) / static_cast<float>(numSamples);
    const float hfRatio = highEnergy / (lowEnergy + highEnergy + 1.0e-7f);
    const float consonantScore = (hfRatio * 0.65f) + (juce::jlimit (0.0f, 1.0f, zcrNorm * 3.5f) * 0.35f);
    const float thresholdScore = 1.0f - vcBias;

    return (consonantScore >= thresholdScore * 0.45f) ? PhonemeType::Consonant : PhonemeType::Vowel;
}

int VocalPhonemeGainAudioProcessor::findOrCreateClipForTime (
    double timeSec, double blockDurationSec, PhonemeType detectedType, float peak)
{
    for (int i = 0; i < static_cast<int>(clips.size()); ++i)
    {
        auto& c = clips[static_cast<size_t>(i)];
        if (timeSec >= c.startTimeSec && timeSec <= c.endTimeSec)
        {
            if (captureEnabled.load() && peak > c.peakLevel) c.peakLevel = peak;
            return i;
        }
    }

    if (!captureEnabled.load() || detectedType == PhonemeType::Silence)
        return -1;

    const double mergeToleranceSec = 0.045;
    if (!clips.empty())
    {
        auto& last = clips.back();
        if (last.type == detectedType &&
            std::abs (timeSec - last.endTimeSec) <= mergeToleranceSec &&
            (last.endTimeSec - last.startTimeSec) < 0.85)
        {
            last.endTimeSec = std::max (last.endTimeSec, timeSec + blockDurationSec);
            last.peakLevel  = std::max (last.peakLevel, peak);
            return static_cast<int>(clips.size()) - 1;
        }
    }

    if (clips.size() < 2048)
    {
        PhonemeClip newClip;
        newClip.startTimeSec = timeSec;
        newClip.endTimeSec   = timeSec + std::max (blockDurationSec, 0.04);
        newClip.type         = detectedType;
        newClip.peakLevel    = peak;
        newClip.clipGainDb   = 0.0f;
        clips.push_back (newClip);
        return static_cast<int>(clips.size()) - 1;
    }
    return -1;
}

void VocalPhonemeGainAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int numChannels = buffer.getNumChannels();
    const int numSamples  = buffer.getNumSamples();
    if (numChannels == 0 || numSamples == 0) return;

    const float vowelGainDb     = apvts.getRawParameterValue ("vowelGain")->load();
    const float consonantGainDb = apvts.getRawParameterValue ("consonantGain")->load();
    const float thresholdDb     = apvts.getRawParameterValue ("threshold")->load();
    const float vcBias          = apvts.getRawParameterValue ("vcSensitivity")->load();
    const float smoothingMs     = apvts.getRawParameterValue ("smoothingMs")->load();

    const float thresholdLinear = juce::Decibels::decibelsToGain (thresholdDb);
    const double blockDurationSec = static_cast<double>(numSamples) / currentSampleRate;

    double playTimeSec = fallbackPlayheadSec;
    bool isPlaying = true;

    if (auto* playHead = getPlayHead())
    {
        if (auto posInfo = playHead->getPosition())
        {
            isPlaying = posInfo->getIsPlaying();
            if (auto timeSec = posInfo->getTimeInSeconds())
                playTimeSec = *timeSec;
        }
    }

    if (isPlaying) fallbackPlayheadSec = playTimeSec + blockDurationSec;
    currentPlayheadSec.store (playTimeSec);

    float framePeak = 0.0f;
    PhonemeType rawType = classifyFrame (buffer.getReadPointer (0), numSamples, thresholdLinear, vcBias, framePeak);

    if (rawType == pendingType)
    {
        pendingDurationSec += blockDurationSec;
        if (pendingDurationSec >= 0.030 || rawType == PhonemeType::Silence)
            stableType = rawType;
    }
    else
    {
        pendingType = rawType;
        pendingDurationSec = blockDurationSec;
    }

    float targetGainDb = 0.0f;
    PhonemeType effectiveType = stableType;

    {
        const juce::SpinLock::ScopedTryLockType lock (clipLock);
        if (lock.isLocked() && isPlaying)
        {
            const int clipIdx = findOrCreateClipForTime (playTimeSec, blockDurationSec, stableType, framePeak);
            if (clipIdx >= 0 && clipIdx < static_cast<int>(clips.size()))
            {
                const auto& clip = clips[static_cast<size_t>(clipIdx)];
                effectiveType = clip.type;
                targetGainDb += clip.clipGainDb;
            }
        }
    }

    if (effectiveType == PhonemeType::Vowel)          targetGainDb += vowelGainDb;
    else if (effectiveType == PhonemeType::Consonant) targetGainDb += consonantGainDb;

    activePhonemeType.store (static_cast<int>(effectiveType));
    currentAppliedGainDb.store (targetGainDb);

    const float targetGainLinear = juce::Decibels::decibelsToGain (targetGainDb);
    const float smoothSamples = std::max (1.0f, static_cast<float>(currentSampleRate) * (smoothingMs * 0.001f));
    const float alpha = 1.0f - std::exp (-1.0f / smoothSamples);

    for (int sample = 0; sample < numSamples; ++sample)
    {
        smoothedGainLinear += alpha * (targetGainLinear - smoothedGainLinear);
        for (int ch = 0; ch < numChannels; ++ch)
            buffer.getWritePointer (ch)[sample] *= smoothedGainLinear;
    }
}

std::vector<PhonemeClip> VocalPhonemeGainAudioProcessor::getClipsCopy() const
{
    const juce::SpinLock::ScopedLockType lock (clipLock);
    return clips;
}

void VocalPhonemeGainAudioProcessor::setClipGain (size_t index, float newGainDb)
{
    const juce::SpinLock::ScopedLockType lock (clipLock);
    if (index < clips.size()) clips[index].clipGainDb = juce::jlimit (-18.0f, 12.0f, newGainDb);
}

void VocalPhonemeGainAudioProcessor::toggleClipType (size_t index)
{
    const juce::SpinLock::ScopedLockType lock (clipLock);
    if (index < clips.size())
        clips[index].type = (clips[index].type == PhonemeType::Vowel) ? PhonemeType::Consonant : PhonemeType::Vowel;
}

void VocalPhonemeGainAudioProcessor::clearClips()
{
    const juce::SpinLock::ScopedLockType lock (clipLock);
    clips.clear();
}

void VocalPhonemeGainAudioProcessor::resetAllClipGains()
{
    const juce::SpinLock::ScopedLockType lock (clipLock);
    for (auto& c : clips) c.clipGainDb = 0.0f;
}

void VocalPhonemeGainAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml (state.createXml());
    auto* clipsXml = xml->createNewChildElement ("PhonemeClips");
    for (const auto& c : getClipsCopy())
    {
        auto* item = clipsXml->createNewChildElement ("Clip");
        item->setAttribute ("start", c.startTimeSec);
        item->setAttribute ("end",   c.endTimeSec);
        item->setAttribute ("type",  static_cast<int>(c.type));
        item->setAttribute ("peak",  static_cast<double>(c.peakLevel));
        item->setAttribute ("gain",  static_cast<double>(c.clipGainDb));
    }
    copyXmlToBinary (*xml, destData);
}

void VocalPhonemeGainAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xmlState (getXmlFromBinary (data, sizeInBytes));
    if (xmlState != nullptr && xmlState->hasTagName (apvts.state.getType()))
    {
        apvts.replaceState (juce::ValueTree::fromXml (*xmlState));
        if (auto* clipsXml = xmlState->getChildByName ("PhonemeClips"))
        {
            std::vector<PhonemeClip> loaded;
            for (auto* item : clipsXml->getChildIterator())
            {
                PhonemeClip c;
                c.startTimeSec = item->getDoubleAttribute ("start", 0.0);
                c.endTimeSec   = item->getDoubleAttribute ("end", 0.05);
                c.type         = static_cast<PhonemeType>(item->getIntAttribute ("type", 1));
                c.peakLevel    = static_cast<float>(item->getDoubleAttribute ("peak", 0.5));
                c.clipGainDb   = static_cast<float>(item->getDoubleAttribute ("gain", 0.0));
                loaded.push_back (c);
            }
            const juce::SpinLock::ScopedLockType lock (clipLock);
            clips = std::move (loaded);
        }
    }
}

juce::AudioProcessorEditor* VocalPhonemeGainAudioProcessor::createEditor()
{
    return new VocalPhonemeGainAudioProcessorEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new VocalPhonemeGainAudioProcessor();
}
