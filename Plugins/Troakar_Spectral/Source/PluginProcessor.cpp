#include "PluginProcessor.h"
#include "PluginEditor.h"

TroakarSpectralAudioProcessor::TroakarSpectralAudioProcessor()
     : AudioProcessor (BusesProperties()
                       .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
       apvts (*this, nullptr, "Parameters", createParameterLayout())
{
}

TroakarSpectralAudioProcessor::~TroakarSpectralAudioProcessor() {}

juce::AudioProcessorValueTreeState::ParameterLayout TroakarSpectralAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    using FloatAttr = juce::AudioParameterFloatAttributes;
    auto dbFormat = [](float value, int) { return juce::String(value, 1) + " dB"; };
    auto hzFormat = [](float value, int) { 
        return value >= 1000.0f ? juce::String(value / 1000.0f, 2) + " kHz" 
                                : juce::String(std::round(value)) + " Hz"; 
    };
    auto pctFormat = [](float value, int) { return juce::String(static_cast<int>(std::round(value))) + " %"; };
    auto qFormat = [](float value, int) { return juce::String(value, 2); };

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "IN_GAIN", "Input Gain", 
        juce::NormalisableRange<float>(-24.0f, 24.0f, 0.1f), 0.0f, 
        FloatAttr().withStringFromValueFunction(dbFormat)));
    
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "OUT_LVL", "Output Level", 
        juce::NormalisableRange<float>(-24.0f, 24.0f, 0.1f), 0.0f, 
        FloatAttr().withStringFromValueFunction(dbFormat)));
    
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "MIX", "Dry/Wet Mix", 
        juce::NormalisableRange<float>(0.0f, 100.0f, 1.0f), 100.0f, 
        FloatAttr().withStringFromValueFunction(pctFormat)));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "AMOUNT", "Comp Amount", 
        juce::NormalisableRange<float>(0.0f, 150.0f, 1.0f), 100.0f, 
        FloatAttr().withStringFromValueFunction(pctFormat)));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "GLOBAL_THRESH", "EQ Threshold", 
        juce::NormalisableRange<float>(-24.0f, 24.0f, 0.1f), 0.0f, 
        FloatAttr().withStringFromValueFunction(dbFormat)));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "UPWARD_RANGE", "Max Upward Gain", 
        juce::NormalisableRange<float>(0.0f, 24.0f, 0.1f), 4.0f, 
        FloatAttr().withStringFromValueFunction(dbFormat)));
    
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "DOWNWARD_RANGE", "Max Downward Red.", 
        juce::NormalisableRange<float>(-24.0f, 0.0f, 0.1f), -4.0f, 
        FloatAttr().withStringFromValueFunction(dbFormat)));
    
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "SPECTRAL_SPEED", "Reaction Speed", 
        juce::NormalisableRange<float>(0.0f, 100.0f, 1.0f), 50.0f, 
        FloatAttr().withStringFromValueFunction(pctFormat)));
    
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "SMOOTHING", "Freq Smoothing", 
        juce::NormalisableRange<float>(0.0f, 100.0f, 1.0f), 20.0f, 
        FloatAttr().withStringFromValueFunction(pctFormat)));

    for (int i = 0; i < NUM_TARGET_BANDS; ++i)
    {
        juce::String idPrefix = "BAND_" + juce::String(i);
        juce::String namePrefix = "Band " + juce::String(i + 1);

        bool defaultEnabled = (i < 2);
        float defaultFreq = (i == 0) ? 100.0f : (i == 1) ? 5000.0f : 1000.0f * (i + 1);

        params.push_back(std::make_unique<juce::AudioParameterBool>(
            idPrefix + "_ENABLE", namePrefix + " Enabled", defaultEnabled));
        
        params.push_back(std::make_unique<juce::AudioParameterFloat>(
            idPrefix + "_FREQ", namePrefix + " Freq", 
            juce::NormalisableRange<float>(20.0f, 20000.0f, 0.1f, 0.25f), defaultFreq, 
            FloatAttr().withStringFromValueFunction(hzFormat)));

        params.push_back(std::make_unique<juce::AudioParameterFloat>(
            idPrefix + "_GAIN", namePrefix + " Gain", 
            juce::NormalisableRange<float>(-24.0f, 24.0f, 0.1f), 0.0f, 
            FloatAttr().withStringFromValueFunction(dbFormat)));

        params.push_back(std::make_unique<juce::AudioParameterFloat>(
            idPrefix + "_Q", namePrefix + " Q", 
            juce::NormalisableRange<float>(0.1f, 10.0f, 0.01f, 0.4f), 1.0f, 
            FloatAttr().withStringFromValueFunction(qFormat)));
    }

    return { params.begin(), params.end() };
}

void TroakarSpectralAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    spectralEngine.prepare(sampleRate);
}

void TroakarSpectralAudioProcessor::releaseResources() {}

void TroakarSpectralAudioProcessor::processBlock(
    juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples  = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();
    if (numSamples == 0 || numChannels == 0) return;

    // 1. Input gain c плавным кроссфейдом (убирает щелчки)
    float inGainDb = *apvts.getRawParameterValue("IN_GAIN");
    const float inGainLin = juce::Decibels::decibelsToGain(inGainDb);
    if (std::abs(inGainLin - prevInGain) > 1.0e-4f)
    {
        buffer.applyGainRamp(0, numSamples, prevInGain, inGainLin);
        prevInGain = inGainLin;
    }
    else
    {
        buffer.applyGain(inGainLin);
    }

    // 2. Dry copy для mix
    float mixPct = *apvts.getRawParameterValue("MIX");
    const float mixWet = juce::jlimit(0.0f, 1.0f, mixPct / 100.0f);

    juce::AudioBuffer<float> dryBuffer;
    if (mixWet < 0.999f)
        dryBuffer.makeCopyOf(buffer);

    // 3. Spectral processing
    spectralEngine.refreshGradientBands(gradientManager.bands, getSampleRate());

    float upMax   = *apvts.getRawParameterValue("UPWARD_RANGE");
    float downMax = *apvts.getRawParameterValue("DOWNWARD_RANGE");
    float amount  = *apvts.getRawParameterValue("AMOUNT");
    float speed   = *apvts.getRawParameterValue("SPECTRAL_SPEED");
    float smooth  = *apvts.getRawParameterValue("SMOOTHING");

    spectralEngine.process(buffer, apvts,
                           upMax, downMax, amount, speed, smooth,
                           spectrumDataLeft, compressionDeltaData);

    // 4. Dry/wet mix
    if (mixWet < 0.999f)
    {
        const float mixDry = 1.0f - mixWet;
        for (int ch = 0; ch < numChannels; ++ch)
        {
            auto* wet = buffer.getWritePointer(ch);
            const auto* dry = dryBuffer.getReadPointer(ch);
            for (int i = 0; i < numSamples; ++i)
                wet[i] = wet[i] * mixWet + dry[i] * mixDry;
        }
    }

    // 5. Output gain c плавным кроссфейдом (убирает щелчки)
    float outLvlDb = *apvts.getRawParameterValue("OUT_LVL");
    const float outGainLin = juce::Decibels::decibelsToGain(outLvlDb);
    if (std::abs(outGainLin - prevOutGain) > 1.0e-4f)
    {
        buffer.applyGainRamp(0, numSamples, prevOutGain, outGainLin);
        prevOutGain = outGainLin;
    }
    else
    {
        buffer.applyGain(outGainLin);
    }
}

juce::AudioProcessorEditor* TroakarSpectralAudioProcessor::createEditor()
{
    return new TroakarSpectralAudioProcessorEditor (*this);
}

void TroakarSpectralAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml (state.createXml());
    copyXmlToBinary (*xml, destData);
}

void TroakarSpectralAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xmlState (getXmlFromBinary (data, sizeInBytes));
    if (xmlState != nullptr && xmlState->hasTagName(apvts.state.getType()))
        apvts.replaceState (juce::ValueTree::fromXml (*xmlState));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new TroakarSpectralAudioProcessor();
}
