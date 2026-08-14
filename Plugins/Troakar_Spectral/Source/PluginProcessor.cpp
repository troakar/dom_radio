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

    pInGain    = apvts.getRawParameterValue("IN_GAIN");
    pOutLvl    = apvts.getRawParameterValue("OUT_LVL");
    pMix       = apvts.getRawParameterValue("MIX");
    pAmount    = apvts.getRawParameterValue("AMOUNT");
    pThresh    = apvts.getRawParameterValue("GLOBAL_THRESH");
    pUpRange   = apvts.getRawParameterValue("UPWARD_RANGE");
    pDownRange = apvts.getRawParameterValue("DOWNWARD_RANGE");
    pSpeed     = apvts.getRawParameterValue("SPECTRAL_SPEED");
    pSmooth    = apvts.getRawParameterValue("SMOOTHING");
    pUpSel     = apvts.getRawParameterValue("UP_SEL");
    pDownSel   = apvts.getRawParameterValue("DOWN_SEL");
    pFftMode   = apvts.getRawParameterValue("FFT_MODE");
    pDeltaMode = apvts.getRawParameterValue("DELTA_MODE");
    pViewRange = apvts.getRawParameterValue("VIEW_RANGE");

    pSpeedAuto = apvts.getRawParameterValue("SPEED_AUTO");
    pAttackMs = apvts.getRawParameterValue("ATTACK_MS");
    pReleaseMs = apvts.getRawParameterValue("RELEASE_MS");
    pKneeWidth = apvts.getRawParameterValue("KNEE_WIDTH");
    pLookaheadMs = apvts.getRawParameterValue("LOOKAHEAD_MS");

    for (int i = 0; i < 4; ++i) {
        juce::String prefix = "GRADIENT_" + juce::String(i);
        pGradEnable[i]  = apvts.getRawParameterValue(prefix + "_ENABLE");
        pGradFreq[i]    = apvts.getRawParameterValue(prefix + "_CENTER_FREQ");
        pGradGain[i]    = apvts.getRawParameterValue(prefix + "_CENTER_GAIN");
        pGradBw[i]      = apvts.getRawParameterValue(prefix + "_BANDWIDTH");
        pGradAmt[i]     = apvts.getRawParameterValue(prefix + "_AMOUNT");
        pGradUpMax[i]   = apvts.getRawParameterValue(prefix + "_UP_MAX");
        pGradDownMax[i] = apvts.getRawParameterValue(prefix + "_DOWN_MAX");
        pGradSpeed[i]   = apvts.getRawParameterValue(prefix + "_SPEED");
        pGradSmooth[i]  = apvts.getRawParameterValue(prefix + "_SMOOTH");
        pGradUpSel[i]   = apvts.getRawParameterValue(prefix + "_UP_SEL");
        pGradDownSel[i] = apvts.getRawParameterValue(prefix + "_DOWN_SEL");
        pGradAutoSpeed[i] = apvts.getRawParameterValue(prefix + "_AUTO_SPEED");
        pGradAttack[i]   = apvts.getRawParameterValue(prefix + "_ATTACK");
        pGradRelease[i]  = apvts.getRawParameterValue(prefix + "_RELEASE");
        pGradKnee[i]     = apvts.getRawParameterValue(prefix + "_KNEE");
    }

    for (int i = 0; i < 8; ++i) {
        juce::String prefix = "BAND_" + juce::String(i);
        pBandEnable[i] = apvts.getRawParameterValue(prefix + "_ENABLE");
        pBandFreq[i]   = apvts.getRawParameterValue(prefix + "_FREQ");
        pBandGain[i]   = apvts.getRawParameterValue(prefix + "_GAIN");
        pBandQ[i]      = apvts.getRawParameterValue(prefix + "_Q");

        apvts.addParameterListener(prefix + "_ENABLE", this);
        apvts.addParameterListener(prefix + "_FREQ", this);
        apvts.addParameterListener(prefix + "_GAIN", this);
        apvts.addParameterListener(prefix + "_Q", this);
    }
    
    spectralEngine.linkParameters(pBandEnable, pBandFreq, pBandGain, pBandQ);
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

TroakarSpectralAudioProcessor::~TroakarSpectralAudioProcessor() 
{
    for (int i = 0; i < 8; ++i) {
        juce::String prefix = "BAND_" + juce::String(i);
        apvts.removeParameterListener(prefix + "_ENABLE", this);
        apvts.removeParameterListener(prefix + "_FREQ", this);
        apvts.removeParameterListener(prefix + "_GAIN", this);
        apvts.removeParameterListener(prefix + "_Q", this);
    }
}

void TroakarSpectralAudioProcessor::parameterChanged (const juce::String& parameterID, float /*newValue*/)
{
    if (parameterID.startsWith("BAND_")) {
        spectralEngine.invalidateTarget();
    }
}

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

    params.push_back(std::make_unique<juce::AudioParameterBool>(
        "IO_LINK", "In/Out Gain Link", false));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "AMOUNT", "Comp Amount", 
        juce::NormalisableRange<float>(0.0f, 300.0f, 1.0f, 0.65f), 100.0f,
        FloatAttr().withStringFromValueFunction(pctFormat)));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "GLOBAL_THRESH", "Threshold", 
        juce::NormalisableRange<float>(-48.0f, 12.0f, 0.1f), 0.0f, 
        FloatAttr().withStringFromValueFunction(dbFormat)));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "UPWARD_RANGE", "Max Upward Gain", 
        juce::NormalisableRange<float>(0.0f, 48.0f, 0.1f, 0.70f), 4.0f,
        FloatAttr().withStringFromValueFunction(dbFormat)));
    
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "DOWNWARD_RANGE", "Max Downward Red.", 
        juce::NormalisableRange<float>(0.0f, 24.0f, 0.1f), 12.0f, 
        FloatAttr().withStringFromValueFunction(dbFormat)));
    
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "SPECTRAL_SPEED", "Reaction Speed", 
        juce::NormalisableRange<float>(0.0f, 100.0f, 1.0f), 50.0f, 
        FloatAttr().withStringFromValueFunction(pctFormat)));
    
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "SMOOTHING", "Freq Smoothing", 
        juce::NormalisableRange<float>(0.0f, 100.0f, 1.0f), 20.0f, 
        FloatAttr().withStringFromValueFunction(pctFormat)));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "UP_SEL", "Up Selectivity", 
        juce::NormalisableRange<float>(-100.0f, 100.0f, 1.0f), 0.0f, 
        FloatAttr().withStringFromValueFunction(pctFormat)));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "DOWN_SEL", "Down Selectivity", 
        juce::NormalisableRange<float>(-100.0f, 100.0f, 1.0f), 0.0f, 
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
            juce::NormalisableRange<float>(-60.0f, 60.0f, 0.1f), 0.0f, 
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
            juce::NormalisableRange<float>(-60.0f, 60.0f, 0.1f), 0.0f,
            FloatAttr().withStringFromValueFunction(dbFormat)));

        params.push_back(std::make_unique<juce::AudioParameterFloat>(
            idPrefix + "_BANDWIDTH", namePrefix + " Bandwidth",
            juce::NormalisableRange<float>(0.5f, 4.0f, 0.1f), 1.5f,
            FloatAttr().withStringFromValueFunction([](float v, int) { return juce::String(v, 2) + " oct"; })));

        params.push_back(std::make_unique<juce::AudioParameterFloat>(
            idPrefix + "_AMOUNT", namePrefix + " Amount",
            juce::NormalisableRange<float>(0.0f, 300.0f, 1.0f, 0.65f), 100.0f,
            FloatAttr().withStringFromValueFunction(pctFormat)));

        params.push_back(std::make_unique<juce::AudioParameterFloat>(
            idPrefix + "_UP_MAX", namePrefix + " Up Max",
            juce::NormalisableRange<float>(0.0f, 48.0f, 0.1f, 0.70f), 4.0f,
            FloatAttr().withStringFromValueFunction(dbFormat)));

        params.push_back(std::make_unique<juce::AudioParameterFloat>(
            idPrefix + "_DOWN_MAX", namePrefix + " Down Max",
            juce::NormalisableRange<float>(0.0f, 24.0f, 0.1f), 12.0f,
            FloatAttr().withStringFromValueFunction(dbFormat)));

        params.push_back(std::make_unique<juce::AudioParameterFloat>(
            idPrefix + "_SPEED", namePrefix + " Speed",
            juce::NormalisableRange<float>(0.0f, 100.0f, 1.0f), 50.0f,
            FloatAttr().withStringFromValueFunction(pctFormat)));

        params.push_back(std::make_unique<juce::AudioParameterFloat>(
            idPrefix + "_SMOOTH", namePrefix + " Smooth",
            juce::NormalisableRange<float>(0.0f, 100.0f, 1.0f), 20.0f,
            FloatAttr().withStringFromValueFunction(pctFormat)));

        params.push_back(std::make_unique<juce::AudioParameterFloat>(
            idPrefix + "_UP_SEL", namePrefix + " Up Sel",
            juce::NormalisableRange<float>(-100.0f, 100.0f, 1.0f), 0.0f,
            FloatAttr().withStringFromValueFunction(pctFormat)));

        params.push_back(std::make_unique<juce::AudioParameterFloat>(
            idPrefix + "_DOWN_SEL", namePrefix + " Down Sel",
            juce::NormalisableRange<float>(-100.0f, 100.0f, 1.0f), 0.0f,
            FloatAttr().withStringFromValueFunction(pctFormat)));
    }

    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        "FFT_MODE", "FFT Size / Precision",
        juce::StringArray { "512 (Fast / Low Latency)", "1024 (Balanced)", "2048 (Precise / Mastering)" }, 0));

    params.push_back(std::make_unique<juce::AudioParameterBool>(
        "DELTA_MODE", "Delta Listen", false));

    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        "VIEW_RANGE", "View Depth",
        juce::StringArray {
            "24 dB (Mastering)",
            "48 dB (Mixing)",
            "72 dB (Deep)",
            "96 dB (Full Spectrum)",
            "120 dB (Extended)"
        },
        3));

    params.push_back(std::make_unique<juce::AudioParameterBool>(
        "SPEED_AUTO", "Speed Auto Mode", true));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "ATTACK_MS", "Attack Time",
        juce::NormalisableRange<float>(0.1f, 100.0f, 0.1f, 0.3f), 5.0f,
        FloatAttr().withStringFromValueFunction([](float v, int) { 
            return juce::String(v, 1) + " ms"; 
        })));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "RELEASE_MS", "Release Time",
        juce::NormalisableRange<float>(10.0f, 1000.0f, 1.0f, 0.3f), 150.0f,
        FloatAttr().withStringFromValueFunction([](float v, int) { 
            return juce::String((int)v) + " ms"; 
        })));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "KNEE_WIDTH", "Knee Width",
        juce::NormalisableRange<float>(0.0f, 12.0f, 0.1f), 3.0f,
        FloatAttr().withStringFromValueFunction([](float v, int) { 
            return juce::String(v, 1) + " dB"; 
        })));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "LOOKAHEAD_MS", "Look-Ahead",
        juce::NormalisableRange<float>(0.0f, 10.0f, 0.1f), 5.0f,
        FloatAttr().withStringFromValueFunction([](float v, int) { 
            return juce::String(v, 1) + " ms"; 
        })));

    for (int g = 0; g < 4; ++g)
    {
        juce::String idPrefix = "GRADIENT_" + juce::String(g);
        juce::String namePrefix = "Gradient " + juce::String(g + 1);

        params.push_back(std::make_unique<juce::AudioParameterBool>(
            idPrefix + "_AUTO_SPEED", namePrefix + " Auto Speed", true));

        params.push_back(std::make_unique<juce::AudioParameterFloat>(
            idPrefix + "_ATTACK", namePrefix + " Attack",
            juce::NormalisableRange<float>(0.1f, 100.0f, 0.1f, 0.3f), 5.0f,
            FloatAttr().withStringFromValueFunction([](float v, int) { 
                return juce::String(v, 1) + " ms"; 
            })));

        params.push_back(std::make_unique<juce::AudioParameterFloat>(
            idPrefix + "_RELEASE", namePrefix + " Release",
            juce::NormalisableRange<float>(10.0f, 1000.0f, 1.0f, 0.3f), 150.0f,
            FloatAttr().withStringFromValueFunction([](float v, int) { 
                return juce::String((int)v) + " ms"; 
            })));

        params.push_back(std::make_unique<juce::AudioParameterFloat>(
            idPrefix + "_KNEE", namePrefix + " Knee",
            juce::NormalisableRange<float>(0.0f, 12.0f, 0.1f), 3.0f,
            FloatAttr().withStringFromValueFunction([](float v, int) { 
                return juce::String(v, 1) + " dB"; 
            })));
    }

    return { params.begin(), params.end() };
}

void TroakarSpectralAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    spectralEngine.prepare(sampleRate);
    visualFFTSize.store(currentFFTSize, std::memory_order_release);

    for (int i = 0; i < MAX_FFT_BINS; ++i) {
        spectrumDataLeft[i].store(-100.0f, std::memory_order_relaxed);
        compressionDeltaData[i].store(0.0f, std::memory_order_relaxed);
        sidechainData[i].store(-100.0f, std::memory_order_relaxed);
    }

    juce::dsp::ProcessSpec spec;
    spec.sampleRate = sampleRate;
    spec.maximumBlockSize = static_cast<juce::uint32>(samplesPerBlock);
    spec.numChannels = 2;
    dryDelay.prepare(spec);
    dryDelay.setDelay(currentFFTSize);

    delayedDryBuffer.setSize(2, MAX_BLOCK_SIZE);
    delayedDryBuffer.clear();

    smoothedMix.reset(sampleRate, 0.04);

    setLatencySamples(currentFFTSize + spectralEngine.getLookaheadSamples());
}

void TroakarSpectralAudioProcessor::releaseResources() {}

void TroakarSpectralAudioProcessor::handleAsyncUpdate()
{
    if (requiresLatencyUpdate.exchange(false))
    {
        setLatencySamples(currentFFTSize + spectralEngine.getLookaheadSamples());
        updateHostDisplay();
    }
}

void TroakarSpectralAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    if (numSamples == 0) return;

    if (numSamples > MAX_BLOCK_SIZE) return;

    auto mainBus = getBusBuffer(buffer, true, 0);
    int mainChannels = juce::jmin(mainBus.getNumChannels(), 2);
    
    auto sidechainBus = (getBusCount(true) > 1 && getBus(true, 1)->isEnabled()) 
                        ? getBusBuffer(buffer, true, 1) 
                        : juce::AudioBuffer<float>();
    bool hasSidechain = sidechainBus.getNumChannels() > 0;

    for (int ch = 0; ch < mainChannels; ++ch) {
        const float* rawIn = mainBus.getReadPointer(ch);
        float* dryOut = delayedDryBuffer.getWritePointer(ch);

        for (int i = 0; i < numSamples; ++i) {
            dryDelay.pushSample(ch, rawIn[i]);
            dryOut[i] = dryDelay.popSample(ch);
        }
    }

    float inGainDb = *pInGain;
    const float inGainLin = juce::Decibels::decibelsToGain(inGainDb);
    const float inGainStart = prevInGain;

    if (std::abs(inGainLin - prevInGain) > 1.0e-4f) {
        mainBus.applyGainRamp(0, numSamples, prevInGain, inGainLin);
        prevInGain = inGainLin;
    } else {
        mainBus.applyGain(inGainLin);
    }

    // =========================================================================
    // 4. WET ПУТЬ: СПЕКТРАЛЬНАЯ ОБРАБОТКА
    // =========================================================================
    float upMax   = *pUpRange;
    float downMax = -(*pDownRange);
    float amount  = *pAmount;
    float speed   = *pSpeed;
    float smooth  = *pSmooth;
    float upSel   = *pUpSel;
    float downSel = *pDownSel;

    float attackMs  = *pAttackMs;
    float releaseMs = *pReleaseMs;
    float speedAuto = *pSpeedAuto;
    float kneeWidth = *pKneeWidth;
    float lookahead = *pLookaheadMs;

    bool engineDirty = false;

    int fftMode = static_cast<int>(*pFftMode);
    if (fftMode != prevFFTMode) {
        prevFFTMode = fftMode;
        currentFFTSize = (fftMode == 0) ? 512 : (fftMode == 1) ? 1024 : 2048;
        spectralEngine.switchFFTSize(currentFFTSize);
        dryDelay.setDelay(currentFFTSize); 

        for (int i = 0; i < MAX_FFT_BINS; ++i) {
            spectrumDataLeft[i].store(-100.0f, std::memory_order_relaxed);
            compressionDeltaData[i].store(0.0f, std::memory_order_relaxed);
            sidechainData[i].store(-100.0f, std::memory_order_relaxed);
        }

        engineDirty = true;
        spectralEngine.invalidateTarget();

        requiresLatencyUpdate.store(true);
        triggerAsyncUpdate();
    }

    engineDirty |= syncGradientPointsFromAPVTS();

    if (upMax != prevUpMax || downMax != prevDownMax || amount != prevAmount || 
        speed != prevSpeed || smooth != prevSmooth || upSel != prevUpSel || downSel != prevDownSel ||
        attackMs != prevAttackMs || releaseMs != prevReleaseMs || speedAuto != prevSpeedAuto ||
        kneeWidth != prevKneeWidth || lookahead != prevLookaheadMs) 
    {
        engineDirty = true;
        prevUpMax = upMax; prevDownMax = downMax; prevAmount = amount;
        prevSpeed = speed; prevSmooth = smooth; prevUpSel = upSel; prevDownSel = downSel;
        prevAttackMs = attackMs; prevReleaseMs = releaseMs; prevSpeedAuto = speedAuto;
        prevKneeWidth = kneeWidth; prevLookaheadMs = lookahead;
    }

    if (engineDirty) {
        spectralEngine.updateParameters(upMax, downMax, amount, speed, smooth, upSel, downSel,
            *pSpeedAuto > 0.5f, *pAttackMs, *pReleaseMs, *pKneeWidth, *pLookaheadMs,
            audioThreadGradients, getSampleRate());
    }

    spectralEngine.process(mainBus, hasSidechain ? &sidechainBus : nullptr, *pThresh, spectrumDataLeft, compressionDeltaData, sidechainData);

    visualFFTSize.store(currentFFTSize, std::memory_order_release);

    float outLvlDb = *pOutLvl;
    const float outGainLin = juce::Decibels::decibelsToGain(outLvlDb);
    const float outGainStart = prevOutGain;

    if (std::abs(outGainLin - prevOutGain) > 1.0e-4f) {
        mainBus.applyGainRamp(0, numSamples, prevOutGain, outGainLin);
        prevOutGain = outGainLin;
    } else {
        mainBus.applyGain(outGainLin);
    }

    float mixPct = *pMix;
    smoothedMix.setTargetValue(mixPct / 100.0f);
    bool deltaMode = *pDeltaMode > 0.5f;

    for (int i = 0; i < numSamples; ++i) {
        float mixWet = smoothedMix.getNextValue();
        float mixDry = 1.0f - mixWet;

        for (int ch = 0; ch < mainChannels; ++ch) {
            auto* wet = mainBus.getWritePointer(ch);
            const auto* dry = delayedDryBuffer.getReadPointer(ch);
            
            float fullyProcessedWet = wet[i]; 

            if (deltaMode) {
                const float t = numSamples > 1
                              ? static_cast<float>(i) / static_cast<float>(numSamples - 1)
                              : 1.0f;

                const float inGainAt =
                    inGainStart + (inGainLin - inGainStart) * t;

                const float outGainAt =
                    outGainStart + (outGainLin - outGainStart) * t;

                const float unprocessedWetReference =
                    dry[i] * inGainAt * outGainAt;

                wet[i] = fullyProcessedWet - unprocessedWetReference;
            } else {
                wet[i] = (fullyProcessedWet * mixWet) + (dry[i] * mixDry);
            }
        }
    }

    for (int ch = mainChannels; ch < buffer.getNumChannels(); ++ch) {
        buffer.clear(ch, 0, numSamples);
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
    {
        apvts.replaceState (juce::ValueTree::fromXml (*xmlState));
        
        gradientManager.points.clear();
        
        for (int i = 0; i < 4; ++i)
        {
            juce::String prefix = "GRADIENT_" + juce::String(i);
            
            if (*apvts.getRawParameterValue(prefix + "_ENABLE") > 0.5f)
            {
                GradientPoint pt;
                pt.id = i;
                pt.name = "G" + juce::String(i + 1);
                pt.color = gradientManager.availableColors[i % gradientManager.availableColors.size()];
                pt.active = true;
                pt.isSelected = false;
                
                pt.centerFreqHz    = *apvts.getRawParameterValue(prefix + "_CENTER_FREQ");
                pt.centerGainDb    = *apvts.getRawParameterValue(prefix + "_CENTER_GAIN");
                pt.radiusOctaves   = *apvts.getRawParameterValue(prefix + "_BANDWIDTH");
                pt.amountPct       = *apvts.getRawParameterValue(prefix + "_AMOUNT");
                pt.upMaxDb         = *apvts.getRawParameterValue(prefix + "_UP_MAX");
                pt.downMaxDb       = -(*apvts.getRawParameterValue(prefix + "_DOWN_MAX"));
                pt.speedPct        = *apvts.getRawParameterValue(prefix + "_SPEED");
                pt.smoothPct       = *apvts.getRawParameterValue(prefix + "_SMOOTH");
                pt.upSelectivity   = *apvts.getRawParameterValue(prefix + "_UP_SEL");
                pt.downSelectivity = *apvts.getRawParameterValue(prefix + "_DOWN_SEL");
                
                pt.useAutoSpeed    = *apvts.getRawParameterValue(prefix + "_AUTO_SPEED") > 0.5f;
                pt.attackMs        = *apvts.getRawParameterValue(prefix + "_ATTACK");
                pt.releaseMs       = *apvts.getRawParameterValue(prefix + "_RELEASE");
                pt.kneeWidthDb     = *apvts.getRawParameterValue(prefix + "_KNEE");
                
                gradientManager.points.push_back(pt);
            }
        }

        spectralEngine.invalidateTarget();
    }
}

bool TroakarSpectralAudioProcessor::syncGradientPointsFromAPVTS()
{
    bool changed = false;
    for (size_t i = 0; i < 4; ++i)
    {
        auto& point = audioThreadGradients[i];

        bool  active = *pGradEnable[i] > 0.5f;
        float freq   = *pGradFreq[i];
        float gain   = *pGradGain[i];
        float bw     = *pGradBw[i];
        float amt    = *pGradAmt[i];
        float up     = *pGradUpMax[i];
        float dn     = -(*pGradDownMax[i]);
        float spd    = *pGradSpeed[i];
        float sm     = *pGradSmooth[i];
        float upSel  = *pGradUpSel[i];
        float dnSel  = *pGradDownSel[i];
        bool  autoSpd = *pGradAutoSpeed[i] > 0.5f;
        float atk     = *pGradAttack[i];
        float rel     = *pGradRelease[i];
        float knee    = *pGradKnee[i];

        if (point.active != active || point.centerFreqHz != freq || point.centerGainDb != gain ||
            point.radiusOctaves != bw || point.amountPct != amt ||
            point.upMaxDb != up || point.downMaxDb != dn ||
            point.speedPct != spd || point.smoothPct != sm ||
            point.upSelectivity != upSel || point.downSelectivity != dnSel ||
            point.useAutoSpeed != autoSpd || point.attackMs != atk || 
            point.releaseMs != rel || point.kneeWidthDb != knee) 
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
            point.upSelectivity = upSel;
            point.downSelectivity = dnSel;
            point.useAutoSpeed = autoSpd;
            point.attackMs = atk;
            point.releaseMs = rel;
            point.kneeWidthDb = knee;
        }
    }
    return changed;
}

void TroakarSpectralAudioProcessor::syncGradientPointsToAPVTS()
{
    for (int i = 0; i < 4; ++i)
    {
        juce::String prefix = "GRADIENT_" + juce::String(i);
        auto* enableParam = apvts.getParameter(prefix + "_ENABLE");
        
        auto* pointPtr = gradientManager.getPoint(i);
        
        if (pointPtr != nullptr)
        {
            const auto& point = *pointPtr;
            auto* freqParam   = apvts.getParameter(prefix + "_CENTER_FREQ");
            auto* gainParam   = apvts.getParameter(prefix + "_CENTER_GAIN");
            auto* bwParam     = apvts.getParameter(prefix + "_BANDWIDTH");
            auto* amountParam = apvts.getParameter(prefix + "_AMOUNT");
            auto* upParam     = apvts.getParameter(prefix + "_UP_MAX");
            auto* downParam   = apvts.getParameter(prefix + "_DOWN_MAX");
            auto* speedParam  = apvts.getParameter(prefix + "_SPEED");
            auto* smoothParam = apvts.getParameter(prefix + "_SMOOTH");
            auto* upSelParam  = apvts.getParameter(prefix + "_UP_SEL");
            auto* dnSelParam  = apvts.getParameter(prefix + "_DOWN_SEL");
            auto* autoSpeedParam = apvts.getParameter(prefix + "_AUTO_SPEED");
            auto* attackParam    = apvts.getParameter(prefix + "_ATTACK");
            auto* releaseParam   = apvts.getParameter(prefix + "_RELEASE");
            auto* kneeParam      = apvts.getParameter(prefix + "_KNEE");

            enableParam->setValueNotifyingHost(1.0f);
            freqParam->setValueNotifyingHost(freqParam->convertTo0to1(point.centerFreqHz));
            gainParam->setValueNotifyingHost(gainParam->convertTo0to1(point.centerGainDb));
            bwParam->setValueNotifyingHost(bwParam->convertTo0to1(point.radiusOctaves));
            amountParam->setValueNotifyingHost(amountParam->convertTo0to1(point.amountPct));
            upParam->setValueNotifyingHost(upParam->convertTo0to1(point.upMaxDb));
            downParam->setValueNotifyingHost(downParam->convertTo0to1(-point.downMaxDb));
            speedParam->setValueNotifyingHost(speedParam->convertTo0to1(point.speedPct));
            smoothParam->setValueNotifyingHost(smoothParam->convertTo0to1(point.smoothPct));
            upSelParam->setValueNotifyingHost(upSelParam->convertTo0to1(point.upSelectivity));
            dnSelParam->setValueNotifyingHost(dnSelParam->convertTo0to1(point.downSelectivity));
            
            autoSpeedParam->setValueNotifyingHost(point.useAutoSpeed ? 1.0f : 0.0f);
            attackParam->setValueNotifyingHost(attackParam->convertTo0to1(point.attackMs));
            releaseParam->setValueNotifyingHost(releaseParam->convertTo0to1(point.releaseMs));
            kneeParam->setValueNotifyingHost(kneeParam->convertTo0to1(point.kneeWidthDb));
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
