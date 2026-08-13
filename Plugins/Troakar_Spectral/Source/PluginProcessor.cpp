#include "PluginProcessor.h"
#include "PluginEditor.h"

TroakarSpectralAudioProcessor::TroakarSpectralAudioProcessor()
     : AudioProcessor (BusesProperties()
                       .withInput  ("Input",     juce::AudioChannelSet::stereo(), true)
                       .withOutput ("Output",    juce::AudioChannelSet::stereo(), true)
                       .withInput  ("Sidechain", juce::AudioChannelSet::stereo(), false)),
       apvts (*this, nullptr, "Parameters", createParameterLayout())
{
    smoothedMix.setCurrentAndTargetValue(1.0f);
}

bool TroakarSpectralAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo() ||
        layouts.getMainInputChannelSet()  != juce::AudioChannelSet::stereo())
        return false;

    if (layouts.getChannelSet(true, 1) != juce::AudioChannelSet::disabled() &&
        layouts.getChannelSet(true, 1) != juce::AudioChannelSet::stereo() &&
        layouts.getChannelSet(true, 1) != juce::AudioChannelSet::mono())
        return false;

    return true;
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
        juce::NormalisableRange<float>(0.0f, 150.0f, 1.0f), 0.0f, 
        FloatAttr().withStringFromValueFunction(pctFormat)));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "GLOBAL_THRESH", "Threshold", 
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

        params.push_back(std::make_unique<juce::AudioParameterBool>(
            idPrefix + "_ENABLE", namePrefix + " Enabled", false));
        float defaultFreq = (i == 0) ? 100.0f : (i == 1) ? 5000.0f : 1000.0f * (i + 1);
        
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

    for (int g = 0; g < 4; ++g)
    {
        juce::String idPrefix = "GRADIENT_" + juce::String(g);
        juce::String namePrefix = "Gradient " + juce::String(g + 1);

        params.push_back(std::make_unique<juce::AudioParameterBool>(
            idPrefix + "_ENABLE", namePrefix + " Enabled", false));

        params.push_back(std::make_unique<juce::AudioParameterFloat>(
            idPrefix + "_CENTER_FREQ", namePrefix + " Center Freq",
            juce::NormalisableRange<float>(20.0f, 20000.0f, 0.1f, 0.25f),
            (g == 0) ? 100.0f : (g == 1) ? 500.0f : (g == 2) ? 2500.0f : 8000.0f,
            FloatAttr().withStringFromValueFunction(hzFormat)));

        params.push_back(std::make_unique<juce::AudioParameterFloat>(
            idPrefix + "_CENTER_GAIN", namePrefix + " Center Gain (Thresh Offset)",
            juce::NormalisableRange<float>(-24.0f, 24.0f, 0.1f), 0.0f,
            FloatAttr().withStringFromValueFunction(dbFormat)));

        params.push_back(std::make_unique<juce::AudioParameterFloat>(
            idPrefix + "_BANDWIDTH", namePrefix + " Bandwidth",
            juce::NormalisableRange<float>(0.5f, 4.0f, 0.1f), 1.5f,
            FloatAttr().withStringFromValueFunction([](float v, int) { return juce::String(v, 2) + " oct"; })));

        params.push_back(std::make_unique<juce::AudioParameterFloat>(
            idPrefix + "_AMOUNT", namePrefix + " Amount",
            juce::NormalisableRange<float>(0.0f, 150.0f, 1.0f), 100.0f,
            FloatAttr().withStringFromValueFunction(pctFormat)));

        params.push_back(std::make_unique<juce::AudioParameterFloat>(
            idPrefix + "_UP_MAX", namePrefix + " Up Max",
            juce::NormalisableRange<float>(0.0f, 24.0f, 0.1f), 4.0f,
            FloatAttr().withStringFromValueFunction(dbFormat)));

        params.push_back(std::make_unique<juce::AudioParameterFloat>(
            idPrefix + "_DOWN_MAX", namePrefix + " Down Max",
            juce::NormalisableRange<float>(-24.0f, 0.0f, 0.1f), -4.0f,
            FloatAttr().withStringFromValueFunction(dbFormat)));

        params.push_back(std::make_unique<juce::AudioParameterFloat>(
            idPrefix + "_SPEED", namePrefix + " Speed",
            juce::NormalisableRange<float>(0.0f, 100.0f, 1.0f), 50.0f,
            FloatAttr().withStringFromValueFunction(pctFormat)));

        params.push_back(std::make_unique<juce::AudioParameterFloat>(
            idPrefix + "_SMOOTH", namePrefix + " Smooth",
            juce::NormalisableRange<float>(0.0f, 100.0f, 1.0f), 20.0f,
            FloatAttr().withStringFromValueFunction(pctFormat)));
    }

    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        "FFT_MODE", "FFT Size / Precision",
        juce::StringArray { "512 (Fast / Low Latency)", "1024 (Balanced)", "2048 (Precise / Mastering)" }, 0));

    return { params.begin(), params.end() };
}

void TroakarSpectralAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    spectralEngine.prepare(sampleRate);
    
    dryDelayBuffer.setSize(2, 2048);
    dryDelayBuffer.clear();
    dryDelayIndex = 0;

    audioThreadGradients.resize(4);
    smoothedMix.reset(sampleRate, 0.04);

    setLatencySamples(currentFFTSize);
}

void TroakarSpectralAudioProcessor::releaseResources() {}

void TroakarSpectralAudioProcessor::processBlock(
    juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples  = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();
    if (numSamples == 0 || numChannels == 0) return;

    int fftMode = static_cast<int>(*apvts.getRawParameterValue("FFT_MODE"));
    if (fftMode != prevFFTMode)
    {
        prevFFTMode = fftMode;
        currentFFTSize = (fftMode == 0) ? 512 : (fftMode == 1) ? 1024 : 2048;

        spectralEngine.switchFFTSize(currentFFTSize);
        setLatencySamples(currentFFTSize);
        updateHostDisplay();
    }

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

    // 2. Dry copy для mix (С ИДЕАЛЬНОЙ ФАЗОВОЙ КОМПЕНСАЦИЕЙ ЗАДЕРЖКИ)
    float mixPct = *apvts.getRawParameterValue("MIX");
    smoothedMix.setTargetValue(mixPct / 100.0f);

    juce::AudioBuffer<float> delayedDryBuffer;
    delayedDryBuffer.setSize(numChannels, numSamples, false, false, true);

    for (int ch = 0; ch < numChannels; ++ch)
    {
        const float* dryIn = buffer.getReadPointer(ch);
        float* dryOut = delayedDryBuffer.getWritePointer(ch);
        float* delayData = dryDelayBuffer.getWritePointer(ch);

        int tempIndex = dryDelayIndex;
        for (int i = 0; i < numSamples; ++i)
        {
            dryOut[i] = delayData[tempIndex];
            delayData[tempIndex] = dryIn[i];
            
            if (++tempIndex >= currentFFTSize) tempIndex = 0;
        }
    }
    
    dryDelayIndex = (dryDelayIndex + numSamples) % currentFFTSize;

    // 3. Spectral processing
    float upMax   = *apvts.getRawParameterValue("UPWARD_RANGE");
    float downMax = *apvts.getRawParameterValue("DOWNWARD_RANGE");
    float amount  = *apvts.getRawParameterValue("AMOUNT");
    float speed   = *apvts.getRawParameterValue("SPECTRAL_SPEED");
    float smooth  = *apvts.getRawParameterValue("SMOOTHING");

    // ЛЕНИВЫЕ ВЫЧИСЛЕНИЯ: Проверяем, трогал ли пользователь ручки
    bool engineDirty = syncGradientPointsFromAPVTS();
    if (upMax != prevUpMax || downMax != prevDownMax || amount != prevAmount || 
        speed != prevSpeed || smooth != prevSmooth) 
    {
        engineDirty = true;
        prevUpMax = upMax; prevDownMax = downMax; prevAmount = amount;
        prevSpeed = speed; prevSmooth = smooth;
    }

    // Если трогал — пересчитываем массивы коэффициентов
    if (engineDirty) {
        spectralEngine.updateParameters(upMax, downMax, amount, speed, smooth, 
                                        audioThreadGradients, getSampleRate());
    }

    // Сигнатура process стала чище, передаем только буферы
    auto mainBuffer = getBusBuffer(buffer, true, 0);
    auto sidechainBuffer = getBusBuffer(buffer, true, 1);

    bool hasSidechain = (getBusCount(true) > 1 && 
                         getBus(true, 1)->isEnabled() && 
                         sidechainBuffer.getNumChannels() > 0);

    spectralEngine.process(mainBuffer, 
                           hasSidechain ? &sidechainBuffer : nullptr, 
                           apvts, spectrumDataLeft, compressionDeltaData);

    static int debugCounter = 0;
    if (++debugCounter >= 100) {
        debugCounter = 0;

        float minDelta = 999.0f, maxDelta = -999.0f, avgDelta = 0.0f;
        int validBins = 0;

        for (int i = 10; i < 200; ++i) {
            float delta = compressionDeltaData[i].load(std::memory_order_relaxed);
            if (std::abs(delta) < 30.0f) {
                minDelta = juce::jmin(minDelta, delta);
                maxDelta = juce::jmax(maxDelta, delta);
                avgDelta += delta;
                ++validBins;
            }
        }

        if (validBins > 0) {
            avgDelta /= validBins;

            DBG("=== GAIN STAGING ===");
            DBG("Input level: " + juce::String(juce::Decibels::gainToDecibels(buffer.getRMSLevel(0, 0, buffer.getNumSamples())), 1) + " dB");
            DBG("Min gain delta: " + juce::String(minDelta, 2) + " dB");
            DBG("Max gain delta: " + juce::String(maxDelta, 2) + " dB");
            DBG("Avg gain delta: " + juce::String(avgDelta, 2) + " dB");
            DBG("===================");
        }
    }

    // 4. Dry/wet mix с плавным сглаживанием (РЕШАЕТ ЩЕЛЧКИ ПРИ АВТОМАТИЗАЦИИ)
    for (int ch = 0; ch < numChannels; ++ch)
    {
        auto* wet = buffer.getWritePointer(ch);
        const auto* dry = delayedDryBuffer.getReadPointer(ch);
        
        for (int i = 0; i < numSamples; ++i)
        {
            float mixWet = smoothedMix.getNextValue();
            float mixDry = 1.0f - mixWet;
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

bool TroakarSpectralAudioProcessor::syncGradientPointsFromAPVTS()
{
    bool changed = false;
    for (size_t i = 0; i < audioThreadGradients.size() && i < 4; ++i)
    {
        juce::String prefix = "GRADIENT_" + juce::String((int)i);
        auto& point = audioThreadGradients[i];

        bool  active = *apvts.getRawParameterValue(prefix + "_ENABLE") > 0.5f;
        float freq   = *apvts.getRawParameterValue(prefix + "_CENTER_FREQ");
        float gain   = *apvts.getRawParameterValue(prefix + "_CENTER_GAIN");
        float bw     = *apvts.getRawParameterValue(prefix + "_BANDWIDTH");
        float amt    = *apvts.getRawParameterValue(prefix + "_AMOUNT");
        float up     = *apvts.getRawParameterValue(prefix + "_UP_MAX");
        float dn     = *apvts.getRawParameterValue(prefix + "_DOWN_MAX");
        float spd    = *apvts.getRawParameterValue(prefix + "_SPEED");
        float sm     = *apvts.getRawParameterValue(prefix + "_SMOOTH");

        if (point.active != active || point.centerFreqHz != freq || point.centerGainDb != gain ||
            point.radiusOctaves != bw || point.amountPct != amt ||
            point.upMaxDb != up || point.downMaxDb != dn ||
            point.speedPct != spd || point.smoothPct != sm) 
        {
            changed = true;
            point.active = active;
            point.centerFreqHz = freq;
            point.centerGainDb = gain;
            point.radiusOctaves = bw;
            point.amountPct = amt;
            point.upMaxDb = up;
            point.downMaxDb = dn;
            point.speedPct = spd;
            point.smoothPct = sm;
        }
    }
    return changed;
}

void TroakarSpectralAudioProcessor::syncGradientPointsToAPVTS()
{
    for (size_t i = 0; i < 4; ++i)
    {
        juce::String prefix = "GRADIENT_" + juce::String((int)i);
        auto* enableParam = apvts.getParameter(prefix + "_ENABLE");
        
        if (i < gradientManager.points.size())
        {
            const auto& point = gradientManager.points[i];
            auto* freqParam   = apvts.getParameter(prefix + "_CENTER_FREQ");
            auto* gainParam   = apvts.getParameter(prefix + "_CENTER_GAIN");
            auto* bwParam     = apvts.getParameter(prefix + "_BANDWIDTH");
            auto* amountParam = apvts.getParameter(prefix + "_AMOUNT");
            auto* upParam     = apvts.getParameter(prefix + "_UP_MAX");
            auto* downParam   = apvts.getParameter(prefix + "_DOWN_MAX");
            auto* speedParam  = apvts.getParameter(prefix + "_SPEED");
            auto* smoothParam = apvts.getParameter(prefix + "_SMOOTH");

            enableParam->setValueNotifyingHost(point.active ? 1.0f : 0.0f);
            freqParam->setValueNotifyingHost(freqParam->convertTo0to1(point.centerFreqHz));
            gainParam->setValueNotifyingHost(gainParam->convertTo0to1(point.centerGainDb));
            bwParam->setValueNotifyingHost(bwParam->convertTo0to1(point.radiusOctaves));
            amountParam->setValueNotifyingHost(amountParam->convertTo0to1(point.amountPct));
            upParam->setValueNotifyingHost(upParam->convertTo0to1(point.upMaxDb));
            downParam->setValueNotifyingHost(downParam->convertTo0to1(point.downMaxDb));
            speedParam->setValueNotifyingHost(speedParam->convertTo0to1(point.speedPct));
            smoothParam->setValueNotifyingHost(smoothParam->convertTo0to1(point.smoothPct));
        }
        else
        {
            enableParam->setValueNotifyingHost(0.0f);
        }
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new TroakarSpectralAudioProcessor();
}
