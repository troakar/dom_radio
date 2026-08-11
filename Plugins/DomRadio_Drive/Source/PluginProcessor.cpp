#include "PluginProcessor.h"

DomRadioDriveAudioProcessor::DomRadioDriveAudioProcessor()
     : AudioProcessor (BusesProperties()
                       .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
       apvts (*this, nullptr, "Parameters", createParameterLayout())
{
    inGainParam    = apvts.getRawParameterValue("IN_GAIN");
    driveTypeParam = apvts.getRawParameterValue("DRIVE_TYPE");
    driveParam     = apvts.getRawParameterValue("DRIVE");
    ironCoreParam  = apvts.getRawParameterValue("IRON_CORE");
    mixParam       = apvts.getRawParameterValue("MIX");
    outLvlParam    = apvts.getRawParameterValue("OUT_LVL");

    toleranceModel.setMode(TroakarDSP::ToleranceModel::UnitMode::Calibrated);
}

DomRadioDriveAudioProcessor::~DomRadioDriveAudioProcessor() {}

juce::AudioProcessorValueTreeState::ParameterLayout DomRadioDriveAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    auto dbFormat = [](float value, int) { return juce::String(value, 1) + " dB"; };
    auto pctFormat = [](float value, int) { return juce::String(static_cast<int>(std::round(value))) + " %"; };

    using FloatAttr = juce::AudioParameterFloatAttributes;

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "IN_GAIN", "Input Gain", juce::NormalisableRange<float>(-18.0f, 18.0f, 0.1f), 0.0f,
        FloatAttr().withStringFromValueFunction(dbFormat)));

    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        "DRIVE_TYPE", "Drive Type", juce::StringArray { "Silicon (Modern)", "Germanium (Vintage)" }, 0));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "DRIVE", "Drive", juce::NormalisableRange<float>(1.0f, 10.0f, 0.1f), 1.0f));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "IRON_CORE", "Iron Core (Transformer)", juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), 0.0f,
        FloatAttr().withStringFromValueFunction([](float v, int){ return juce::String(v * 100.0f, 0) + " %"; })));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "MIX", "Mix", juce::NormalisableRange<float>(0.0f, 100.0f, 1.0f), 100.0f,
        FloatAttr().withStringFromValueFunction(pctFormat)));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        "OUT_LVL", "Output Level", juce::NormalisableRange<float>(-18.0f, 18.0f, 0.1f), 0.0f,
        FloatAttr().withStringFromValueFunction(dbFormat)));

    return { params.begin(), params.end() };
}

void DomRadioDriveAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    const size_t maxBlockSize = (size_t) juce::jmax(2048, samplesPerBlock);
    
    oversampler = std::make_unique<juce::dsp::Oversampling<float>>(
        2, 1, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, false
    );
    oversampler->initProcessing(maxBlockSize);
    oversampler->reset();

    const double osRate = sampleRate * 2.0;
    const int osMaxBlock = (int)maxBlockSize * 2;

    osWorkBuffer.setSize(2, (int)maxBlockSize);

    transL.prepare(osRate, osMaxBlock);
    transR.prepare(osRate, osMaxBlock);
    transL.setTolerances(&toleranceModel.getTolerancesL());
    transR.setTolerances(&toleranceModel.getTolerancesR());

    spiralL.prepare(); spiralR.prepare();
    spiralL.setTolerances(&toleranceModel.getTolerancesL());
    spiralR.setTolerances(&toleranceModel.getTolerancesR());

    clipL.prepare(sampleRate); clipR.prepare(sampleRate);
    clipL.setTolerances(&toleranceModel.getTolerancesL());
    clipR.setTolerances(&toleranceModel.getTolerancesR());

    dcBlockL.prepare(sampleRate); dcBlockR.prepare(sampleRate);
    dcBlockL.reset(); dcBlockR.reset();

    setLatencySamples(static_cast<int>(oversampler->getLatencyInSamples()));

    inGainSmoothed.reset(sampleRate, 0.010);
    driveSmoothed.reset(sampleRate, 0.020);
    ironCoreSmoothed.reset(sampleRate, 0.050);
    mixSmoothed.reset(sampleRate, 0.010);
    outLvlSmoothed.reset(sampleRate, 0.010);

    inGainSmoothed.setCurrentAndTargetValue(juce::Decibels::decibelsToGain(inGainParam->load()));
    driveSmoothed.setCurrentAndTargetValue(driveParam->load());
    ironCoreSmoothed.setCurrentAndTargetValue(ironCoreParam->load());
    mixSmoothed.setCurrentAndTargetValue(mixParam->load() / 100.0f);
    outLvlSmoothed.setCurrentAndTargetValue(juce::Decibels::decibelsToGain(outLvlParam->load()));
}

void DomRadioDriveAudioProcessor::releaseResources() {}

void DomRadioDriveAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int numSamples = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();

    if (numSamples == 0 || oversampler == nullptr) return;

    if (osWorkBuffer.getNumSamples() < numSamples)
        osWorkBuffer.setSize(2, numSamples, false, false, true);

    inGainSmoothed.setTargetValue(juce::Decibels::decibelsToGain(inGainParam->load()));
    driveSmoothed.setTargetValue(driveParam->load());
    ironCoreSmoothed.setTargetValue(ironCoreParam->load());
    mixSmoothed.setTargetValue(mixParam->load() / 100.0f);
    outLvlSmoothed.setTargetValue(juce::Decibels::decibelsToGain(outLvlParam->load()));

    const int driveType = static_cast<int>(driveTypeParam->load());
    const float currentDrive = driveSmoothed.getCurrentValue();
    const float currentIron  = ironCoreSmoothed.getCurrentValue();
    const float currentMix   = mixSmoothed.getCurrentValue();

    driveSmoothed.skip(numSamples);
    ironCoreSmoothed.skip(numSamples);
    mixSmoothed.skip(numSamples);

    // --- BOUTIQUE EQUAL-LOUDNESS GAIN STAGING (Из старших версий) ---
    const float driveNorm = juce::jlimit(0.0f, 1.0f, (currentDrive - 1.0f) / 9.0f);
    const float preampDriveShape = std::pow(driveNorm, 1.25f);
    const float effectivePreampPressure = juce::jlimit(0.70f, 1.60f, 0.70f + preampDriveShape * 0.90f);
    const float effectivePreampDrive = juce::jlimit(1.0f, 10.0f, 1.0f + driveNorm * 9.0f * effectivePreampPressure);

    const float hfeMultiplier = (driveType == 1) ? 1.15f : 1.0f; // Винтажный германий злее
    const float finalDrive = effectivePreampDrive * hfeMultiplier;
    const float presence = 0.1f + (finalDrive * 0.02f);

    const float driveTypeComp = (driveType == 1) ? 1.10f : 1.0f; 
    const float preampCompensation = 1.0f / (1.0f + std::pow(driveNorm, 0.70f) * 2.15f * driveTypeComp);
    const float finalPostGain = 1.0f + (preampCompensation - 1.0f) * currentMix;

    if (inGainSmoothed.isSmoothing()) {
        for (int i = 0; i < numSamples; ++i) {
            float g = inGainSmoothed.getNextValue();
            for (int ch = 0; ch < numChannels; ++ch)
                buffer.setSample(ch, i, buffer.getSample(ch, i) * g);
        }
    } else {
        buffer.applyGain(inGainSmoothed.getCurrentValue());
    }

    osWorkBuffer.copyFrom(0, 0, buffer, 0, 0, numSamples);
    if (numChannels > 1) osWorkBuffer.copyFrom(1, 0, buffer, 1, 0, numSamples);
    else osWorkBuffer.copyFrom(1, 0, buffer, 0, 0, numSamples);

    juce::dsp::AudioBlock<float> fullBlock(osWorkBuffer);
    auto block = fullBlock.getSubBlock(0, (size_t)numSamples);
    auto osBlock = oversampler->processSamplesUp(block);

    const int osNumSamples = (int)osBlock.getNumSamples();
    auto* osL = osBlock.getChannelPointer(0);
    auto* osR = osBlock.getChannelPointer(1);

    for (int i = 0; i < osNumSamples; ++i)
    {
        // Левый канал
        float wetL = osL[i];
        wetL = transL.process(wetL, currentIron, currentMix);
        wetL = spiralL.process(wetL, finalDrive, presence, currentMix);
        osL[i] = wetL * finalPostGain;

        // Правый канал
        float wetR = osR[i];
        wetR = transR.process(wetR, currentIron, currentMix);
        wetR = spiralR.process(wetR, finalDrive, presence, currentMix);
        osR[i] = wetR * finalPostGain;
    }

    oversampler->processSamplesDown(block);

    buffer.copyFrom(0, 0, osWorkBuffer, 0, 0, numSamples);
    if (numChannels > 1) buffer.copyFrom(1, 0, osWorkBuffer, 1, 0, numSamples);

    auto* wetL_ptr = buffer.getWritePointer(0);
    auto* wetR_ptr = numChannels > 1 ? buffer.getWritePointer(1) : nullptr;

    for (int i = 0; i < numSamples; ++i)
    {
        const float currentOut = outLvlSmoothed.getNextValue();

        // Клиппер тоже поддерживает Smart-Mix, DC Blocker чистит результат
        wetL_ptr[i] = dcBlockL.process(clipL.process(wetL_ptr[i] * currentOut, currentMix));

        if (wetR_ptr) {
            wetR_ptr[i] = dcBlockR.process(clipR.process(wetR_ptr[i] * currentOut, currentMix));
        }
    }
}

juce::AudioProcessorEditor* DomRadioDriveAudioProcessor::createEditor()
{
    return new juce::GenericAudioProcessorEditor (*this);
}

void DomRadioDriveAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml (state.createXml());
    copyXmlToBinary (*xml, destData);
}

void DomRadioDriveAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xmlState (getXmlFromBinary (data, sizeInBytes));
    if (xmlState != nullptr && xmlState->hasTagName(apvts.state.getType()))
        apvts.replaceState (juce::ValueTree::fromXml (*xmlState));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new DomRadioDriveAudioProcessor();
}