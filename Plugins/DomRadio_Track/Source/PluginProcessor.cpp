#include "PluginProcessor.h"
#include "PluginEditor.h"

DomRadioTrackAudioProcessor::DomRadioTrackAudioProcessor()
     : AudioProcessor (BusesProperties().withInput("Input", juce::AudioChannelSet::stereo(), true)
                                        .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
        apvts (*this, nullptr, "Parameters", createParameterLayout())
{
    inGainParam = apvts.getRawParameterValue("IN_GAIN");
    driveTypeParam = apvts.getRawParameterValue("DRIVE_TYPE");
    driveParam = apvts.getRawParameterValue("DRIVE");
    tapeDriveParam = apvts.getRawParameterValue("TAPE_DRIVE");
    transientParam = apvts.getRawParameterValue("TRANSIENT");
    tapeSpeedParam = apvts.getRawParameterValue("TAPE_SPEED");
    tapeModelParam = apvts.getRawParameterValue("TAPE_MODEL");
    eqStdParam = apvts.getRawParameterValue("EQ_STD");
    airParam = apvts.getRawParameterValue("AIR");
    biasParam = apvts.getRawParameterValue("BIAS");
    biasSagParam = apvts.getRawParameterValue("BIAS_SAG");
    ironCoreParam = apvts.getRawParameterValue("IRON_CORE");
    bassParam = apvts.getRawParameterValue("BASS");
    trebleParam = apvts.getRawParameterValue("TREBLE");
    bassFreqParam = apvts.getRawParameterValue("BASS_FREQ");
    trebleFreqParam = apvts.getRawParameterValue("TREBLE_FREQ");
    mixParam = apvts.getRawParameterValue("MIX");
    outLvlParam = apvts.getRawParameterValue("OUT_LVL");
    oversamplingParam = apvts.getRawParameterValue("OVERSAMPLING");
    preBassParam = apvts.getRawParameterValue("PRE_BASS");
    preTrebleParam = apvts.getRawParameterValue("PRE_TREBLE");
    preBassFreqParam = apvts.getRawParameterValue("PRE_BASS_FREQ");
    preTrebleFreqParam = apvts.getRawParameterValue("PRE_TREBLE_FREQ");
    detailAmountParam = apvts.getRawParameterValue("DETAIL_AMOUNT");
    detailTiltParam = apvts.getRawParameterValue("DETAIL_TILT");
    detailAlgoParam = apvts.getRawParameterValue("DETAIL_ALGO");
    wowParam = apvts.getRawParameterValue("WOW_AMOUNT");
    flutterParam = apvts.getRawParameterValue("FLUTTER_AMOUNT");
    noiseParam = apvts.getRawParameterValue("TAPE_NOISE");
    ageParam = apvts.getRawParameterValue("AGE");

    startTimerHz(10);
}

DomRadioTrackAudioProcessor::~DomRadioTrackAudioProcessor() {}

void DomRadioTrackAudioProcessor::timerCallback()
{
    if (requiresOversamplingChange.exchange(false))
    {
        suspendProcessing(true);
        prepareToPlay(currentSampleRate, preparedBlockSize);
        suspendProcessing(false);
    }
}

juce::AudioProcessorValueTreeState::ParameterLayout DomRadioTrackAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    auto dbFormat = [](float value, int) {
        const juce::String sign = value > 0.049f ? "+" : "";
        return sign + juce::String(value, 1) + " dB";
    };
    auto dbParse = [](const juce::String& text) {
        return text.retainCharacters("-0123456789.").getFloatValue();
    };
    auto pctFormat = [](float value, int) {
        return juce::String(static_cast<int>(std::round(value))) + " %";
    };
    auto pctParse = [](const juce::String& text) {
        return text.retainCharacters("0123456789.").getFloatValue();
    };
    auto modulationPercentFormat = [](float value, int) {
        return juce::String(static_cast<int>(std::round(value * 100.0f))) + " %";
    };
    auto modulationPercentParse = [](const juce::String& text) {
        return juce::jlimit(0.0f, 1.0f, text.retainCharacters("0123456789.").getFloatValue() / 100.0f);
    };
    using FloatAttr = juce::AudioParameterFloatAttributes;

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "IN_GAIN", "Input Gain", juce::NormalisableRange<float>(-18.0f, 18.0f, 0.1f), 0.0f,
        FloatAttr().withStringFromValueFunction(dbFormat).withValueFromStringFunction(dbParse)));

    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        "DRIVE_TYPE", "Drive Type", juce::StringArray { "Silicon", "Germanium" }, 0));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "DRIVE", "Pre Drive", juce::NormalisableRange<float>(1.0f, 10.0f, 0.1f), 1.0f,
        FloatAttr().withStringFromValueFunction([](float value, int) {
            const float thd = 0.3f + std::pow((value - 1.0f) / 9.0f, 1.5f) * 4.7f;
            return juce::String(value, 1) + " (THD ~" + juce::String(thd, 1) + "%)";
        })));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "TAPE_DRIVE", "Tape Drive", juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), 0.0f,
        FloatAttr().withStringFromValueFunction([](float value, int) {
            return juce::String(static_cast<int>(std::round(value * 100.0f))) + " %";
        }).withValueFromStringFunction(pctParse)));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "TRANSIENT", "Transient", juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), 1.0f,
        FloatAttr().withStringFromValueFunction([](float value, int) {
            if (value >= 0.99f) return juce::String("Bypass");
            const float slewRate = 0.05f + value * value * 12.0f;
            return juce::String(slewRate, 2) + " V/μs";
        })));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "TAPE_SPEED", "Tape Speed",
        juce::NormalisableRange<float>(TroakarDSP::TapesDSP::minTapeSpeedIps, 
                                       TroakarDSP::TapesDSP::maxTapeSpeedIps, 0.001f, 0.55f), 15.0f,
        FloatAttr().withStringFromValueFunction([](float value, int) {
            return juce::String(value, value < 10.0f ? 3 : 2) + " ips";
        })));

    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        "TAPE_MODEL", "Tape Model", 
        juce::StringArray { "SVEMA A4409", "ORWO TYP 106", "SCOTCH 2500", "BASF SPR 50" }, 0));

    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        "EQ_STD", "EQ Standard", juce::StringArray { "CCIR", "NAB" }, 0));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "AIR", "Air", juce::NormalisableRange<float>(0.0f, 15.0f, 0.1f), 0.0f,
        FloatAttr().withStringFromValueFunction([](float value, int) {
            return value < 0.05f ? juce::String("Off") : "+" + juce::String(value, 1) + " dB";
        })));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "BIAS", "Bias", juce::NormalisableRange<float>(-1.0f, 1.0f, 0.01f), 0.0f,
        FloatAttr().withStringFromValueFunction([](float value, int) {
            if (std::abs(value) < 0.01f) return juce::String("Nominal");
            const int percent = static_cast<int>(std::round(std::abs(value) * 50.0f));
            return juce::String(value < 0.0f ? "Under -" : "Over +") + juce::String(percent) + "%";
        })));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "BIAS_SAG", "Bias Sag", juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), 0.0f,
        FloatAttr().withStringFromValueFunction(pctFormat).withValueFromStringFunction(pctParse)));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "IRON_CORE", "Iron Core", juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), 0.0f,
        FloatAttr().withStringFromValueFunction(pctFormat).withValueFromStringFunction(pctParse)));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "BASS", "Bass", juce::NormalisableRange<float>(-18.0f, 18.0f, 0.1f), 0.0f,
        FloatAttr().withStringFromValueFunction(dbFormat).withValueFromStringFunction(dbParse)));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "TREBLE", "Treble", juce::NormalisableRange<float>(-18.0f, 18.0f, 0.1f), 0.0f,
        FloatAttr().withStringFromValueFunction(dbFormat).withValueFromStringFunction(dbParse)));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "BASS_FREQ", "Bass Freq", juce::NormalisableRange<float>(30.0f, 300.0f, 1.0f), 60.0f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "TREBLE_FREQ", "Treble Freq", juce::NormalisableRange<float>(1000.0f, 15000.0f, 10.0f), 10000.0f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "PRE_BASS", "Pre-Bass", juce::NormalisableRange<float>(-18.0f, 18.0f, 0.1f), 0.0f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "PRE_TREBLE", "Pre-Treble", juce::NormalisableRange<float>(-18.0f, 18.0f, 0.1f), 0.0f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "PRE_BASS_FREQ", "Pre-Bass Freq", juce::NormalisableRange<float>(30.0f, 300.0f, 1.0f), 100.0f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "PRE_TREBLE_FREQ", "Pre-Treble Freq", juce::NormalisableRange<float>(1000.0f, 15000.0f, 10.0f), 5000.0f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "DETAIL_AMOUNT", "Detail Amount", juce::NormalisableRange<float>(0.0f, 100.0f, 1.0f), 0.0f,
        FloatAttr().withStringFromValueFunction(pctFormat).withValueFromStringFunction(pctParse)));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "DETAIL_TILT", "Detail Tilt", juce::NormalisableRange<float>(-100.0f, 100.0f, 1.0f), 0.0f,
        FloatAttr().withStringFromValueFunction([](float value, int) {
            if (std::abs(value) < 1.0f) return juce::String("Center");
            return value < 0.0f ? "Low " + juce::String(std::abs(value), 0) + "%" : "Air " + juce::String(value, 0) + "%";
        })));
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        "DETAIL_ALGO", "Detail Algorithm", juce::StringArray { "Wideband Tilt", "Multiband Spectral" }, 1));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "MIX", "Mix", juce::NormalisableRange<float>(0.0f, 100.0f, 1.0f), 100.0f,
        FloatAttr().withStringFromValueFunction(pctFormat).withValueFromStringFunction(pctParse)));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "OUT_LVL", "Output", juce::NormalisableRange<float>(-18.0f, 18.0f, 0.1f), 0.0f,
        FloatAttr().withStringFromValueFunction(dbFormat).withValueFromStringFunction(dbParse)));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "WOW_AMOUNT", "Wow", juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), 0.0f,
        FloatAttr().withStringFromValueFunction(modulationPercentFormat).withValueFromStringFunction(modulationPercentParse)));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "FLUTTER_AMOUNT", "Flutter", juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), 0.0f,
        FloatAttr().withStringFromValueFunction(modulationPercentFormat).withValueFromStringFunction(modulationPercentParse)));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "TAPE_NOISE", "Noise", juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), 0.0f,
        FloatAttr().withStringFromValueFunction(modulationPercentFormat).withValueFromStringFunction(modulationPercentParse)));
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "AGE", "Tape Age", juce::NormalisableRange<float>(0.0f, 50.0f, 1.0f), 0.0f,
        FloatAttr().withStringFromValueFunction([](float value, int) {
            const int years = static_cast<int>(std::round(value));
            return years == 0 ? juce::String("New Reel") : juce::String(years) + " yrs";
        })));

    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        "OVERSAMPLING", "Oversampling", juce::StringArray { "1x", "2x", "4x" }, 1));

    return { params.begin(), params.end() };
}

void DomRadioTrackAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    preparedBlockSize = samplesPerBlock;
    
    const int osIndex = getRequestedOversamplingIndex();
    activeOversamplingIndex = osIndex;
    
    oversampler = std::make_unique<juce::dsp::Oversampling<float>>(
        2, osIndex, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, false
    );
    
    const size_t maxExpectedBlock = static_cast<size_t>(juce::jmax(65536, samplesPerBlock));
    oversampler->initProcessing(maxExpectedBlock);
    oversampler->reset();

    const double osRate = sampleRate * (1 << osIndex);
    const int osMaxBlock = static_cast<int>(maxExpectedBlock) * (1 << osIndex);
    
    osWorkBuffer.setSize(2, static_cast<int>(maxExpectedBlock));

    wetChainL.prepare(osRate, osMaxBlock);
    wetChainR.prepare(osRate, osMaxBlock);

    clipL.prepare(sampleRate);
    clipR.prepare(sampleRate);
    finalDcBlockL.prepare(sampleRate);
    finalDcBlockR.prepare(sampleRate);
    noiseDecayFilterL.prepare(sampleRate);
    noiseDecayFilterR.prepare(sampleRate);
    finalDcBlockL.reset();
    finalDcBlockR.reset();
    
    meterAttackCoeff = 1.0f - std::exp(-1.0f / (0.300f * static_cast<float>(sampleRate)));
    meterReleaseCoeff = 1.0f - std::exp(-1.0f / (0.300f * static_cast<float>(sampleRate)));
    vuEnvL = 0.0f;
    vuEnvR = 0.0f;
    
    setLatencySamples(static_cast<int>(oversampler->getLatencyInSamples()));

    inputGainSmoothed.reset(sampleRate, 0.010);
    driveSmoothed.reset(sampleRate, 0.020);
    tapeDriveSmoothed.reset(sampleRate, 0.020);
    tapeSpeedSmoothed.reset(sampleRate, 0.080);
    transientSmoothed.reset(sampleRate, 0.010);
    biasSmoothed.reset(sampleRate, 0.020);
    biasSagSmoothed.reset(sampleRate, 0.050);
    ironCoreSmoothed.reset(sampleRate, 0.050);
    airSmoothed.reset(sampleRate, 0.020);
    bassSmoothed.reset(sampleRate, 0.020);
    trebleSmoothed.reset(sampleRate, 0.020);
    bassFrequencySmoothed.reset(sampleRate, 0.020);
    trebleFrequencySmoothed.reset(sampleRate, 0.020);
    mixSmoothed.reset(sampleRate, 0.010);
    outputGainSmoothed.reset(sampleRate, 0.010);
    preBassSmoothed.reset(sampleRate, 0.020);
    preTrebleSmoothed.reset(sampleRate, 0.020);
    preBassFreqSmoothed.reset(sampleRate, 0.020);
    preTrebleFreqSmoothed.reset(sampleRate, 0.020);

    inputGainSmoothed.setCurrentAndTargetValue(juce::Decibels::decibelsToGain(inGainParam->load()));
    driveSmoothed.setCurrentAndTargetValue(driveParam->load());
    tapeDriveSmoothed.setCurrentAndTargetValue(tapeDriveParam->load());
    tapeSpeedSmoothed.setCurrentAndTargetValue(tapeSpeedParam->load());
    transientSmoothed.setCurrentAndTargetValue(transientParam->load());
    biasSmoothed.setCurrentAndTargetValue(biasParam->load());
    biasSagSmoothed.setCurrentAndTargetValue(biasSagParam->load());
    ironCoreSmoothed.setCurrentAndTargetValue(ironCoreParam->load());
    airSmoothed.setCurrentAndTargetValue(airParam->load());
    bassSmoothed.setCurrentAndTargetValue(bassParam->load());
    trebleSmoothed.setCurrentAndTargetValue(trebleParam->load());
    bassFrequencySmoothed.setCurrentAndTargetValue(bassFreqParam->load());
    trebleFrequencySmoothed.setCurrentAndTargetValue(trebleFreqParam->load());
    mixSmoothed.setCurrentAndTargetValue(mixParam->load() / 100.0f);
    outputGainSmoothed.setCurrentAndTargetValue(juce::Decibels::decibelsToGain(outLvlParam->load()));
    preBassSmoothed.setCurrentAndTargetValue(preBassParam->load());
    preTrebleSmoothed.setCurrentAndTargetValue(preTrebleParam->load());
    preBassFreqSmoothed.setCurrentAndTargetValue(preBassFreqParam->load());
    preTrebleFreqSmoothed.setCurrentAndTargetValue(preTrebleFreqParam->load());
    
    mechL.prepare(sampleRate, static_cast<int>(maxExpectedBlock));
    mechR.prepare(sampleRate, static_cast<int>(maxExpectedBlock));
    wowGenL.prepare(sampleRate);
    wowGenR.prepare(sampleRate);
    wowGenL.setSeed(101);
    wowGenR.setSeed(202);
    velvetGrainL.prepare(sampleRate, BinaryData::noise_mid_velvet_and_soft_mp3, BinaryData::noise_mid_velvet_and_soft_mp3Size);
    velvetGrainR.prepare(sampleRate, BinaryData::noise_mid_velvet_and_soft_mp3, BinaryData::noise_mid_velvet_and_soft_mp3Size);
    vinylGrainL.prepare(sampleRate, BinaryData::noise_high_vinyl_like_mp3, BinaryData::noise_high_vinyl_like_mp3Size);
    vinylGrainR.prepare(sampleRate, BinaryData::noise_high_vinyl_like_mp3, BinaryData::noise_high_vinyl_like_mp3Size);
    contactL.prepare(sampleRate);
    contactR.prepare(sampleRate);

    DBG("velvet loaded: " << velvetGrainL.getNumLoadedSamples());
    jassert(velvetGrainL.getNumLoadedSamples() > 0);

    wowSmoothed.reset(sampleRate, 0.05);
    flutterSmoothed.reset(sampleRate, 0.05);
    noiseSmoothed.reset(sampleRate, 0.05);
    ageSmoothed.reset(sampleRate, 0.05);
    wowSmoothed.setCurrentAndTargetValue(wowParam ? wowParam->load() : 0.0f);
    flutterSmoothed.setCurrentAndTargetValue(flutterParam ? flutterParam->load() : 0.0f);
    noiseSmoothed.setCurrentAndTargetValue(noiseParam ? noiseParam->load() : 0.0f);
    ageSmoothed.setCurrentAndTargetValue(ageParam ? ageParam->load() : 0.0f);
    
    resetProcessingState();
}

void DomRadioTrackAudioProcessor::releaseResources() {}

int DomRadioTrackAudioProcessor::getRequestedOversamplingIndex() const noexcept
{
    return juce::jlimit(0, 2, static_cast<int>(oversamplingParam->load()));
}

void DomRadioTrackAudioProcessor::resetProcessingState()
{
    inputSatEnvelope = 0.0f;
    tapeSatEnvelope = 0.0f;
    inputSaturationLevel.store(0.0f);
    tapeSaturationLevel.store(0.0f);
    displayDetailActivity.store(0.0f);
    
    finalDcBlockL.reset();
    finalDcBlockR.reset();
    noiseDecayFilterL.reset();
    noiseDecayFilterR.reset();
    vuEnvL = 0.0f;
    vuEnvR = 0.0f;
    
    if (oversampler) oversampler->reset();
    wetChainL.reset();
    wetChainR.reset();

    wowGenL.prepare(currentSampleRate);
    wowGenR.prepare(currentSampleRate);
    wowGenL.setSeed(101);
    wowGenR.setSeed(202);
    velvetGrainL.reset();
    velvetGrainR.reset();
    vinylGrainL.reset();
    vinylGrainR.reset();
}

void DomRadioTrackAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    if (oversampler == nullptr)
    {
        buffer.clear();
        return;
    }

    const int requested = getRequestedOversamplingIndex();
    if (requested != activeOversamplingIndex && !requiresOversamplingChange.load())
        requiresOversamplingChange.store(true);

    const int numSamples = buffer.getNumSamples();
    if (numSamples > 65536) return;

    inputGainSmoothed.setTargetValue(juce::Decibels::decibelsToGain(inGainParam->load()));
    driveSmoothed.setTargetValue(driveParam->load());
    tapeDriveSmoothed.setTargetValue(tapeDriveParam->load());
    tapeSpeedSmoothed.setTargetValue(tapeSpeedParam->load());
    transientSmoothed.setTargetValue(transientParam->load());
    biasSmoothed.setTargetValue(biasParam->load());
    biasSagSmoothed.setTargetValue(biasSagParam->load());
    ironCoreSmoothed.setTargetValue(ironCoreParam->load());
    airSmoothed.setTargetValue(airParam->load());
    bassSmoothed.setTargetValue(bassParam->load());
    trebleSmoothed.setTargetValue(trebleParam->load());
    bassFrequencySmoothed.setTargetValue(bassFreqParam->load());
    trebleFrequencySmoothed.setTargetValue(trebleFreqParam->load());
    mixSmoothed.setTargetValue(mixParam->load() / 100.0f);
    outputGainSmoothed.setTargetValue(juce::Decibels::decibelsToGain(outLvlParam->load()));
    preBassSmoothed.setTargetValue(preBassParam->load());
    preTrebleSmoothed.setTargetValue(preTrebleParam->load());
    preBassFreqSmoothed.setTargetValue(preBassFreqParam->load());
    preTrebleFreqSmoothed.setTargetValue(preTrebleFreqParam->load());
    wowSmoothed.setTargetValue(wowParam->load());
    flutterSmoothed.setTargetValue(flutterParam->load());
    noiseSmoothed.setTargetValue(noiseParam->load());

    driveSmoothed.skip(numSamples);
    tapeDriveSmoothed.skip(numSamples);
    tapeSpeedSmoothed.skip(numSamples);
    transientSmoothed.skip(numSamples);
    biasSmoothed.skip(numSamples);
    biasSagSmoothed.skip(numSamples);
    ironCoreSmoothed.skip(numSamples);
    airSmoothed.skip(numSamples);
    bassSmoothed.skip(numSamples);
    trebleSmoothed.skip(numSamples);
    bassFrequencySmoothed.skip(numSamples);
    trebleFrequencySmoothed.skip(numSamples);
    mixSmoothed.skip(numSamples);
    preBassSmoothed.skip(numSamples);
    preTrebleSmoothed.skip(numSamples);
    preBassFreqSmoothed.skip(numSamples);
    preTrebleFreqSmoothed.skip(numSamples);
    wowSmoothed.skip(numSamples);
    flutterSmoothed.skip(numSamples);

    if (inputGainSmoothed.isSmoothing())
    {
        for (int i = 0; i < numSamples; ++i)
        {
            const float g = inputGainSmoothed.getNextValue();
            for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
                buffer.setSample(ch, i, buffer.getSample(ch, i) * g);
        }
    }
    else buffer.applyGain(inputGainSmoothed.getCurrentValue());

    if (osWorkBuffer.getNumSamples() < numSamples)
        osWorkBuffer.setSize(2, juce::jmax(65536, numSamples), false, false, true);

    osWorkBuffer.copyFrom(0, 0, buffer.getReadPointer(0), numSamples);
    if (buffer.getNumChannels() == 1) osWorkBuffer.copyFrom(1, 0, buffer.getReadPointer(0), numSamples);
    else osWorkBuffer.copyFrom(1, 0, buffer.getReadPointer(1), numSamples);

    const float tapeSpeedIps = juce::jlimit(TroakarDSP::TapesDSP::minTapeSpeedIps, TroakarDSP::TapesDSP::maxTapeSpeedIps, tapeSpeedSmoothed.getCurrentValue());
    const float tapeSpeedNorm = TroakarDSP::TapesDSP::speedIpsToNorm(tapeSpeedIps) * 0.75f;
    int eqStd = static_cast<int>(eqStdParam->load());
    int tapeModel = static_cast<int>(tapeModelParam->load());
    const auto tapeProfile = TroakarDSP::TapesDSP::getBalancedProfile(tapeModel);
    float airGain = airSmoothed.getCurrentValue();
    const float biasRaw = juce::jlimit(-1.0f, 1.0f, biasSmoothed.getCurrentValue());
    const float mixAmount = mixSmoothed.getCurrentValue();
    const float currentAge = ageSmoothed.getCurrentValue();
    float tapeBias = 0.0f;
    if (std::abs(biasRaw) > 0.001f)
    {
        const float sign = biasRaw >= 0.0f ? 1.0f : -1.0f;
        const float normalized = std::abs(biasRaw);
        float shaped = normalized < 0.5f ? normalized * 1.5f : 0.75f + 0.25f * ((normalized - 0.5f) * 2.0f) * ((normalized - 0.5f) * 2.0f) * (3.0f - 2.0f * ((normalized - 0.5f) * 2.0f));
        tapeBias = sign * shaped * 1.15f;
    }

    const float detAmount = detailAmountParam->load() / 100.0f * mixAmount;
    const float detTilt = detailTiltParam->load() / 100.0f;
    const auto detAlgo = static_cast<TroakarDSP::ExtractorAlgorithm>(detailAlgoParam->load());

    wetChainL.eq.updateBiasResponse(tapeBias, tapeSpeedNorm, mixAmount);
    wetChainR.eq.updateBiasResponse(tapeBias, tapeSpeedNorm, mixAmount);

    {
        juce::dsp::AudioBlock<float> fullBlock (osWorkBuffer);
        auto block = fullBlock.getSubBlock (0, (size_t) numSamples);
        auto osBlock = oversampler->processSamplesUp (block);

        const int osNumSamples = (int) osBlock.getNumSamples();
        auto* osL = osBlock.getChannelPointer(0);
        auto* osR = osBlock.getChannelPointer(1);

        if (tapeSpeedNorm != lastTapeSpeed || eqStd != lastEqStd || tapeModel != lastTapeModel
            || std::abs(airGain - lastAirGain) > 0.005f || std::abs(mixAmount - lastMixAmount) > 0.005f
            || std::abs(currentAge - lastAge) > 0.05f)
        {
            isEqUpdating.store(true, std::memory_order_release);
            wetChainL.eq.updateParameters(tapeSpeedNorm, eqStd, airGain, 0.0f, currentAge, tapeProfile, mixAmount);
            wetChainR.eq.updateParameters(tapeSpeedNorm, eqStd, airGain, 0.0f, currentAge, tapeProfile, mixAmount);
            wetChainL.trans.updateParameters(mixAmount);
            wetChainR.trans.updateParameters(mixAmount);
            wetChainL.tapeProfile.updateProfile(tapeProfile);
            wetChainR.tapeProfile.updateProfile(tapeProfile);
            isEqUpdating.store(false, std::memory_order_release);

            lastTapeSpeed = tapeSpeedNorm;
            lastEqStd = eqStd;
            lastTapeModel = tapeModel;
            lastAirGain = airGain;
            lastMixAmount = mixAmount;
            lastAge = currentAge;
        }

        if (std::abs(bassSmoothed.getCurrentValue() * mixAmount - lastBass) > 0.01f || std::abs(trebleSmoothed.getCurrentValue() * mixAmount - lastTreble) > 0.01f
            || std::abs(bassFrequencySmoothed.getCurrentValue() - lastBassFrequency) > 0.5f || std::abs(trebleFrequencySmoothed.getCurrentValue() - lastTrebleFrequency) > 5.0f)
        {
            wetChainL.pultec.updateCoefficients(bassSmoothed.getCurrentValue() * mixAmount, trebleSmoothed.getCurrentValue() * mixAmount, bassFrequencySmoothed.getCurrentValue(), trebleFrequencySmoothed.getCurrentValue());
            wetChainR.pultec.updateCoefficients(bassSmoothed.getCurrentValue() * mixAmount, trebleSmoothed.getCurrentValue() * mixAmount, bassFrequencySmoothed.getCurrentValue(), trebleFrequencySmoothed.getCurrentValue());
            lastBass = bassSmoothed.getCurrentValue() * mixAmount;
            lastTreble = trebleSmoothed.getCurrentValue() * mixAmount;
            lastBassFrequency = bassFrequencySmoothed.getCurrentValue();
            lastTrebleFrequency = trebleFrequencySmoothed.getCurrentValue();
        }

        if (std::abs(preBassSmoothed.getCurrentValue() * mixAmount - lastPreBass) > 0.01f || std::abs(preTrebleSmoothed.getCurrentValue() * mixAmount - lastPreTreble) > 0.01f
            || std::abs(preBassFreqSmoothed.getCurrentValue() - lastPreBassFreq) > 0.5f || std::abs(preTrebleFreqSmoothed.getCurrentValue() - lastPreTrebleFreq) > 5.0f)
        {
            wetChainL.emphasis.updateCoefficients(preBassSmoothed.getCurrentValue() * mixAmount, preTrebleSmoothed.getCurrentValue() * mixAmount, preBassFreqSmoothed.getCurrentValue(), preTrebleFreqSmoothed.getCurrentValue());
            wetChainR.emphasis.updateCoefficients(preBassSmoothed.getCurrentValue() * mixAmount, preTrebleSmoothed.getCurrentValue() * mixAmount, preBassFreqSmoothed.getCurrentValue(), preTrebleFreqSmoothed.getCurrentValue());
            lastPreBass = preBassSmoothed.getCurrentValue() * mixAmount;
            lastPreTreble = preTrebleSmoothed.getCurrentValue() * mixAmount;
            lastPreBassFreq = preBassFreqSmoothed.getCurrentValue();
            lastPreTrebleFreq = preTrebleFreqSmoothed.getCurrentValue();
        }

        const float effMix = mixSmoothed.getCurrentValue();
        const float biasSag = biasSagSmoothed.getCurrentValue() * mixAmount;
        const float ironCore = ironCoreSmoothed.getCurrentValue() * mixAmount;
        const float transient = 1.0f - ((1.0f - transientSmoothed.getCurrentValue()) * mixAmount);

        int driveType = static_cast<int>(driveTypeParam->load());
        const float requestedDrive = driveSmoothed.getCurrentValue();
        const float drive = juce::jlimit(1.0f, 10.0f, requestedDrive);

        const float preampDriveNorm = juce::jlimit(0.0f, 1.0f, (drive - 1.0f) / 9.0f);
        const float preampDriveShape = std::pow(preampDriveNorm, 1.25f);
        const float effectivePreampPressure = juce::jlimit(0.70f, 1.60f, 0.70f + preampDriveShape * 0.90f);
        const float effectivePreampDrive = juce::jlimit(1.0f, 10.0f, 1.0f + preampDriveNorm * 9.0f * effectivePreampPressure);

        const float tapeDriveRaw = juce::jlimit(0.0f, 1.0f, tapeDriveSmoothed.getCurrentValue());
        constexpr float tapeDriveStart = 0.04f;
        constexpr float maxInternalTapeDrive = 0.55f; 
        const float tapeDriveAfterThreshold = juce::jlimit(0.0f, 1.0f, (tapeDriveRaw - tapeDriveStart) / (1.0f - tapeDriveStart));
        const float tapeActivityNorm = std::pow(tapeDriveAfterThreshold, 1.65f);
        const float tapeDriveControl = tapeActivityNorm * maxInternalTapeDrive;

        const float effectiveTapePressure = juce::jlimit(1.00f, 1.45f, 1.00f + tapeActivityNorm * 0.45f);

        displayEffectivePreampActivity.store(juce::jlimit(0.0f, 1.0f, preampDriveNorm * effectivePreampPressure * 0.55f), std::memory_order_relaxed);
        displayEffectiveTapeActivity.store(juce::jlimit(0.0f, 1.0f, tapeActivityNorm * effectiveTapePressure * 0.55f), std::memory_order_relaxed);

        const float driveTypeComp = (driveType == 1) ? 1.10f : 1.0f;
        const float preampCompensation = 1.0f / (1.0f + std::pow(preampDriveNorm, 0.70f) * 2.15f * driveTypeComp);
        const float tapeCompensation = 1.0f / (1.0f + tapeActivityNorm * 0.65f);
        const float crossCoupling = 1.0f + (preampDriveNorm * tapeActivityNorm * 0.45f);
        const float rawPostGain = (preampCompensation * tapeCompensation) / crossCoupling;
        const float finalPostGain = 1.0f + (rawPostGain - 1.0f) * effMix;

        const auto tapeFormat = TroakarDSP::TapesDSP::getFormatForSpeedIps(tapeSpeedIps);
        const float headroomScale = juce::Decibels::decibelsToGain(12.0f - tapeFormat.headroomDb);
        const float headroomMakeup = 1.0f / headroomScale;

        auto processWetChannel = [&](float input, auto& chain, float& inActivity, float& tapeActivity) -> float
        {
            float wet = input;
            wet = chain.emphasis.processPre(wet);
            const float inputBefore = wet;

            wet = chain.trans.process(wet, ironCore, effMix);
            wet = chain.slew.process(wet, transient);
            wet = chain.spiral.process(wet, effectivePreampDrive, 0.1f, effMix);
            inActivity = std::abs(wet - inputBefore);

            if (biasSag > 0.001f) {
                const float sag = chain.biasSag.process(wet, biasSag);
                const float sagEffect = sag * 5.0f;
                wet *= (1.0f - juce::jlimit(0.0f, 0.65f, sagEffect * 0.35f));
                wet += wet * std::abs(wet) * (sagEffect * 1.5f + sagEffect * sagEffect * 2.5f) * TroakarDSP::TapesDSP::harmonicDistortionTrim;
            }

            wet = chain.eq.processPre(wet);
            const float tapeCoreBefore = wet;
            
            wet *= headroomScale;
            wet = chain.tapeCore.process(wet, tapeDriveControl, effectiveTapePressure, tapeBias, tapeProfile);
            wet *= headroomMakeup;
            
            const float tapeCoreActivity = std::abs(wet - tapeCoreBefore);
            const float profileBefore = wet;
            wet = chain.tapeProfile.process(wet, tapeDriveControl, effectiveTapePressure, tapeBias, tapeSpeedNorm, effMix);
            const float profileActivity = std::abs(wet - profileBefore);
            tapeActivity = juce::jlimit(0.0f, 1.0f, tapeCoreActivity + profileActivity);
            
            wet = chain.dcBlock.process(wet);
            wet = chain.eq.processPost(wet);
            wet = chain.emphasis.processPost(wet);
            wet = chain.pultec.process(wet);
            wet = chain.detailExtractor.process(wet, detAmount, detTilt, detAlgo);

            return wet * finalPostGain;
        };

        for (int i = 0; i < osNumSamples; ++i)
        {
            float actInL = 0.0f, actTpL = 0.0f;
            osL[i] = processWetChannel(osL[i], wetChainL, actInL, actTpL);

            float actInR = 0.0f, actTpR = 0.0f;
            osR[i] = processWetChannel(osR[i], wetChainR, actInR, actTpR);
        }

        oversampler->processSamplesDown(block);

        displayDetailActivity.store(
            juce::jlimit(0.0f, 1.0f,
                std::max(wetChainL.detailExtractor.getCurrentDetailGainDb(),
                         wetChainR.detailExtractor.getCurrentDetailGainDb()) / 18.0f),
            std::memory_order_relaxed);
    }

    buffer.copyFrom(0, 0, osWorkBuffer.getReadPointer(0), numSamples);
    if (buffer.getNumChannels() > 1) buffer.copyFrom(1, 0, osWorkBuffer.getReadPointer(1), numSamples);

    auto* wetL = buffer.getWritePointer(0);
    auto* wetR = buffer.getNumChannels() > 1 ? buffer.getWritePointer(1) : nullptr;

    const float effWow = wowSmoothed.getNextValue();
    const float effFlutter = flutterSmoothed.getNextValue() * 0.5f;
    const float effNoise = noiseSmoothed.getNextValue() * mixAmount; 

    for (int i = 0; i < numSamples; ++i)
    {
        const float currentOutput = outputGainSmoothed.getNextValue();
        float finalL = wetL[i];
        float finalR = wetR != nullptr ? wetR[i] : 0.0f;

        const float currentAge = ageSmoothed.getNextValue();

        const auto modMaster = wowGenL.generate(tapeSpeedNorm, effWow, effFlutter);
        const auto modR_raw  = wowGenR.generate(tapeSpeedNorm, effWow, effFlutter);

        TroakarDSP::WowFlutterModulation modL = modMaster;
        TroakarDSP::WowFlutterModulation modR;
        modR.wow     = modMaster.wow     * 0.90f + modR_raw.wow     * 0.10f;
        modR.flutter = modMaster.flutter * 0.90f + modR_raw.flutter * 0.10f;

        if (i == 0) {
            displayWow.store(juce::jlimit(-1.0f, 1.0f, modMaster.wow), std::memory_order_relaxed);
            displayFlutter.store(juce::jlimit(-1.0f, 1.0f, modMaster.flutter), std::memory_order_relaxed);
        }

        finalL = mechL.process(finalL, tapeSpeedNorm, mixAmount, 0.0f, currentAge, true, modL);
        if (wetR != nullptr)
            finalR = mechR.process(finalR, tapeSpeedNorm, mixAmount, 0.0f, currentAge, false, modR);

        const float currentNoise = noiseSmoothed.getNextValue() * mixAmount;
        const float ageNorm      = juce::jlimit(0.0f, 1.0f, currentAge / 50.0f);

        if (currentNoise > 0.0001f) {
            const auto modeEnum = TroakarDSP::NoiseMode::dynamicNoise;

            float midGrainL  = velvetGrainL.process(finalL, modeEnum, tapeSpeedNorm, ageNorm);
            float highGrainL = vinylGrainL.process(finalL, modeEnum, tapeSpeedNorm, ageNorm);
            float combinedGrainL = midGrainL * (1.0f - ageNorm * 0.2f) + highGrainL * (0.30f + ageNorm * 0.70f);

            float noiseCurve = currentNoise * (0.45f + 0.55f * currentNoise);
            float additiveNoiseL = combinedGrainL * noiseCurve * (0.45f + ageNorm * 0.15f);
            float contactNoiseL = contactL.process(finalL, currentAge, 0.0f, modeEnum) * currentNoise;
            float rawNoiseSumL = (additiveNoiseL + contactNoiseL) * 0.6f;

            float noiseCutoff = juce::jlimit(4000.0f, 20000.0f, 18000.0f - ageNorm * 4000.0f - currentNoise * 2000.0f);
            if (i % 32 == 0) {
                noiseDecayFilterL.setLowPass(noiseCutoff, 0.707f);
                noiseDecayFilterR.setLowPass(noiseCutoff, 0.707f);
            }
            finalL += noiseDecayFilterL.processSample(rawNoiseSumL);

            if (wetR != nullptr) {
                float midGrainR  = velvetGrainR.process(finalR, modeEnum, tapeSpeedNorm, ageNorm);
                float highGrainR = vinylGrainR.process(finalR, modeEnum, tapeSpeedNorm, ageNorm);
                float combinedGrainR = midGrainR * (1.0f - ageNorm * 0.2f) + highGrainR * (0.30f + ageNorm * 0.70f);

                float additiveNoiseR = combinedGrainR * noiseCurve * (0.45f + ageNorm * 0.15f);
                float contactNoiseR = contactR.process(finalR, currentAge, 0.0f, modeEnum) * currentNoise;
                float rawNoiseSumR = (additiveNoiseR + contactNoiseR) * 0.6f;

                finalR += noiseDecayFilterR.processSample(rawNoiseSumR);
            }
        }

        wetL[i] = finalDcBlockL.process(clipL.process(finalL * currentOutput, mixAmount));
        const float absL = std::abs(wetL[i]);
        vuEnvL += (absL > vuEnvL ? meterAttackCoeff : meterReleaseCoeff) * (absL - vuEnvL);

        if (wetR != nullptr) {
            wetR[i] = finalDcBlockR.process(clipR.process(finalR * currentOutput, mixAmount));
            const float absR = std::abs(wetR[i]);
            vuEnvR += (absR > vuEnvR ? meterAttackCoeff : meterReleaseCoeff) * (absR - vuEnvR);
        }
    }

    auto linToVU = [](float lin) {
        if (lin <= 1.0e-4f) return 0.0f;
        const float db = juce::Decibels::gainToDecibels(lin);
        return juce::jlimit(0.0f, 1.0f, (db + 48.0f) / 48.0f);
    };

    meterLevelLeft.store(linToVU(vuEnvL));
    meterLevelRight.store(linToVU(vuEnvR));

    const float signalPresence = juce::jlimit(0.0f, 1.0f, (vuEnvL + vuEnvR) * 0.707f * 3.0f); 
    const float driveSatFactor = juce::jlimit(0.0f, 1.0f, (driveSmoothed.getCurrentValue() - 1.0f) / 5.0f);
    const float ironSatFactor  = juce::jlimit(0.0f, 1.0f, ironCoreSmoothed.getCurrentValue() * 1.5f);
    const float slewSatFactor  = juce::jlimit(0.0f, 1.0f, (1.0f - transientSmoothed.getCurrentValue()) * 1.5f);
    const float targetInputSat = juce::jlimit(0.0f, 1.0f, (driveSatFactor * 0.60f + ironSatFactor * 0.35f + slewSatFactor * 0.20f) * TroakarDSP::TapesDSP::harmonicDistortionTrim * signalPresence);
    const float tapeDriveFactor = juce::jlimit(0.0f, 1.0f, tapeDriveSmoothed.getCurrentValue() * 1.25f);
    const float biasSatFactor   = juce::jlimit(0.0f, 0.5f, std::abs(tapeBias) * 1.2f);
    const float profileSatMult  = 1.0f + (tapeProfile.oddHarmonics + tapeProfile.evenHarmonics) * 4.0f;
    const float targetTapeSat = juce::jlimit(0.0f, 1.0f, (tapeDriveFactor * 0.75f + biasSatFactor * 0.25f) * profileSatMult * TroakarDSP::TapesDSP::harmonicDistortionTrim * signalPresence);

    const float inCoeff   = (targetInputSat > inputSatEnvelope) ? 0.40f : 0.12f;
    const float tapeCoeff = (targetTapeSat > tapeSatEnvelope)   ? 0.40f : 0.12f;
    
    inputSatEnvelope += inCoeff * (targetInputSat - inputSatEnvelope);
    tapeSatEnvelope  += tapeCoeff * (targetTapeSat - tapeSatEnvelope);

    inputSaturationLevel.store(inputSatEnvelope);
    tapeSaturationLevel.store(tapeSatEnvelope);
}

double DomRadioTrackAudioProcessor::getCompositeMagnitude(double frequency) const noexcept
{
    if (isEqUpdating.load(std::memory_order_acquire)) return 1.0;
    const double hpf = frequency / std::sqrt(frequency * frequency + 30.0 * 30.0);
    return hpf * wetChainL.eq.getMagnitudeForFrequency(frequency) * wetChainL.pultec.getMagnitudeForFrequency(frequency);
}

double DomRadioTrackAudioProcessor::getEmphasisMagnitude(double frequency) const noexcept
{
    if (isEqUpdating.load(std::memory_order_acquire)) return 1.0;
    return wetChainL.emphasis.getMagnitudeForFrequency(frequency);
}

void DomRadioTrackAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty("eqMonitorExpanded", eqMonitorExpanded, nullptr);
    
    std::unique_ptr<juce::XmlElement> xml (state.createXml());
    copyXmlToBinary (*xml, destData);
}

void DomRadioTrackAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xmlState (getXmlFromBinary (data, sizeInBytes));
    if (xmlState != nullptr && xmlState->hasTagName (apvts.state.getType()))
    {
        apvts.replaceState (juce::ValueTree::fromXml (*xmlState));
        eqMonitorExpanded = apvts.state.getProperty("eqMonitorExpanded", false);
    }
}

juce::AudioProcessorEditor* DomRadioTrackAudioProcessor::createEditor()
{
    return new DomRadioTrackAudioProcessorEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new DomRadioTrackAudioProcessor();
}
